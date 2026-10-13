# Menu v2：Boot Manager 完成紀錄

日期：2026-10-13。[文件索引](../README.md) · [核定計畫](../plans/menu-v2.md) · [驗收證據](../artifacts/menu-v2/README.md)

基準為 menu-v1 的 `84b467fdc766021856d8c7bacc9676a2a4ad197c`。
本次實作與驗收證據隨 menu-v2 提交保存；來源識別使用 artifact 中的 SHA256，不把基準 commit 當成新實作版本。
使用者於 2026-10-13 確認人工測試完成，並授權 commit 與 push；個別手動操作步驟未另行記錄。

## 實作結果

Station 0.2 的 Boot Manager 顯示固定 GOP Demo 項目，Enter 使用目前 Station 所在檔案系統，
透過真正的父 image handle、完整 file device path、LoadImage 與 StartImage 執行正式 GOP 映像。
子程式返回後顯示階段與實際 EFI_STATUS，保留項目並允許再次啟動。

BootLoader 集中來源查詢、路徑限制、自載入防護、application 型別檢查、空 LoadOptions、watchdog 與子映像所有權。
StartImage 返回後先取消 watchdog，再以 TEST_PROTOCOL 確認映像是否仍存活；不重複卸載已由韌體釋放的 application。
無法清理的 handle 保留在 BootLoader，阻擋新的載入及父映像退出。

選單保存呼叫者與每次啟動前的兩層快照。返回時依序恢復 GOP、文字模式、屬性與游標，
重新取得 ConOut／Mode／GOP 資訊，並在重畫後再查模式與尺寸。
失敗時不用舊 geometry 繪圖；有 pending image 時提供 Retry cleanup／Reset，Cancel 返回復原頁。

`build-menu` 配置兩個正式映像；各測試入口與 dirty／error／text／watchdog-hold 子映像使用獨立建置。
靜態檢查驗證 GOP.EFI 等於該次正式 GOP 建置、不同於 Station，以及 ABI、唯一 efi_main、EFI subsystem、入口與無 hosted helpers。

## 任務與證據

| 計畫任務 | 完成內容 | 驗證 |
|---|---|---|
| 1：UEFI ABI | 正確函式型別、Loaded Image、Device Path、GUID、記憶體型別及 offset／size asserts | MinGW、GCC、Clang 與既有 GOP fixture |
| 2：來源與路徑 | 單一 instance／長度／節點限制、ASCII 目標、分段與大小寫自載入拒絕 | 真正 boot.cpp 的 53 案 host suite |
| 3：生命週期 | load error 帶 handle、driver、application 自動釋放、query／unload 失敗、retry、watchdog／錯誤優先 | host 及雙編譯器 sanitizer |
| 4：真實核心 | 正式 GOP 配置與獨立 BootLoader EFI probe | 核心 OVMF load／draw／return marker |
| 5：父 handle | station、menu、host 與 guest observer 同步傳入 image handle | v1 host／QEMU 與 startup runtime 契約 |
| 6：清單 | GOP Demo、真實路徑、邊界、開頁不載入、Esc 保留主頁位置 | 40×15 host 與正式 QEMU |
| 7：整合與恢復 | 雙層快照、模式／尺寸／純值核對、pending 復原與安全出口 | 132 案 menu host suite |
| 8：獨立觀察 | 真正子映像、下一次等待前 observer、short watchdog 正反對照 | 最終 menu-boot QEMU 全流程 |
| 9：交叉驗證 | 三位 subagent review、問題重現及修正、完整回歸、截圖與來源識別 | 本紀錄與 artifact |

## 多 subagent review

三位 agent 分別建置自己的測試，再審查其他人的實作；主代理處理整合與最終獨立重跑。

| Agent | 獨立審查範圍 | 結果 |
|---|---|---|
| menu_tests | boot.hpp／boot.cpp／uefi.hpp 的 ABI、路徑、所有權、watchdog | 無阻擋問題；另建立 UI 修正前／後的回歸案例 |
| boot_loader | menu／station 的恢復、復原、錯誤優先順序與退出屏障 | 找到下列問題，修正後重新 review 與執行 132 案，無剩餘阻擋問題 |
| qemu_harness | host fixture、Makefile、測試可信度、獨立韌體觀察與視覺證據 | 補齊 confirmed Reset、不同 Mode 物件與 serial 完整狀態等待；無剩餘阻擋問題 |

修正項目：

- console setter 回報成功仍可能沒有恢復值：在重畫前比對真正 Attribute／CursorColumn／CursorRow／CursorVisible。
- 復原等待錯誤前若清理已成功，不能用目前 pending=false 遺失原本 StartImage 的主要錯誤。
- 重試清理的後續錯誤不能覆寫第一個 cleanup_status。
- 無法呈現復原頁時，仍可使用的 OutputString 輸出主要及清理狀態，再受控停止。
- pending image 下 ResetSystem 缺少時立即回到復原頁；40×15 的 Reset／主頁提示保留完整 Esc 說明。

前五個可執行 defect probes 包含三個 scalar、清理狀態及主要錯誤案例，
均在保存的修正前 menu 上失敗、在最終 menu 上通過，見 [review-regressions.log](../artifacts/menu-v2/review-regressions.log)。

## 最終驗收

| 驗證 | 結果 |
|---|---|
| 人工測試 | 使用者於 2026-10-13 確認完成 |
| `make test-menu test-boot-qemu test test-host test-gop` | 全部通過；包含 menu-v1 導航／資訊／Reset／返回及原 GOP 回歸 |
| `make test-menu-boot-qemu` | serial 等待條件補強後再跑全流程，來源雜湊與最終檔案一致 |
| Boot host | 53 案，真正 boot 模組、韌體函式表邊界 |
| Menu host | 132 案，真正 menu／boot／station／startup |
| 原 GOP host | 114 案 |
| `make test-menu-sanitize` | GCC、Clang 各通過 boot 53 案與 menu 132 案，ASan／UBSan／LeakSanitizer 無報告 |
| 正式 GOP 子映像 | 真正 Station 連續三輪 800×600 三色塊與返回清單；之後導航、Reset Cancel、返回韌體 |
| Dirty child | 兩輪實際改變文字／GOP／屬性／游標；observer 在下一次等待前核對恢復值；各輪閒置 4.5 秒仍可操作 |
| Watchdog control | 獨立 VM 成功設定 2 秒 timer 後，以 QMP guest-reset 證明真實逾時重啟 |
| Error／無 VGA | 各兩輪真正 entry、返回前 marker、子 runtime_fini、父恢復；無 VGA 使用不依賴 GOP 的文字子程式 |
| Missing／corrupt | 每台 VM 的 ESP 個別移除／破壞 GOP.EFI，各重試兩輪，顯示真實 Load error 並可退出 |
| 截圖 | 主代理與 reviewer 視檢正式三色塊、清單返回、dirty 恢復及載入失敗；文字、反白、路徑與提示清楚 |

啟動 probe 與 observer 都連結真正模組；正式產品沒有 test 開關。
子映像 markers 以每次 Enter 後的 serial 範圍判定，父 runtime_fini 僅在 Station 最終退出後出現。

## 證據與限制

核心 log、來源／映像／韌體／各 VM 初始 ESP 雜湊、完整 QEMU 命令、serial、QMP 事件與 PNG 保存在
[artifacts/menu-v2](../artifacts/menu-v2/README.md)。完整暫存建置、ESP／VARS／PPM 路徑見 `runs.json`。

QEMU FAT backing 需寫入 `/var/tmp`，LeakSanitizer 需存取受沙箱限制的 ptrace；
首次沙箱執行失敗後，使用已核准的沙箱外執行完成驗收。
本機既有輸出曾出現時鐘偏差警告；另以強制重建與獨立乾淨來源建置確認結果，沒有更改系統時間。

- 目前 OVMF 沒有支援所需單欄文字模式；40×15 由 host fixture 覆蓋，沒有宣稱實際韌體驗證。
- Security Violation／driver／清理失敗等負面所有權案例使用假韌體；沒有宣稱建立 Secure Boot 簽章環境。
- console／輸入與清理永久失效時，保留子 handle 並受控停止，屬不可恢復的複合失效；沒有宣稱實機 fault injection。
- 此版未納入完整 Linux／Windows 開機、命令列／initrd、磁碟探索或 NVRAM 設定保存。
