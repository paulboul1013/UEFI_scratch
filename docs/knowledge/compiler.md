# 編譯器與 Freestanding Environment

[文件索引](../README.md) · 分類：知識

搜尋關鍵字：MinGW、compiler、freestanding、nostdlib、wchar_t、CHAR16、red zone。

相關原始碼：[Makefile](../../Makefile)。

本文指令在專案根目錄執行。

## Compiler 與 Freestanding Environment

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
runtime.cpp → runtime.o
main.cpp    → main.o
```

### Compiler Flags

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

### 編譯 `startup.cpp`

```bash
x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -c     -fno-exceptions     -fno-rtti     -fno-threadsafe-statics     -fno-asynchronous-unwind-tables     -fno-stack-protector     -mno-red-zone     -fPIC     -fPIE     -fshort-wchar     startup.cpp     -o startup.o
```

### 編譯 `main.cpp`

```bash
x86_64-w64-mingw32-g++     -nostdlib     -ffreestanding     -c     -fno-exceptions     -fno-rtti     -fno-threadsafe-statics     -fno-asynchronous-unwind-tables     -fno-stack-protector     -mno-red-zone     -fPIC     -fPIE     -fshort-wchar     main.cpp     -o main.o
```

`runtime.cpp` 使用相同編譯旗標。可用 `make compile` 編譯全部三個來源檔。
