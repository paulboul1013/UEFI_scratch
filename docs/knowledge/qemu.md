# ESP、OVMF 與 QEMU 操作

[文件索引](../README.md) · 分類：知識

搜尋關鍵字：ESP、FAT、BOOTX64.EFI、OVMF、QEMU、自動開機、Boot Manager、BootOrder、NVRAM、VARS、_start、efi_main。

相關原始碼：[Makefile](../../Makefile)、[startup.cpp](../../startup.cpp)、[main.cpp](../../main.cpp)、[tests/gop_qemu.py](../../tests/gop_qemu.py)。

本文指令在專案根目錄執行。用 `make run` 可建置並開啟 QEMU。
需重設本機 NVRAM 時，可用 `make run-clean`。以下保留手動操作步驟。

## 為何啟動 QEMU 就顯示自訂畫面

目前的設定讓 OVMF 自動從虛擬磁碟開機，載入你的 EFI 程式。程式執行後輸出文字與三色塊，再等待按鍵，因此啟動 QEMU 後就直接看到自訂畫面。

| 角色 | 負責的工作 |
|---|---|
| QEMU | 模擬 CPU、磁碟與顯示裝置，執行 OVMF 韌體 |
| OVMF | 初始化 UEFI 環境，由 Boot Manager 選擇並載入開機程式 |
| 本專案的 EFI 程式 | 執行 C++ 初始化、輸出文字、以 GOP 畫圖並等待按鍵 |

### 從主機目錄到開機檔案

`make run` 先執行建置。Makefile 把 `output/BOOTX64.efi` 複製到：

```text
主機端：esp/EFI/BOOT/BOOTX64.EFI
```

QEMU 的磁碟設定是：

```text
-drive format=raw,file=fat:rw:esp
```

這會把主機的 `esp/` 目錄內容呈現為虛擬 FAT 磁碟。OVMF 看到的磁碟內路徑是 `\EFI\BOOT\BOOTX64.EFI`；`esp/` 是主機端的目錄名稱。
依據：[Makefile](../../Makefile)、[QEMU 虛擬 FAT 文件](https://www.qemu.org/docs/master/system/images.html#virtual-fat-disk-images)。

`\EFI\BOOT\BOOTX64.EFI` 是 x64 UEFI 的標準預設開機路徑。韌體的 Boot Manager 依開機選項與順序選擇裝置；選項指向檔案系統裝置而未指定映像檔時，會尋找預設開機檔案。
依據：[UEFI Boot Manager 規格](https://github.com/UEFI/UEFI-Specification-Release/blob/main/source/03_Boot_Manager.rst)。

本專案的實測 serial 顯示 OVMF 自動載入並啟動 `UEFI QEMU HARDDISK`，接著出現應用程式訊息。這是目前組態的實際行為；開機選單的顯示時機仍由韌體設定與開機策略決定。
證據：[GOP 第二版 serial](../artifacts/gop-v2/positive-serial.log)。

### 從映像入口到畫面

正常執行順序如下：

```text
QEMU 執行 OVMF
  ↓
OVMF Boot Manager 選中虛擬 FAT 磁碟
  ↓
載入 EFI/BOOT/BOOTX64.EFI
  ↓
PE 載入器呼叫映像入口 _start()
  ↓
runtime_init()：執行全域建構子
  ↓
回到 _start()，再呼叫 efi_main()
  ↓
ConOut->OutputString()：輸出文字
  ↓
GOP->SetMode()：需要時切換解析度
  ↓
GOP->Blt()：填入紅、綠、藍色塊
  ↓
BootServices->WaitForEvent()：等待按鍵
```

Makefile 的 `-Wl,-e,_start` 將 `_start` 設為 PE 映像入口。`efi_main` 是本專案由 `_start()` 呼叫的應用函式名稱。實際呼叫關係見 [startup.cpp](../../startup.cpp)，PE 與 entry point 原理見 [Linker 文件](linker.md)。

`efi_main()` 透過韌體提供的 console 與 GOP 介面產生畫面。程式最後等待鍵盤事件，所以文字與色塊會維持顯示。按鍵後，應用先恢復原模式，再返回 `_start()` 執行 `runtime_fini()`，最後把控制權交回 OVMF。
實作見 [main.cpp](../../main.cpp)，模式與 BLT 細節見 [GOP](gop.md)，初始化與清理見 [C++ runtime](runtime.md)。

### 開機順序與 VARS

UEFI 的 `BootOrder` 決定開機選項順序，`Boot####` 保存各選項。這些變數由韌體保存在 NVRAM；本專案使用可寫的 `OVMF_VARS_4M.fd` 保存韌體變數。
依據：[UEFI Boot Manager 規格](https://github.com/UEFI/UEFI-Specification-Release/blob/main/source/03_Boot_Manager.rst)。

`make run` 沿用本機的 VARS 檔案。`make run-clean` 先還原原始 VARS，再啟動 QEMU，因此會重設先前保存的韌體設定。選單操作與自動開機都是 Boot Manager 選擇開機程式的方式。

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

### 手動選擇開機裝置

若進入 OVMF Boot Manager，可選擇：

```text
UEFI QEMU HARDDISK
```

選 Enter 後：

```text
BOOTX64.EFI
 ↓
_start
 ↓
runtime_init
 ↓
efi_main
 ↓
Hello UEFI
```

目前完整的應用畫面見 [GOP](gop.md)。測試與 log 操作見 [驗證與 Debug](testing.md)。
