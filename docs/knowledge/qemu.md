# ESP、OVMF 與 QEMU 操作

[文件索引](../README.md) · 分類：知識

搜尋關鍵字：ESP、FAT、BOOTX64.EFI、OVMF、QEMU、NVRAM、VARS、Boot Manager。

相關原始碼：[Makefile](../../Makefile)、[tests/gop_qemu.py](../../tests/gop_qemu.py)。

本文指令在專案根目錄執行。用 `make run` 可建置並開啟 QEMU。
需重設本機 NVRAM 時，可用 `make run-clean`。以下保留手動操作步驟。

## 建立 ESP 目錄

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

## OVMF / QEMU

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

目前完整的應用畫面見 [GOP](gop.md)。測試與 log 操作見 [驗證與 Debug](testing.md)。
