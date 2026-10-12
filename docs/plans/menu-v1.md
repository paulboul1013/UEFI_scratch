# UEFI 中介站第一版選單：規格與實作計畫

日期：2026-10-12。狀態：已完成實作、多 agent review 與交叉驗證。

需求：參考 OVMF 基礎介面，規劃本 repo 的選單，並說明每個選項的用途。
本文件保留事前規格與工作順序；完成結果、review 與證據見 [實作紀錄](../history/menu-v1.md)。

## 1. 目標與暫定決策

建立可用鍵盤操作的 UEFI 主選單，作為未來開機平台的入口。
使用 OVMF 的上方資訊、左側選單、右側說明及底部按鍵提示版型。
先完成選單、資訊頁與返回流程，再接入開機載入與設定修改。

目前採用以下暫定決策：

- 螢幕顯示英文；本規格與選項解說使用繁體中文。語言偏好已詢問使用者，尚未收到答覆。
- 使用 `ConOut` 的文字輸出與目前文字模式，支援至少 `80×25` 的完整版型。
- 使用現有 `ConIn`、`WaitForKey` 與 `WaitForEvent`，持續處理鍵盤事件。
- 進入互動等待前停用 boot watchdog，避免等待期間因逾時重啟。此項由實作 review 補入。
- 主選單沿用目前文字模式，不直接切換 GOP，也不固定要求 `800×600`。
- 新增獨立的選單 EFI 映像與 ESP，沿用 `startup.cpp` 和 `runtime.cpp`。
- 保留目前的 GOP 示範映像及驗收入口。後續可再把選單設為預設啟動程式。
- 選項的名稱參考 OVMF，第一版的實際功能以本文件為準。

選單不是 OVMF HII 表單的直接移植。第一版不需要建置 EDK II。
若改成繁體中文顯示，需另加入字型與文字繪製工作，並更新此計畫。

## 2. OVMF 參考與每個選項的意思

本 repo 保存的 OVMF 返回畫面包含 Select Language、Device Manager、Boot Manager、Boot Maintenance Manager、Continue 與 Reset。
下表分開描述 OVMF 的用途及本平台第一版的行為。

| 選項 | 中文意思與用途 | 第一版進入後的行為 |
|---|---|---|
| Select Language | 選擇介面語言。它影響畫面文字，並非程式語言或 OS 語言。 | 顯示 English 為目前語言。繁體中文列為後續能力；不改 OVMF 的 PlatformLang。 |
| Device Manager | 裝置管理。OVMF 會列出可設定的驅動／裝置表單。 | 顯示本平台的韌體與顯示資訊，先提供唯讀頁面；不宣稱已完成完整 HII 裝置設定。 |
| Boot Manager | 開機管理。手動選擇這一次要啟動的開機項目。 | 顯示尚未加入開機項目的說明。後續接入 EFI loader 選擇與啟動。 |
| Boot Maintenance Manager | 開機設定維護。管理新增／刪除項目、順序及相關開機設定。 | 顯示 Default Boot Entry: Not configured 等平台設定摘要；修改與保存功能列為後續。 |
| Continue | 繼續開機。OVMF 會繼續韌體的開機流程；本平台未來會啟動自己設定的預設項目。 | 顯示 No default boot entry configured。維持在平台內，不假裝已啟動 OS。 |
| Reset | 重新啟動機器。這不是恢復出廠設定，也不是清除 NVRAM。 | 顯示 Cancel / Restart 確認頁，預設選 Cancel。確認 Restart 後呼叫冷重啟。 |
| Return to Firmware | 本平台新增的返回選項。結束目前 EFI application，將控制權還給呼叫者。 | 恢復修改過的 console 設定，返回 `_start()` 執行既有 C++ 清理。 |

Boot Manager 決定「這次啟動哪個項目」。Boot Maintenance Manager 決定「保存哪些項目與順序」。
Continue 使用預設項目；Return to Firmware 則結束本平台。兩者必須分開實作。

返回後的畫面由韌體開機政策決定。本 repo 的既有紀錄顯示 OVMF 主選單，但不把這項觀察當成所有韌體的保證。

### 2.1 每個項目的右側說明

右側說明隨反白項目更新。英文草稿如下：

| 選項 | 右側說明草稿 |
|---|---|
| Select Language | Select the language used by this application. English is available in this version. |
| Device Manager | View firmware and graphics information. Device configuration will be added later. |
| Boot Manager | Choose an entry to boot now. Boot entry loading is not available yet. |
| Boot Maintenance Manager | View boot settings. Editing boot entries and boot order will be added later. |
| Continue | Boot the default entry. No default entry is configured in this version. |
| Reset | Restart the machine. A confirmation page opens before restart. |
| Return to Firmware | Exit this application and return control to the UEFI caller. |

未接上執行功能的項目仍可反白並開啟說明頁，方便理解其用途。
它們會明確顯示 Not available yet，不顯示虛假的開機成功訊息。

## 3. 主畫面配置

以下使用文字格示意。尖括號中的內容必須來自實際查詢，不是寫死的測試值。

```text
UEFI Station 0.1
Firmware: <reported vendor>           UEFI: <reported revision>
Text: <columns>x<rows>                Graphics: <current mode>
----------------------------------------------------------------------------
> Select Language                    | Select the language used
  Device Manager                     | by this application.
  Boot Manager                       |
  Boot Maintenance Manager           | English is available
  Continue                           | in this version.
  Reset                              |
  Return to Firmware                 |
----------------------------------------------------------------------------
Up/Down: Select    Enter: Open    Esc: Back / Return
```

| 區域 | 規劃 |
|---|---|
| 上方資訊 | 平台名稱、韌體廠商、UEFI revision、文字尺寸與圖形模式摘要 |
| 左側選單 | 七個項目；初始反白 Select Language |
| 右側說明 | 說明目前項目的用途及是否可執行 |
| 底部提示 | 列出目前頁面有效的按鍵 |

配色參考 OVMF：淺灰底、一般黑字、標題及說明藍字、目前選項黑底白字。
只在狀態改變時重畫。長字串按區域寬度截斷或換行。
避免寫入最後一格觸發捲動。所有游標位置都需落在有效文字範圍內。

若有效文字模式小於 `80×25`，改用單欄布局。
單欄需至少 `40×15`，說明改放選單下方並截斷至可用行數。
更小的模式顯示簡短錯誤，再返回韌體；不嘗試在狹小畫面畫完整介面。

## 4. 鍵盤操作與狀態

| 操作 | 行為 |
|---|---|
| 上／下 | 移動目前選項，到第一個／最後一個項目就停止，不循環 |
| Enter | 開啟目前頁面，或執行目前確認操作 |
| Esc：子頁 | 返回主選單，保留原先反白位置 |
| Esc：Reset 確認頁 | 取消重啟，返回主選單 |
| Esc：主選單 | 等同 Return to Firmware |
| 其他按鍵 | 忽略，不意外執行項目 |

Reset 確認頁有 Cancel 和 Restart 兩個選項，使用上下鍵與 Enter。
預設 Cancel，讓 Enter 不會直接重啟。

核心狀態為 `FrontPage`、`LanguagePage`、`DevicePage`、`BootPage`、`MaintenancePage`、`ContinueInfo`、`ResetConfirm` 及 `ExitRequested`。
保存目前頁面、主選單反白項目與確認選項，不使用遞迴呼叫堆疊維持頁面。

輸入流程沿用 `WaitForEvent → ReadKeyStroke`。
若讀取回報 `EFI_NOT_READY`，回到等待；其他輸入失敗顯示錯誤並經清理出口返回。

## 5. 第一版資料與行為界線

### Device Manager 資訊頁

- 從 System Table 讀取 FirmwareVendor、FirmwareRevision 及 Hdr.Revision。
- 清楚區分 UEFI 規格 revision 與韌體廠商 revision，不把主機套件版本混入畫面。
- 從文字 console 取得目前模式的 columns / rows。
- GOP 可用時，顯示目前模式編號、解析度及像素格式。
- GOP 不可用時顯示 Graphics unavailable，文字選單仍可操作。
- CPU 型號、RAM、PCI、磁碟與完整裝置設定留待後續，不使用示意值冒充查詢結果。

### 尚未接上的開機功能

第一版不讀寫 `BootOrder`、`BootNext` 或 `Boot####`。
不呼叫 `LoadImage`、`StartImage` 或 `ExitBootServices`。
後續接入 EFI loader 時，Boot Manager 選擇一次性項目，Continue 使用平台預設項目。
需避免把本平台本身選成預設載入項目而反覆載入。

### 返回與重新啟動

進入 UI 前保存 console 的屬性、游標位置與可見狀態。
正常返回或錯誤返回時，嘗試恢復有更動的設定，保留主要錯誤狀態。
EnableCursor 不支援時可保留韌體原狀；QueryMode、SetAttribute 與定位輸出能力則需有效。
UI 不切換 GOP 模式；目前 GOP 示範程式的恢復邏輯維持獨立。

實作 review 補充：先呼叫 `SetWatchdogTimer(0, 0, 0, nullptr)` 停用開機 watchdog。
`EFI_UNSUPPORTED` 表示系統沒有 watchdog，可繼續等待；方法缺少或其他失敗則顯示短訊息並返回。

Reset 使用 `RuntimeServices->ResetSystem(EfiResetCold, ...)`。
方法缺少時顯示 Restart unavailable；方法意外返回時顯示錯誤，不回報重啟成功。
重新啟動後不會走正常 runtime 清理；返回韌體時仍走既有 `_start()` 清理流程。

## 6. 程式結構與風格

建議新增以下檔案，保留目前 GOP 程式作為可獨立驗收的示範：

```text
station.cpp                  選單映像的 efi_main 入口
menu.hpp                     頁面、選項及 menu_run 介面
menu.cpp                     顯示、輸入、資訊頁與確認流程
uefi.hpp                     共用 UEFI 型別與正確函式指標
startup.cpp / runtime.cpp    共用的初始化與清理
tests/menu_host.cpp          真正 menu 程式的假韌體驗證
tests/menu_qemu.py           乾淨來源的 QEMU 操作及截圖
docs/plans/menu-v1.md         本規格與任務
docs/knowledge/menu.md       未來實作完成後的操作說明
```

選單映像與原 GOP 映像分開連結，各自只包含一個 `efi_main`。
選單產物使用 `output/menu/BOOTX64.EFI`、`output/menu/esp/EFI/BOOT/BOOTX64.EFI`。
執行時使用 `output/menu/OVMF_VARS_4M.fd`，由系統模板建立。
這使選單實作可獨立驗收，也保留既有 GOP 回歸入口。

沿用 MinGW、freestanding、Microsoft x64 ABI、16-bit wchar_t 與既有 runtime。
不引入例外、RTTI 或標準函式庫依賴。函式命名沿用 snake_case。
例如既有程式使用下列風格：

```cpp
static EFI_STATUS write_text(EFI_SYSTEM_TABLE *table, const CHAR16 *text)
{
    return table->ConOut->OutputString(table->ConOut, const_cast<CHAR16 *>(text));
}
```

新增 `QueryMode`、`SetAttribute`、`SetCursorPosition`、`EnableCursor` 與文字 Mode 的正確宣告。
其中 `QueryMode` 是文字 console 方法，需與 GOP 的 QueryMode 分清楚。
宣告 Runtime Services 到 ResetSystem 所需欄位，加入 x64 offset 驗證。

## 7. 建置與驗證命令

既有命令目前可用：

```bash
make test
make test-host
make test-gop
```

本次實作已新增以下入口：

```bash
make build-menu       # 建置獨立選單 EFI 映像與 ESP
make run-menu         # 在 QEMU / OVMF 執行選單
make test-menu-static # 檢查映像格式、入口與 ESP 一致性
make test-menu-host   # 驗證選單、輸入與恢復流程
make test-menu-qemu   # 從乾淨來源檢查實際 UI
make test-menu        # 執行上述選單驗收
```

## 8. 依序實作的任務

### 任務 1：補齊文字 console 宣告

範圍：S。相依：無。預計檔案：`uefi.hpp`、`tests/menu_host.cpp`。

- [x] 正確宣告文字模式、顏色及游標方法，區分 GOP 與文字 QueryMode。
- [x] 加入必要 offset / size 檢查，既有 GOP 假韌體 fixture 仍能編譯。
- [x] 驗證：`make test`、`make test-host`，及 `g++ -std=c++17 -fshort-wchar -Wall -Wextra -Werror -c tests/menu_host.cpp -o /tmp/uefi-menu-abi.o`。

### 任務 2：完成主選單、導航與返回

範圍：M。相依：1。預計檔案：`station.cpp`、`menu.hpp`、`menu.cpp`、`Makefile`、`tests/menu_host.cpp`。

- [x] 獨立映像能顯示七個項目，反白與右側說明同步更新。
- [x] 上下、Enter、Esc 行為符合第 4 節；正常退出恢復 console 狀態並返回。
- [x] 驗證：`make test-menu-static`、`make test-menu-host` 與 QEMU 完整畫面。此 OVMF 沒有單欄模式，單欄由主機 fixture 驗證。

檢查點 A：主選單可操作、返回可驗證，既有 GOP 主機測試仍通過。

### 任務 3：接入 Device Manager 資訊頁

範圍：S。相依：2。預計檔案：`menu.cpp`、`tests/menu_host.cpp`。

- [x] 頁面資料來自 System Table、文字 console 與可用的 GOP。
- [x] GOP 缺少或無效時，清楚標示不可用，仍可返回主選單。
- [x] 驗證：`make test-menu-host`，注入不同模式資料，再檢查 QEMU 實際資訊。

### 任務 4：完成語言、開機與設定說明頁

範圍：S。相依：2。預計檔案：`menu.hpp`、`menu.cpp`、`tests/menu_host.cpp`。

- [x] 語言頁正確顯示目前語言，開機與設定頁有明確功能說明。
- [x] Continue 缺少預設項目時留在 UI；所有說明頁 Esc 返回原選項。
- [x] 驗證：`make test-menu-host`，確認沒有映像載入或韌體變數寫入呼叫。

檢查點 B：各頁資訊與名稱一致，從所有子頁都能返回。

### 任務 5：完成 Reset 確認與動作

範圍：S。相依：2。預計檔案：`uefi.hpp`、`menu.cpp`、`tests/menu_host.cpp`。

- [x] Reset 預設 Cancel；只有明確選擇 Restart 並按 Enter 才重啟。
- [x] 驗證 ResetSystem 型別、欄位順序與狀態處理；缺少方法及意外返回有明確結果。
- [x] 驗證：`make test-menu-host`；QEMU 重啟案例使用獨立 VARS 與 ESP。

### 任務 6：完成 QEMU 驗收與文件

範圍：M。相依：3、4、5。預計檔案：`tests/menu_qemu.py`、`Makefile`、`docs/knowledge/menu.md`、`README.md`、`docs/README.md`。

- [x] 乾淨來源能建置選單、開啟子頁、返回、退出及確認重新啟動。
- [x] 保存主選單、資訊頁與 Reset 確認畫面；重複按鍵時無捲動、裁切或說明殘留。
- [x] 驗證：`make test-menu`、`make test`、`make test-host`、`make test-gop`；視檢截圖並記錄結果。

檢查點 C：選單全流程、console 恢復與既有 GOP 回歸通過，才記錄為已實作。

## 9. 必要測試與主要相依

主機測試使用真正 `menu.cpp` 與假的 UEFI 介面，驗證可觀察行為：

- 導航邊界、Enter、Esc、未知按鍵及連續進出頁面。
- 右側說明是否對應選項，長字串及小模式是否越界。
- 資訊頁使用實際 fixture 值，而非固定測試字串。
- 未完成項目不觸發重啟、映像載入或變數寫入。
- `EFI_NOT_READY` 返回等待，其他錯誤走一致的清理出口。
- 恢復 console 屬性、游標與可見狀態；恢復失敗不覆寫既有主要錯誤。
- Reset 的取消、確認、方法缺失與意外返回。

QEMU 測試驗證主機 fixture 無法證明的部分：畫面實際可讀、反白、重畫、按鍵事件、返回與重啟。
每個 VM 使用獨立 ESP / VARS，不依賴舊的 `.o` 或 `.EFI`。

| 主要相依 | 處理方式 |
|---|---|
| 文字 console 與 GOP 的狀態差異 | 主選單只使用目前文字模式，圖形 demo 維持獨立 |
| 中文字形尚未整合 | 第一版暫定英文，中文介面另納入字型與繪製規格 |
| 文字模式大小與長字串 | 查詢尺寸、限制行寬、驗證所有游標位置 |
| Boot Manager 名稱容易被理解為已可開機 | 畫面明確標示尚未接入載入器 |
| Continue 與返回韌體的混淆 | 使用不同動作與狀態，不共用退出處理 |
| 重啟改變 VM 的執行流程 | 先顯示確認頁，測試使用獨立變數儲存區 |

## 10. 實作規則

- 一律保留 freestanding 建置與既有 runtime 契約。
- 新 UEFI 方法需有正確函式型別；不能直接呼叫 `EFI_GENERIC_FN` 佔位欄位。
- 所有螢幕資料來自實際查詢，對缺少能力提供清楚狀態。
- 若擴大至中文、真實開機載入或寫入設定，先更新對應規格與驗收範圍。
- 不修改系統 OVMF 映像，不覆蓋使用者現有 VARS，不移除既有 GOP 驗收。

## 11. 來源與待定事項

參考本 repo 的 `uefi.hpp`、`main.cpp`、`startup.cpp`、`Makefile`，以及保存的 OVMF 返回畫面。
上游來源用於確認選項含義與介面，並非宣稱本機套件完全等同上游 master。

- [OVMF 主頁選項組合](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Application/UiApp/FrontPageCustomizedUi.c)
- [語言、Continue 與 Reset 字串](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Application/UiApp/FrontPageStrings.uni)
- [裝置管理頁](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Library/DeviceManagerUiLib/DeviceManager.c)
- [開機項目選擇](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Library/BootManagerUiLib/BootManager.c)
- [開機設定維護項目](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Library/BootMaintenanceManagerUiLib/BootMaintenanceManagerStrings.uni)
- [文字 console 與布局](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Protocol/SimpleTextOut.h)
- [鍵盤 scan code](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Protocol/SimpleTextIn.h)

待定：使用者的螢幕語言偏好。若沒有進一步指定，依本文件暫定的英文介面規劃。
