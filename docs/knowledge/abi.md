# ABI 與呼叫慣例

[文件索引](../README.md) · 分類：知識

搜尋關鍵字：ABI、Calling Convention、Microsoft x64、System V、register、ImageHandle。

相關原始碼：[uefi.hpp](../../uefi.hpp)、[startup.cpp](../../startup.cpp)。

## ABI 與 Calling Convention

### ABI

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

### System V AMD64 ABI

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

### Microsoft x64 ABI

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

### UEFI entry 參數

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
