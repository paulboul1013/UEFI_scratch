# UEFI Station 文字選單

[文件索引](../README.md) · 分類：知識

搜尋關鍵字：menu、選單、Boot Manager、LoadImage、StartImage、pending image、ConOut、QueryMode、ResetSystem、watchdog。

相關原始碼：[station.cpp](../../station.cpp)、[menu.hpp](../../menu.hpp)、[menu.cpp](../../menu.cpp)、[boot.hpp](../../boot.hpp)、[boot.cpp](../../boot.cpp)、[uefi.hpp](../../uefi.hpp)、[Makefile](../../Makefile)。

## 建置與操作

```bash
make build-menu
make run-menu
```

獨立映像位於 `output/menu/BOOTX64.EFI`，ESP 位於 `output/menu/esp/`。
`run-menu` 首次從系統模板建立 `output/menu/OVMF_VARS_4M.fd`，後續使用同一份選單 VARS。
`build-menu` 同時配置 Station 與正式 GOP 映像：

```text
output/menu/esp/EFI/BOOT/BOOTX64.EFI  Station
output/menu/esp/EFI/TOOLS/GOP.EFI     GOP Demo
```

原 GOP 程式仍使用 `make run`；兩個映像各自連結一個 `efi_main`，並共用 startup/runtime。

畫面使用英文。Up / Down 移動選項，到邊界就停止；Enter 開啟子頁。
子頁 Esc 回到原選項，主頁 Esc 或 Return to Firmware 結束 EFI application。
Reset 先開啟確認頁，預設 Cancel；只有選擇 Restart 再按 Enter 才呼叫冷重啟。

| 選項 | 目前行為 |
|---|---|
| Select Language | 顯示目前使用 English；繁體中文尚未實作 |
| Device Manager | 顯示韌體廠商、廠商 revision、UEFI revision、文字尺寸及有效 GOP 資訊 |
| Boot Manager | 顯示 GOP Demo 清單；Enter 載入並執行，返回後保留項目與真實狀態，可再次啟動 |
| Boot Maintenance Manager | 顯示未設定的預設項目及尚未實作的設定功能 |
| Continue | 顯示 No default boot entry configured，留在 UI |
| Reset | Cancel / Restart 確認；方法缺少或意外返回時顯示錯誤狀態 |
| Return to Firmware | 恢復 console 設定，返回既有 `_start()` 清理流程 |

## 顯示與資料

選單沿用目前文字模式，至少 `80×25` 時使用左選單、右說明布局。
較小的模式使用單欄，最低為 `40×15`；再小則顯示短訊息並返回 `EFI_UNSUPPORTED`。
所有文字輸出依區域寬度截斷或換行，最後一欄保留空白以避免捲動。
只有頁面或選項改變時才重畫；未知按鍵與導航邊界不重畫。

資訊取自實際 System Table、文字 QueryMode 與 GOP 的目前 Mode/Info。
UEFI revision 使用 major.minor；韌體廠商 revision 另以八位十六進位顯示。
GOP 缺少或資料無效時顯示 Graphics unavailable，文字操作仍可用。
不查詢 CPU、RAM 或磁碟，不讀寫 BootOrder / BootNext / BootCurrent / Boot####。

## Boot Manager

清單使用固定項目表，顯示設定路徑 `\EFI\TOOLS\GOP.EFI`；開啟清單不載入映像，
清單也不宣稱檔案已存在。Enter 才使用 Station 的真實 image handle，查詢 Loaded Image
與其 DeviceHandle 的 Device Path，組合完整檔案路徑，再呼叫 `LoadImage(FALSE)`／`StartImage`。
此版只接受 EFI application 的 LoaderCode／LoaderData 記憶體型別，LoadOptions 為空。

來源路徑限單一 instance、1024 bytes／64 nodes，目標限 ASCII 根路徑與 259 個 CHAR16；
拒絕控制字元、`.`／`..` 路徑段與無法解讀的父路徑。
自載入防護比對同一 DeviceHandle 上的完整正規化路徑，包含大小寫、重複反斜線與連續 File Path nodes。

畫面顯示 `Image returned`，或 `Prepare failed`／`Load failed`／`Image check failed`／`Start failed`／`Cleanup failed`，
以及該次呼叫的十六位十六進位 EFI_STATUS。正常與可恢復失敗都留在清單，可 Enter 重試或 Esc 返回主頁。
Station 提供呼叫 EFI OS loader 的基礎；完整 OS 開機、kernel 參數、initrd 與磁碟探索仍需另行實作。

EFI application 返回後可能已被韌體釋放，因此先用 `OpenProtocol(TEST_PROTOCOL)` 確認存活，
再決定是否 UnloadImage；不讀取舊 Loaded Image 指標。載入錯誤仍交付 handle 的情況也會清理。
若查詢或卸載失敗，BootLoader 保留 handle，畫面只提供 Retry cleanup／Reset，Esc 不離開復原頁。
Reset 的 Cancel 回到復原頁，成功清理後才允許新啟動或返回韌體。

## 等待、錯誤與恢復

選單透過 `WaitForEvent → ReadKeyStroke` 等待輸入；`EFI_NOT_READY` 回到等待。
其他輸入或繪製錯誤經同一出口返回。進入畫面前保存屬性、游標位置及可見狀態，
正常或錯誤返回時嘗試恢復所有曾更動的設定，清理失敗不覆寫先前的主要錯誤。
子程式啟動前另存選單快照；返回時先恢復 GOP，再恢復文字模式、屬性與游標，重新查詢模式及尺寸，
重畫後再確認文字與 GOP 狀態。最終離開 Station 才恢復呼叫者快照並執行父程式 runtime_fini。
恢復失敗不使用舊尺寸繪製完整選單；完成清理後返回第一個主要錯誤。
若映像清理與 console／輸入永久失效，父程式保持持有映像並受控停止；不自動重啟。
EnableCursor 缺少或回報 `EFI_UNSUPPORTED` 時保持韌體的可見狀態。

進入等待前呼叫 `SetWatchdogTimer(0, 0, 0, nullptr)`，停用開機流程的 watchdog。
該方法回報 `EFI_UNSUPPORTED` 表示系統沒有 watchdog，可以繼續操作；
方法缺少或其他停用錯誤則顯示短訊息並返回，避免進入可能逾時重啟的等待。
StartImage 前嘗試設定 300 秒 watchdog，返回後立即停用，再清理映像與恢復畫面。
停用失敗不進入一般選單等待；有 pending image 時留在復原流程，沒有時清理 console 並返回錯誤。
這項補充源於實作 review，型別與語意對照 [EDK II UefiSpec.h](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Uefi/UefiSpec.h)，
開機時啟用與返回後停用見 [BmBoot.c](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Library/UefiBootManagerLib/BmBoot.c)。

ResetSystem 意外返回時，畫面顯示 Restart failed: ResetSystem returned，仍可 Esc 返回；
之後離開選單會保留 `EFI_DEVICE_ERROR`。真正重啟不會執行正常 runtime 清理。
返回後由呼叫者決定下一個畫面，不能保證所有韌體都顯示 OVMF 主頁。

## 驗證

```bash
make test-menu-static
make test-menu-host
make test-menu-qemu
make test-boot-host
make test-boot-qemu
make test-menu-boot-qemu
make test-menu
```

靜態檢查驗證 PE32+、EFI subsystem、唯一 efi_main、入口 `_start`、無 DLL import 與 ESP 一致性。
主機測試連結真正 boot/menu/station，使用假韌體檢查路徑、所有權、watchdog、畫面、鍵盤、資料、重啟、錯誤與恢復。
QEMU 測試從乾淨來源建置正式與 observer 映像，每台 VM 都使用獨立 ESP / VARS。
v1 observer 在真正 menu 返回後檢查 console 與 GOP，再觀察 atexit 清理順序。
v2 observer 在子映像返回後、下一次等待前檢查模式、尺寸、屬性、游標與 watchdog；
測試子映像包含 dirty／error／無 GOP 的文字程式，正式 GOP 則由真正 Station 連續啟動三輪。
另有獨立的短 watchdog 逾時 VM，以 QMP guest-reset 證明計時器確實會重啟。
重新啟動案例以 `-no-reboot` 的 QMP `guest-reset` 事件確認請求。

QEMU 的單欄布局覆蓋取決於韌體實際提供的文字模式；未提供時會明確記錄限制，
`40×15` 等尺寸另由主機 fixture 驗證。最新驗收與截圖見 [選單第二版完成紀錄](../history/menu-v2.md)。
