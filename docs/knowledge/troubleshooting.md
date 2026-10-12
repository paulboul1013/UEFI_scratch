# 建置與啟動問題排查

[文件索引](../README.md) · 分類：知識

搜尋關鍵字：cannot open output、multiple definition、undefined reference、init_array、fini_array、linker warning。

相關原始碼：[Makefile](../../Makefile)、[linker.ld](../../linker.ld)、[startup.cpp](../../startup.cpp)。

本文指令在專案根目錄執行。以下保留既有錯誤與處理方式。

## 目前遇過的錯誤

### 1. output directory 不存在

錯誤：

```text
cannot open output file output/BOOTX64.efi
```

解法：

```bash
mkdir -p output
```

### 2. `_start` multiple definition

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

### 3. undefined reference to `efi_main`

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

### 4. `.init_array/.fini_array` warning

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

目前已使用 `.init_array` 與 `atexit()` 執行建構與清理。保留全域物件測試，以確認初始化區段正常。

QEMU FAT 暫存檔與 `-vga none` 的環境限制見 [測試產物與環境](testing.md#測試產物與環境)。
