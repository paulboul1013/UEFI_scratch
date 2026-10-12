# Linker、PE32+ 與 Entry Point

[文件索引](../README.md) · 分類：知識

搜尋關鍵字：linker、linker.ld、PE32+、section、symbol、relocation、entry point。

相關原始碼：[linker.ld](../../linker.ld)、[Makefile](../../Makefile)。

本文指令在專案根目錄執行。

## Linker、PE+ 與 Entry Point

Object file 裡包含：

```text
.text
.data
.bss
symbols
relocations
```

但是 `.o` 不是完整 executable。

Linker 負責：

```text
- merge sections
- resolve symbols
- assign addresses
- apply relocations
- set executable entry point
- create PE32+ image
```

### Linker Script

`linker.ld` 決定最後 image layout。

目前主要 section：

```text
.text
└── machine code

.rdata
└── read-only data

.data
└── initialized writable data

.bss
└── zero/uninitialized data

.init_array
└── C++ global constructors

.fini_array
└── destructors

.pdata / .xdata / .eh_frame
└── unwind / exception metadata

.reloc
└── PE relocation data

.debug_*
└── DWARF debug information
```

目前 `runtime_init()` 會執行 `.init_array`。全域解構函式由 MinGW 的 `atexit()` 註冊，再由 `runtime_fini()` 執行。
目前不遍歷 `.fini_array`。

### Link Command

```bash
mkdir -p output
```

然後：

```bash
x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -Wl,-T,linker.ld     -Wl,--image-base,0x400000     -Wl,-m,i386pep     -Wl,--subsystem,10     -Wl,-e,_start     startup.o     runtime.o     main.o     -o output/BOOTX64.efi
```

| Option | 說明 |
|---|---|
| `-Wl,...` | 將參數傳給 linker |
| `-T linker.ld` | 使用自訂 linker script |
| `--image-base,0x400000` | Preferred image base |
| `-m,i386pep` | 產生 x86_64 PE+ |
| `--subsystem,10` | PE subsystem = EFI Application |
| `-e,_start` | PE executable entry point = `_start` |
| `-o output/BOOTX64.efi` | 最終輸出 |

### Compiler vs Linker vs Loader

```text
BUILD TIME

.cpp
 ↓
Compiler
 ↓
.o
 ↓
Linker
 ↓
BOOTX64.efi
```

```text
RUN TIME

BOOTX64.efi
 ↓
UEFI PE Loader
 ↓
Memory
 ↓
_start
```

## 完整 Build 流程

```bash
# Compile startup.cpp
x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -c     -fno-exceptions     -fno-rtti     -fno-threadsafe-statics     -fno-asynchronous-unwind-tables     -fno-stack-protector     -mno-red-zone     -fPIC     -fPIE     -fshort-wchar     startup.cpp     -o startup.o

# Compile runtime.cpp
x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -c     -fno-exceptions     -fno-rtti     -fno-threadsafe-statics     -fno-asynchronous-unwind-tables     -fno-stack-protector     -mno-red-zone     -fPIC     -fPIE     -fshort-wchar     runtime.cpp     -o runtime.o

# Compile main.cpp
x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -c     -fno-exceptions     -fno-rtti     -fno-threadsafe-statics     -fno-asynchronous-unwind-tables     -fno-stack-protector     -mno-red-zone     -fPIC     -fPIE     -fshort-wchar     main.cpp     -o main.o

# Link
mkdir -p output

x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -Wl,-T,linker.ld     -Wl,--image-base,0x400000     -Wl,-m,i386pep     -Wl,--subsystem,10     -Wl,-e,_start     startup.o     runtime.o     main.o     -o output/BOOTX64.efi

# Copy to ESP
mkdir -p esp/EFI/BOOT

cp output/BOOTX64.efi    esp/EFI/BOOT/BOOTX64.EFI
```

啟動：

```bash
qemu-system-x86_64     -m 256M     -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd     -drive if=pflash,format=raw,file=OVMF_VARS_4M.fd     -drive format=raw,file=fat:rw:esp
```
