# 驗證、QEMU smoke test 與 Debug 指令

[文件索引](../README.md) · 分類：知識

搜尋關鍵字：make test、test-host、test-gop、serial、QMP、PPM、nm、objdump、GDB。

相關原始碼：[Makefile](../../Makefile)、[tests/gop_host.cpp](../../tests/gop_host.cpp)、[tests/gop_qemu.py](../../tests/gop_qemu.py)。

本文指令在專案根目錄執行。

## 建置與 GOP 測試

```bash
make test
make test-host
python3 tests/gop_qemu.py
```

`make test` 檢查 MinGW 建置、PE32+ / EFI subsystem、正式 main.o 的 efi_main 符號、ESP 與正式映像一致，以及最終 PE entry RVA 等於 `_start - ImageBase`。`uefi.hpp` 的 static_assert 檢查 GOP 結構與 Boot Services / console offset。

`make test-host` 使用 Linux g++，以假韌體呼叫真正的 efi_main；目前有 114 個案例。fixture 以實際大小配置 QueryMode 資料，釋放後由 sanitizer 偵測非法存取；SetMode 會配置新 Mode/Info 並刪除舊物件。案例檢查模式選擇、逐輪釋放、警告、所有致命錯誤、console 改變 GOP、一次恢復與狀態優先順序。重新命名的 startup 入口配合 runtime stubs，確認 init/fini 次數、返回狀態與恢復先於 fini；這不是執行真實 destructor 的測試。

Sanitizer 可在新的 /tmp binary 執行：

```bash
g++ -std=c++17 -fshort-wchar -Wall -Wextra \
    -fsanitize=address,undefined -fno-omit-frame-pointer \
    tests/gop_host.cpp -o /tmp/uefi-gop-v2-host-asan
ASAN_OPTIONS=detect_leaks=1 /tmp/uefi-gop-v2-host-asan
```

fixture 在 FreePool 失敗時會保留產品的釋放嘗試紀錄，最後自行清理殘留配置，避免製造假的 leak。若 LeakSanitizer 受 ptrace 沙箱限制，依工具權限流程允許執行後，才能把 leak 檢查列為通過。

`tests/gop_qemu.py` 只使用 Python 標準函式庫，也可透過 make test-gop 執行。它將來源複製至兩個獨立的乾淨建置目錄，正式映像執行 make test，probe 執行 make test-probe。每個 VM 使用獨立 ESP 與 OVMF VARS。

測試檢查：

- 正式與 probe 都實際選到 800×600，PPM 尺寸與 serial 相符。fallback 不能當成目標解析度驗收成功。
- 上半部有文字像素；文字可讀性另以截圖視檢。下半部三個完整、分離的純 RGB 矩形，在等待按鍵期間維持尺寸與位置；此期間無新 console 訊息。
- 按鍵後程式沒有 GOP ERROR，且韌體選單接手。
- probe 必須從不同原解析度切到目標。它在真正應用返回後、runtime_fini 與韌體接手前，獨立重新取得 GOP，核對原編號、寬高、像素格式與應用 EFI_SUCCESS，輸出 `[PROBE] RESTORE_VERIFIED`。
- 正式映像的 `-vga none` 案例須得到 EFI_NOT_FOUND，沒有繪圖或等待，立即返回韌體。

Probe 原始碼是 [gop_qemu_probe.cpp](../../tests/gop_qemu_probe.cpp)，以名稱替換方式包含真正 main.cpp，沒有產品測試開關，也不修改 startup/runtime。make build-probe / test-probe 產生獨立 object、EFI 與 `output/tests/probe-esp/`，正式 main.o 與映像仍使用原入口。

## 測試產物與環境

QEMU 腳本保留 build.log、probe-build.log、每個案例的 serial.log / qemu.log，以及正向案例的三張 PPM。每次產生新的 `/tmp/uefi-gop-qemu-*` 目錄。最新第二版驗收摘要與畫面見 [完成紀錄](../history/gop-v2.md)。

每個案例預設有 45 秒啟動期限，結束或失敗後都會停止 QEMU。可用 --timeout、--ovmf-code、--ovmf-vars 調整環境。QEMU fat:rw 需要在 /var/tmp 建立暫存檔；受限制時依權限流程允許寫入後重跑。

若其他 OVMF 沒有 800×600，或 -vga none 仍提供 GOP，該驗收案例會明確失敗並保留紀錄。其他韌體的文字 console 位置與 GOP 相容性不能由本次 OVMF 測試推論。畫面文字可讀、無裁切且不與色塊重疊，另以實際截圖視檢。

## 驗證與 Debug 指令

### 查看 shell 指令的 return code

這是上一個 shell 指令的退出碼，不是 UEFI 應用程式的 `EFI_STATUS`。

```bash
echo $?
```

成功：

```text
0
```

### 檢查 EFI executable

```bash
file output/BOOTX64.efi
```

### 查看 object symbols

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

### 查看 sections

```bash
x86_64-w64-mingw32-objdump -h main.o
```

### Disassemble

```bash
x86_64-w64-mingw32-objdump -d startup.o
x86_64-w64-mingw32-objdump -d main.o
```

### 查看 PE header

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

## QEMU 與 GDB

`make debug` 會開啟 GDB stub（port 1234），並在啟動時暫停 QEMU CPU。
符號、sections 與 PE header 可用 `make inspect` 檢查。其他入口見 `make help`。
