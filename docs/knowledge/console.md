# System Table、文字輸出與鍵盤事件

[文件索引](../README.md) · 分類：知識

搜尋關鍵字：System Table、ConOut、ConIn、OutputString、WaitForEvent、ReadKeyStroke、EFI_EVENT。

相關原始碼：[uefi.hpp](../../uefi.hpp)、[main.cpp](../../main.cpp)。

## System Table、Protocol 與 Hello UEFI

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

### `EFI_SYSTEM_TABLE`

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

目前應用程式使用的入口：

```text
ConOut       → OutputString()
ConIn        → WaitForKey / ReadKeyStroke()
BootServices → WaitForEvent() / LocateProtocol()
```

### `EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL`

`uefi.hpp` 宣告的前置欄位：

```text
Reset
OutputString
```

目前程式呼叫 `OutputString()`。`Reset` 保留在結構中，以維持正確欄位順序。

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

### Hello UEFI

文字輸出的基本呼叫：

```cpp
CHAR16 message[] = L"Hello UEFI\r\n";

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

目前程式用鍵盤事件保持畫面：

```cpp
EFI_EVENT events[] = {system_table->ConIn->WaitForKey};
UINTN index = 0;
EFI_STATUS status = system_table->BootServices->WaitForEvent(1, events, &index);
```

`efi_main()` 檢查狀態後呼叫 `ReadKeyStroke()`，再返回 `_start()` 完成清理。
