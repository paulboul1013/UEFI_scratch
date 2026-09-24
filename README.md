# UEFI From Scratch — Learning Notes

> 目前進度：已完成從 **Linux / WSL2 → MinGW cross-compiler → PE32+ EFI application → OVMF/QEMU → `Hello UEFI`** 的最小可執行流程。

---

## 目前成果

目前已成功產生：

```text
output/BOOTX64.efi
```

並確認：

```bash
file output/BOOTX64.efi
```

輸出類似：

```text
PE32+ executable (EFI application) x86-64
```

接著把它放進：

```text
esp/EFI/BOOT/BOOTX64.EFI
```

使用 QEMU + OVMF 啟動後，可以在 OVMF Boot Manager 選擇：

```text
UEFI QEMU HARDDISK
```

並成功看到：

```text
Hello UEFI
```

目前執行鏈：

```text
QEMU
  ↓
OVMF / TianoCore
  ↓
EFI/BOOT/BOOTX64.EFI
  ↓
PE Loader
  ↓
_start
  ↓
efi_main()
  ↓
EFI_SYSTEM_TABLE
  ↓
ConOut
  ↓
OutputString()
  ↓
Hello UEFI
```

---

# 專案結構

```text
.
├── OVMF_VARS_4M.fd
├── README.md
├── esp
│   └── EFI
│       └── BOOT
│           └── BOOTX64.EFI
├── linker.ld
├── main.cpp
├── main.o
├── main.o.
├── output
│   └── BOOTX64.efi
├── startup.cpp
├── startup.o
└── uefi.hpp
```

各檔案用途：

```text
uefi.hpp
└── 最小 UEFI 型別與 Protocol 定義

startup.cpp
└── 定義 PE entry point：_start

main.cpp
└── 定義 efi_main()，目前負責輸出 Hello UEFI

linker.ld
└── 定義 PE image section layout

startup.o
main.o
└── compiler 產生的 object files

output/BOOTX64.efi
└── linker 產生的 EFI application

esp/EFI/BOOT/BOOTX64.EFI
└── QEMU/OVMF 實際載入的 fallback EFI executable

OVMF_VARS_4M.fd
└── OVMF 可寫 NVRAM 狀態
```

`main.o.` 看起來像是不小心產生的多餘檔案，可確認後刪除：

```bash
rm -f main.o.
```

---

# Chapter 1 — UEFI 整體架構

UEFI 可以先理解成：

```text
Firmware 執行環境
+
EFI executable loader
+
Services / Protocols
```

和一般 Linux application 不同：

```text
Linux Application

ELF
 ↓
Linux Loader
 ↓
CRT
 ↓
main()
 ↓
libc / syscall
 ↓
Linux Kernel
```

UEFI Application：

```text
PE32+
 ↓
UEFI Firmware
 ↓
_start
 ↓
efi_main()
 ↓
UEFI Services / Protocols
```

本專案目標：

```text
Host:
Linux / WSL2

Target:
x86_64 UEFI Application
```

UEFI x86_64 executable 使用：

```text
PE32+
```

而不是 Linux 常見的：

```text
ELF
```

---

# Chapter 2 — ABI 與 Calling Convention

## ABI

ABI：

```text
Application Binary Interface
```

規定 binary 層級如何互相合作，例如：

```text
- function arguments 放哪些 registers
- return value 放哪
- stack alignment
- caller-saved / callee-saved registers
- struct layout
```

Calling Convention 是 ABI 的一部分。

## System V AMD64 ABI

Linux x86_64 常見：

```text
arg1 → RDI
arg2 → RSI
arg3 → RDX
arg4 → RCX
arg5 → R8
arg6 → R9

return → RAX
```

## Microsoft x64 ABI

x86_64 UEFI 使用 MSVC-compatible calling convention：

```text
arg1 → RCX
arg2 → RDX
arg3 → R8
arg4 → R9

return → RAX
```

因此：

```text
CPU Architecture 相同
≠
ABI 相同
```

這也是為什麼本專案使用：

```bash
x86_64-w64-mingw32-g++
```

而不是直接依賴 Linux 預設 SysV ABI。

## UEFI entry 參數

UEFI entry 最重要的兩個參數：

```text
arg1 = EFI_HANDLE ImageHandle
arg2 = EFI_SYSTEM_TABLE *SystemTable
```

在 Microsoft x64 ABI 下概念上：

```text
RCX = ImageHandle
RDX = SystemTable
```

---

# Chapter 3 — Compiler 與 Freestanding Environment

Source：

```text
.cpp
```

經過 compiler：

```text
.cpp
 ↓
compiler
 ↓
.o
```

目前會產生：

```text
startup.cpp → startup.o
main.cpp    → main.o
```

## Compiler Flags

本專案目前使用：

```bash
-nostdlib
-ffreestanding
-c
-fno-exceptions
-fno-rtti
-fno-threadsafe-statics
-fno-asynchronous-unwind-tables
-fno-stack-protector
-mno-red-zone
-fPIC
-fPIE
-fshort-wchar
```

| Flag | 用途 |
|---|---|
| `-ffreestanding` | 不把程式視為普通 hosted C/C++ application |
| `-nostdlib` | 不依賴一般 standard runtime/library；link 時尤其重要 |
| `-c` | 只 compile 成 object，不進行 final link |
| `-fshort-wchar` | 讓 `wchar_t` 為 16-bit，配合 UEFI `CHAR16` |
| `-mno-red-zone` | 禁止 compiler 使用 x86_64 red zone |
| `-fPIC` | 產生 position-independent code |
| `-fPIE` | 產生適合 position-independent executable 的 code |
| `-fno-exceptions` | 關閉 C++ exception runtime |
| `-fno-rtti` | 關閉 C++ RTTI |
| `-fno-stack-protector` | 避免產生 `__stack_chk_*` 等 runtime dependency |
| `-fno-threadsafe-statics` | 關閉 local static 的 thread-safe initialization runtime |
| `-fno-asynchronous-unwind-tables` | 關閉部分 automatic unwind metadata |

核心概念：

```text
目前沒有一般 OS runtime
沒有 libc
沒有完整 C++ runtime
```

所以先把需要額外 runtime 支援的功能關掉。

## 編譯 `startup.cpp`

```bash
x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -c     -fno-exceptions     -fno-rtti     -fno-threadsafe-statics     -fno-asynchronous-unwind-tables     -fno-stack-protector     -mno-red-zone     -fPIC     -fPIE     -fshort-wchar     startup.cpp     -o startup.o
```

## 編譯 `main.cpp`

```bash
x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -c     -fno-exceptions     -fno-rtti     -fno-threadsafe-statics     -fno-asynchronous-unwind-tables     -fno-stack-protector     -mno-red-zone     -fPIC     -fPIE     -fshort-wchar     main.cpp     -o main.o
```

---

# Chapter 4 — Linker、PE+ 與 Entry Point

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

## Linker Script

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

目前 `.init_array/.fini_array` 尚未真正使用，runtime 會在後續章節處理。

## Link Command

```bash
mkdir -p output
```

然後：

```bash
x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -Wl,-T,linker.ld     -Wl,--image-base,0x400000     -Wl,-m,i386pep     -Wl,--subsystem,10     -Wl,-e,_start     startup.o     main.o     -o output/BOOTX64.efi
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

## Compiler vs Linker vs Loader

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

---

# Chapter 5 — `_start` → `efi_main()`

目前：

```text
startup.cpp
```

負責定義：

```text
_start
```

而：

```text
main.cpp
```

負責定義：

```text
efi_main
```

關係：

```text
startup.o

T _start
U efi_main
```

```text
main.o

T efi_main
```

Linker 將：

```text
startup.o 的 U efi_main
```

解析到：

```text
main.o 的 T efi_main
```

## Runtime Entry Flow

```text
UEFI Firmware

RCX = ImageHandle
RDX = SystemTable

      ↓

_start(
    ImageHandle,
    SystemTable
)

      ↓

efi_main(
    ImageHandle,
    SystemTable
)

      ↓

RAX = EFI_STATUS
```

目前 `_start` 只是最小 bridge：

```text
Firmware
 ↓
_start
 ↓
efi_main
```

完整 C/C++ runtime 尚未實作。

---

# Chapter 6 — System Table、Protocol 與 Hello UEFI

UEFI application 沒有：

```text
printf()
Linux syscall
glibc
```

因此文字輸出改成：

```text
EFI_SYSTEM_TABLE
      ↓
ConOut
      ↓
EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL
      ↓
OutputString()
```

## `EFI_SYSTEM_TABLE`

目前 `uefi.hpp` 定義最小需要的 System Table layout。

```text
EFI_SYSTEM_TABLE
│
├── Hdr
├── FirmwareVendor
├── FirmwareRevision
├── ConsoleInHandle
├── ConIn
├── ConsoleOutHandle
├── ConOut
├── StandardErrorHandle
├── StdErr
├── RuntimeServices
├── BootServices
├── NumberOfTableEntries
└── ConfigurationTable
```

目前真正使用：

```text
ConOut
```

## `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL`

目前使用：

```text
Reset
OutputString
```

呼叫：

```cpp
system_table->ConOut->OutputString(
    system_table->ConOut,
    message
);
```

可以理解成 C-style OOP：

```text
object->method(
    object,
    argument
)
```

其中第一個 argument：

```text
This = system_table->ConOut
```

## Hello UEFI

目前 `main.cpp` 核心：

```cpp
CHAR16 message[] = L"Hello UEFI
";

system_table->ConOut->OutputString(
    system_table->ConOut,
    message
);
```

由於使用：

```bash
-fshort-wchar
```

所以：

```text
L"Hello UEFI"
 ↓
16-bit wchar_t
 ↓
CHAR16
 ↓
UEFI OutputString
```

目前為了讓畫面停住，可以暫時：

```cpp
for (;;) {
    __asm__ __volatile__("hlt");
}
```

這只是測試手段，不是正式 UEFI event loop。

---

# 建立 ESP 目錄

```bash
mkdir -p esp/EFI/BOOT
```

把最新 EFI executable 複製進去：

```bash
cp output/BOOTX64.efi    esp/EFI/BOOT/BOOTX64.EFI
```

確認：

```bash
find esp -type f
```

預期：

```text
esp/EFI/BOOT/BOOTX64.EFI
```

確認格式：

```bash
file esp/EFI/BOOT/BOOTX64.EFI
```

預期：

```text
PE32+ executable (EFI application) x86-64
```

---

# OVMF / QEMU

OVMF：

```text
Open Virtual Machine Firmware
```

在 QEMU 中提供 UEFI firmware。

目前使用：

```text
/usr/share/OVMF/OVMF_CODE_4M.fd
/usr/share/OVMF/OVMF_VARS_4M.fd
```

建立可寫 VARS copy：

```bash
cp /usr/share/OVMF/OVMF_VARS_4M.fd    ./OVMF_VARS_4M.fd
```

啟動：

```bash
qemu-system-x86_64     -m 256M     -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd     -drive if=pflash,format=raw,file=OVMF_VARS_4M.fd     -drive format=raw,file=fat:rw:esp
```

其中：

```text
OVMF_CODE_4M.fd
└── Firmware code

OVMF_VARS_4M.fd
└── 可寫 UEFI NVRAM

fat:rw:esp
└── 把 Linux 的 esp/ 目錄模擬成 FAT disk
```

在 OVMF Boot Manager：

```text
UEFI QEMU HARDDISK
```

選 Enter 後：

```text
BOOTX64.EFI
 ↓
_start
 ↓
efi_main
 ↓
Hello UEFI
```

---

# 驗證與 Debug 指令

## 查看 return code

```bash
echo $?
```

成功：

```text
0
```

## 檢查 EFI executable

```bash
file output/BOOTX64.efi
```

## 查看 object symbols

```bash
x86_64-w64-mingw32-nm startup.o
x86_64-w64-mingw32-nm main.o
```

重要：

```text
startup.o
T _start
U efi_main
```

```text
main.o
T efi_main
```

## 查看 sections

```bash
x86_64-w64-mingw32-objdump -h main.o
```

## Disassemble

```bash
x86_64-w64-mingw32-objdump -d startup.o
x86_64-w64-mingw32-objdump -d main.o
```

## 查看 PE header

```bash
x86_64-w64-mingw32-objdump     -x output/BOOTX64.efi
```

主要觀察：

```text
pei-x86-64
AddressOfEntryPoint
Subsystem
ImageBase
Sections
```

---

# 目前遇過的錯誤

## 1. output directory 不存在

錯誤：

```text
cannot open output file output/BOOTX64.efi
```

解法：

```bash
mkdir -p output
```

## 2. `_start` multiple definition

原因：

```text
startup.o 定義 _start
main.o 也定義 _start
```

正確：

```text
startup.cpp
└── _start

main.cpp
└── efi_main
```

## 3. undefined reference to `efi_main`

正確 symbol relationship：

```text
startup.o
U efi_main

main.o
T efi_main
```

確認：

```bash
x86_64-w64-mingw32-nm startup.o
x86_64-w64-mingw32-nm main.o
```

## 4. `.init_array/.fini_array` warning

目前 linker 曾出現：

```text
stripping non-representable symbol '__init_array_start'
stripping non-representable symbol '__init_array_end'
...
```

但：

```bash
echo $?
```

仍為：

```text
0
```

且成功產生可被 OVMF 執行的 EFI application。

因此目前先保留，等 Runtime chapter 再整理 constructor/destructor layout。

---

# 完整 Build 流程

```bash
# Compile startup.cpp
x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -c     -fno-exceptions     -fno-rtti     -fno-threadsafe-statics     -fno-asynchronous-unwind-tables     -fno-stack-protector     -mno-red-zone     -fPIC     -fPIE     -fshort-wchar     startup.cpp     -o startup.o

# Compile main.cpp
x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -c     -fno-exceptions     -fno-rtti     -fno-threadsafe-statics     -fno-asynchronous-unwind-tables     -fno-stack-protector     -mno-red-zone     -fPIC     -fPIE     -fshort-wchar     main.cpp     -o main.o

# Link
mkdir -p output

x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -Wl,-T,linker.ld     -Wl,--image-base,0x400000     -Wl,-m,i386pep     -Wl,--subsystem,10     -Wl,-e,_start     startup.o     main.o     -o output/BOOTX64.efi

# Copy to ESP
mkdir -p esp/EFI/BOOT

cp output/BOOTX64.efi    esp/EFI/BOOT/BOOTX64.EFI
```

啟動：

```bash
qemu-system-x86_64     -m 256M     -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd     -drive if=pflash,format=raw,file=OVMF_VARS_4M.fd     -drive format=raw,file=fat:rw:esp
```

---

# 到目前為止的完整資料流

```text
                    SOURCE

          startup.cpp      main.cpp
               │               │
               └──────┬────────┘
                      ▼

                   COMPILER
       x86_64-w64-mingw32-g++

                      │
                      ▼

           startup.o      main.o
               │             │
               └──────┬──────┘
                      │
                      │ + linker.ld
                      ▼

                    LINKER

                      │
                      ▼

              output/BOOTX64.efi
                      │
                      │ cp
                      ▼

          esp/EFI/BOOT/BOOTX64.EFI

                      │
                      ▼

              QEMU + OVMF

                      │
                      ▼

                 PE Loader

                      │
                      ▼

                   _start

                      │
                      ▼

                  efi_main

                      │
                      ▼

              EFI_SYSTEM_TABLE

                      │
                      ▼

                    ConOut

                      │
                      ▼

                 OutputString

                      │
                      ▼

                  Hello UEFI
```

---

# 目前進度

已完成：

```text
Chapter 1
UEFI / Firmware / Application 架構                    ✔

Chapter 2
ABI / Calling Convention / MS x64                    ✔

Chapter 3
Freestanding Compiler / Compiler Flags               ✔

Chapter 4
Linker / PE32+ / linker.ld / Entry Point              ✔

Chapter 5
_start → efi_main                                    ✔

Chapter 6
EFI_SYSTEM_TABLE / ConOut / OutputString / Hello      ✔
```

下一階段尚未開始：

```text
- Simple Text Input / keyboard
- EFI_EVENT / WaitForEvent
- Boot Services
- C/C++ Runtime
- .init_array / global constructors
- destructors
- assert / panic
- __cxa_* helpers
- QEMU + GDB debugging
```

目前這個版本可以視為：

```text
Minimal working x86_64 UEFI Application
```
