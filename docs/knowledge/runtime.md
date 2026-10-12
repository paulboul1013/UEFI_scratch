# C++ runtime 與應用程式生命週期

[文件索引](../README.md) · 分類：知識

搜尋關鍵字：_start、efi_main、init_array、atexit、constructor、destructor、runtime_fini。

相關原始碼：[startup.cpp](../../startup.cpp)、[runtime.cpp](../../runtime.cpp)、[runtime.hpp](../../runtime.hpp)。

## `_start` → `efi_main()`

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

### Runtime Entry Flow

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

目前 `_start` 負責初始化、呼叫應用程式與清理：

```text
Firmware
 ↓
_start
 ↓
runtime_init()
 ↓
efi_main
 ↓
runtime_fini()
 ↓
以 efi_main 的 EFI_STATUS 返回韌體
```

本版提供最小 C++ runtime。例外、RTTI 與完整標準函式庫仍未啟用。

## 初始化與清理

`runtime_init()` 依序呼叫 linker 提供的初始化陣列。`atexit()` 最多登記 32 個清理函式。
`runtime_fini()` 以登記的反向順序執行清理，並在呼叫前移除該筆登記。

本專案的 C++ runtime 與 `EFI_SYSTEM_TABLE->RuntimeServices` 所指向的 UEFI Runtime Services 不同。
目前 `_start()` 在 `efi_main()` 返回後完成清理，再將原本的 `EFI_STATUS` 返回韌體。
