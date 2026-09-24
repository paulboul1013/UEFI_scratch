# UEFI From Scratch - Makefile
#
# Common workflow:
#   make            # compile + link + copy to ESP
#   make test       # build + validate generated EFI image
#   make run        # build + launch QEMU/OVMF
#   make debug      # build + launch QEMU paused with GDB stub on :1234
#   make inspect    # show symbols / sections / PE headers
#   make clean
#   make rebuild

# ------------------------------------------------------------
# Toolchain
# ------------------------------------------------------------

CXX      := x86_64-w64-mingw32-g++
NM       := x86_64-w64-mingw32-nm
OBJDUMP  := x86_64-w64-mingw32-objdump
QEMU     := qemu-system-x86_64

# ------------------------------------------------------------
# Files / directories
# ------------------------------------------------------------

TARGET       := output/BOOTX64.efi
ESP_TARGET   := esp/EFI/BOOT/BOOTX64.EFI

SOURCES      := startup.cpp main.cpp
OBJECTS      := startup.o main.o

LINKER_SCRIPT := linker.ld

OUTPUT_DIR   := output
ESP_DIR      := esp/EFI/BOOT

# ------------------------------------------------------------
# OVMF
# ------------------------------------------------------------

OVMF_CODE_SRC := /usr/share/OVMF/OVMF_CODE_4M.fd
OVMF_VARS_SRC := /usr/share/OVMF/OVMF_VARS_4M.fd
OVMF_VARS     := OVMF_VARS_4M.fd

# ------------------------------------------------------------
# Compiler flags
# ------------------------------------------------------------

CXXFLAGS := \
	-nostdlib \
	-ffreestanding \
	-fno-exceptions \
	-fno-rtti \
	-fno-threadsafe-statics \
	-fno-asynchronous-unwind-tables \
	-fno-stack-protector \
	-mno-red-zone \
	-fPIC \
	-fPIE \
	-fshort-wchar

# ------------------------------------------------------------
# Linker flags
# ------------------------------------------------------------

LDFLAGS := \
	-nostdlib \
	-ffreestanding \
	-Wl,-T,$(LINKER_SCRIPT) \
	-Wl,--image-base,0x400000 \
	-Wl,-m,i386pep \
	-Wl,--subsystem,10 \
	-Wl,-e,_start

# ------------------------------------------------------------
# QEMU flags
# ------------------------------------------------------------

QEMU_FLAGS := \
	-m 256M \
	-drive if=pflash,format=raw,readonly=on,file=$(OVMF_CODE_SRC) \
	-drive if=pflash,format=raw,file=$(OVMF_VARS) \
	-drive format=raw,file=fat:rw:esp

# ------------------------------------------------------------
# Default target
# ------------------------------------------------------------

.PHONY: all
all: build

# ------------------------------------------------------------
# Dependency checks
# ------------------------------------------------------------

.PHONY: check-tools
check-tools:
	@command -v $(CXX) >/dev/null 2>&1 || { echo "ERROR: $(CXX) not found"; exit 1; }
	@command -v $(NM) >/dev/null 2>&1 || { echo "ERROR: $(NM) not found"; exit 1; }
	@command -v $(OBJDUMP) >/dev/null 2>&1 || { echo "ERROR: $(OBJDUMP) not found"; exit 1; }
	@command -v $(QEMU) >/dev/null 2>&1 || { echo "ERROR: $(QEMU) not found"; exit 1; }
	@test -f "$(OVMF_CODE_SRC)" || { echo "ERROR: $(OVMF_CODE_SRC) not found"; exit 1; }
	@test -f "$(OVMF_VARS_SRC)" || { echo "ERROR: $(OVMF_VARS_SRC) not found"; exit 1; }
	@test -f "$(LINKER_SCRIPT)" || { echo "ERROR: $(LINKER_SCRIPT) not found"; exit 1; }
	@echo "Toolchain / OVMF check: OK"

# ------------------------------------------------------------
# Directory setup
# ------------------------------------------------------------

$(OUTPUT_DIR):
	mkdir -p $(OUTPUT_DIR)

$(ESP_DIR):
	mkdir -p $(ESP_DIR)

# ------------------------------------------------------------
# OVMF writable variable store
# ------------------------------------------------------------

$(OVMF_VARS):
	cp $(OVMF_VARS_SRC) $(OVMF_VARS)

.PHONY: reset-vars
reset-vars:
	rm -f $(OVMF_VARS)
	cp $(OVMF_VARS_SRC) $(OVMF_VARS)
	@echo "OVMF variable store reset: $(OVMF_VARS)"

# ------------------------------------------------------------
# Compile
# ------------------------------------------------------------

startup.o: startup.cpp uefi.hpp
	$(CXX) $(CXXFLAGS) -c startup.cpp -o startup.o

main.o: main.cpp uefi.hpp
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

.PHONY: compile
compile: $(OBJECTS)

# ------------------------------------------------------------
# Link
# ------------------------------------------------------------

$(TARGET): $(OBJECTS) $(LINKER_SCRIPT) | $(OUTPUT_DIR)
	$(CXX) $(LDFLAGS) $(OBJECTS) -o $(TARGET)

.PHONY: link
link: $(TARGET)

# ------------------------------------------------------------
# Copy EFI executable to fallback ESP path
# ------------------------------------------------------------

$(ESP_TARGET): $(TARGET) | $(ESP_DIR)
	cp $(TARGET) $(ESP_TARGET)

.PHONY: esp
esp: $(ESP_TARGET)

# ------------------------------------------------------------
# Complete build
# ------------------------------------------------------------

.PHONY: build
build: check-tools $(ESP_TARGET)
	@echo
	@echo "Build complete:"
	@echo "  $(TARGET)"
	@echo "  $(ESP_TARGET)"

# ------------------------------------------------------------
# Validation / test
# ------------------------------------------------------------

.PHONY: test
test: build
	@echo
	@echo "========== FILE FORMAT =========="
	@file $(TARGET)
	@file $(ESP_TARGET)

	@echo
	@echo "========== REQUIRED SYMBOLS =========="
	@$(NM) startup.o | grep -E '(_start|efi_main)' || true
	@$(NM) main.o    | grep -E '(_start|efi_main)' || true
	@echo
	@echo "Expected relationship:"
	@echo "  startup.o : T _start"
	@echo "  startup.o : U efi_main"
	@echo "  main.o    : T efi_main"

	@echo
	@echo "========== PE HEADER =========="
	@$(OBJDUMP) -x $(TARGET) | grep -E \
		'file format|AddressOfEntryPoint|ImageBase|Subsystem' || true

	@echo
	@echo "========== SECTIONS =========="
	@$(OBJDUMP) -h $(TARGET)

	@echo
	@echo "========== ESP =========="
	@find esp -type f -print

	@echo
	@echo "========== RESULT =========="
	@test -f "$(TARGET)"
	@test -f "$(ESP_TARGET)"
	@file "$(TARGET)" | grep -q "PE32+" || { echo "ERROR: output is not PE32+"; exit 1; }
	@file "$(TARGET)" | grep -qi "EFI application" || { echo "ERROR: output is not marked EFI application"; exit 1; }
	@$(NM) startup.o | grep -qE '[[:space:]]T[[:space:]]+_start$$' || { echo "ERROR: startup.o does not define _start"; exit 1; }
	@$(NM) startup.o | grep -qE '[[:space:]]U[[:space:]]+efi_main$$' || { echo "ERROR: startup.o does not reference efi_main"; exit 1; }
	@$(NM) main.o | grep -qE '[[:space:]]T[[:space:]]+efi_main$$' || { echo "ERROR: main.o does not define efi_main"; exit 1; }
	@echo "All static checks passed."

# ------------------------------------------------------------
# Inspection helpers
# ------------------------------------------------------------

.PHONY: symbols
symbols: compile
	@echo "========== startup.o =========="
	$(NM) startup.o
	@echo
	@echo "========== main.o =========="
	$(NM) main.o

.PHONY: sections
sections: build
	$(OBJDUMP) -h $(TARGET)

.PHONY: headers
headers: build
	$(OBJDUMP) -x $(TARGET)

.PHONY: disasm
disasm: compile
	@echo "========== startup.o =========="
	$(OBJDUMP) -d startup.o
	@echo
	@echo "========== main.o =========="
	$(OBJDUMP) -d main.o

.PHONY: inspect
inspect: symbols sections headers

# ------------------------------------------------------------
# QEMU run
# ------------------------------------------------------------

.PHONY: run
run: build $(OVMF_VARS)
	$(QEMU) $(QEMU_FLAGS)

# Reset OVMF NVRAM first, then run.
.PHONY: run-clean
run-clean: build reset-vars
	$(QEMU) $(QEMU_FLAGS)

# ------------------------------------------------------------
# QEMU + GDB
# ------------------------------------------------------------

# -s : start GDB stub on localhost:1234
# -S : halt CPU at startup until GDB issues "continue"
.PHONY: debug
debug: build $(OVMF_VARS)
	$(QEMU) $(QEMU_FLAGS) -s -S

# ------------------------------------------------------------
# Clean / rebuild
# ------------------------------------------------------------

.PHONY: clean
clean:
	rm -f $(OBJECTS)
	rm -f main.o.
	rm -f $(TARGET)
	rm -f $(ESP_TARGET)

.PHONY: distclean
distclean: clean
	rm -f $(OVMF_VARS)
	rm -rf $(OUTPUT_DIR)
	rm -rf esp

.PHONY: rebuild
rebuild:
	$(MAKE) clean
	$(MAKE) build

# ------------------------------------------------------------
# Help
# ------------------------------------------------------------

.PHONY: help
help:
	@echo "UEFI From Scratch Makefile"
	@echo
	@echo "Targets:"
	@echo "  make / make build   Compile, link, copy BOOTX64.EFI to ESP"
	@echo "  make compile        Compile startup.cpp and main.cpp only"
	@echo "  make link           Produce output/BOOTX64.efi"
	@echo "  make esp            Copy EFI image to esp/EFI/BOOT/"
	@echo "  make test           Run static build/PE/symbol/section checks"
	@echo "  make run            Build and boot with QEMU + OVMF"
	@echo "  make run-clean      Reset OVMF VARS, then boot"
	@echo "  make debug          Run QEMU with GDB stub (:1234), CPU paused"
	@echo "  make symbols        Show symbols in startup.o / main.o"
	@echo "  make sections       Show EFI image sections"
	@echo "  make headers        Show complete PE headers"
	@echo "  make disasm         Disassemble startup.o / main.o"
	@echo "  make inspect        symbols + sections + headers"
	@echo "  make reset-vars     Restore a clean OVMF variable store"
	@echo "  make clean          Remove build artifacts"
	@echo "  make distclean      Remove build artifacts, ESP, and local OVMF VARS"
	@echo "  make rebuild        Clean and build again"
