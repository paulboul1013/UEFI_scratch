# UEFI 架構與專案導覽

[文件索引](../README.md) · 分類：知識

搜尋關鍵字：UEFI、Firmware、PE Loader、專案結構、執行鏈。

相關原始碼：[startup.cpp](../../startup.cpp)、[main.cpp](../../main.cpp)、[uefi.hpp](../../uefi.hpp)。

## UEFI 整體架構

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

## 專案結構

```text
.
├── OVMF_VARS_4M.fd
├── README.md
├── docs
│   ├── README.md
│   ├── knowledge
│   ├── history
│   └── artifacts
├── esp
│   └── EFI
│       └── BOOT
│           └── BOOTX64.EFI
├── linker.ld
├── main.cpp
├── main.o
├── output
│   └── BOOTX64.efi
├── runtime.cpp
├── runtime.hpp
├── runtime.o
├── startup.cpp
├── startup.o
├── tests
│   ├── gop_host.cpp
│   ├── gop_qemu.py
│   └── gop_qemu_probe.cpp
└── uefi.hpp
```

各檔案用途：

```text
uefi.hpp
└── 最小 UEFI 型別與 Protocol 定義

startup.cpp
└── 定義 PE entry point：_start

main.cpp
└── 定義 efi_main()，保存原模式、列舉 GOP、切換與畫色塊，按鍵或錯誤後恢復

runtime.cpp / runtime.hpp
└── 最小 C++ runtime：全域建構子、atexit 註冊與清理

tests/gop_host.cpp / tests/gop_qemu.py / tests/gop_qemu_probe.cpp
└── 主機錯誤矩陣、QEMU 圖形與返回前恢復驗證

linker.ld
└── 定義 PE image section layout

startup.o
runtime.o
main.o
└── compiler 產生的 object files

output/BOOTX64.efi
└── linker 產生的 EFI application

esp/EFI/BOOT/BOOTX64.EFI
└── QEMU/OVMF 實際載入的 fallback EFI executable

OVMF_VARS_4M.fd
└── OVMF 可寫 NVRAM 狀態
```

## 從韌體到應用程式

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
runtime_init()：執行全域建構子
  ↓
efi_main()
  ↓
BootServices->LocateProtocol()：取得 GOP、保存原模式純量
  ↓
ConOut->OutputString()：初始訊息與模式列舉
  ↓
GOP->QueryMode() → FreePool()：複製資訊後逐輪釋放
  ↓
ConOut->ClearScreen() → GOP->SetMode()：需要時切到選定模式
  ↓
重新讀取 Mode/Info → 精簡文字 → 核對模式
  ↓
GOP->Blt(EfiBltVideoFill)：填滿三個色塊
  ↓
WaitForEvent() → ReadKeyStroke()
  ↓
需要時 SetMode(original)：驗證恢復後 efi_main 返回
  ↓
runtime_fini()：執行 atexit 清理
  ↓
返回韌體
```

## 到目前為止的完整資料流

```text
startup.cpp + runtime.cpp + main.cpp + headers
  ↓ MinGW compiler
startup.o + runtime.o + main.o
  ↓ linker + linker.ld
output/BOOTX64.efi
  ↓ copy
esp/EFI/BOOT/BOOTX64.EFI
  ↓ QEMU + OVMF PE Loader
_start → runtime_init()
  ↓
efi_main()
  ├─ LocateProtocol() → Mode/Info：先保存原模式純量
  ├─ QueryMode() → FreePool()：列舉、逐輪釋放與選擇
  ├─ ClearScreen() → 必要時 SetMode() → 核對新 Mode/Info
  ├─ 精簡文字 → Blt()：紅、綠、藍色塊
  ├─ WaitForEvent() → ReadKeyStroke()：等待按鍵
  └─ 統一恢復出口：必要時 SetMode(original)，保留主錯誤
  ↓
runtime_fini()
  ↓
返回韌體
```
