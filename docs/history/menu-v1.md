# UEFI Station 選單第一版完成紀錄

日期：2026-10-12。狀態：已完成實作、多 agent review 與交叉驗證。

[規格與任務](../plans/menu-v1.md) · [操作說明](../knowledge/menu.md) · [驗收證據](../artifacts/menu-v1/README.md)

## 完成範圍

新增獨立文字選單映像與 ESP，保留既有 GOP 示範與驗收入口。
七個主選項使用鍵盤導航，說明與反白同步更新；各資訊頁可 Esc 返回原選項。
Device Manager 使用實際 System Table、文字尺寸與有效 GOP 資料。
Reset 預設 Cancel，只有確認 Restart 才冷重啟；返回與錯誤出口嘗試恢復 console 並交給既有 runtime 清理。

第一版採英文，開機載入、預設開機項目、設定保存與中文字型尚未實作。
Continue 留在 UI，Return to Firmware 才結束 application。

## 實作補充

- 補齊 Simple Text Output、Runtime Services ResetSystem 與 watchdog 的正確型別及 x64 ABI 檢查。
- `linker.ld` 加入空初始化指標，避免沒有全域建構子的 PE 映像丟棄 `.init_array`；既有 runtime 會略過空指標。
- Review 發現開機 watchdog 會讓閒置選單逾時重啟，加入停用及失敗處理。
- Cross-review 補齊 probe 對 `runtime.hpp` 的直接建置相依。

## 多方審查

| Reviewer | 責任與獨立證據 | 結果 |
|---|---|---|
| menu_host_tests | 先建立行為測試，再檢查 production、QEMU script、observer 與 Makefile；GCC／Clang sanitizer、ABI 編譯及來源雜湊比對 | 93 案通過；提出並已修正 runtime.hpp 相依，無剩餘阻擋問題 |
| menu_qemu_tests | 乾淨來源 QEMU、真實按鍵、畫面、console 恢復、Reset 事件、無 VGA 操作；視檢截圖及 production | 全流程通過，無額外需修正問題 |
| menu_code_review | 獨立檢查 ABI、狀態、界線、錯誤與恢復；在實際 OVMF 注入 2 秒 watchdog 做修正前後對照 | 找到並已修正 watchdog；修正後閒置 4 秒仍可操作及返回 |
| 主代理 | 獨立重跑完整 menu 驗收、GOP 回歸，視檢主頁／資訊／Reset 截圖 | 全部通過 |

## 驗證結果

| 檢查 | 結果 |
|---|---|
| `make test-menu` | 靜態、93 個主機案例、乾淨來源 QEMU 全數通過 |
| `make test-menu-probe` | 獨立 observer 的 PE、入口、符號與 ESP 通過 |
| GCC／Clang ASan + UBSan + LeakSanitizer | 兩套編譯器各通過 93 案；執行前後來源雜湊一致 |
| 文字布局 | Host 驗證 80×25、40×15、79×24、80×15、120×40、長文字與過小模式；QEMU 為 100×31 |
| 真實資訊 | Device Resolution 與 QMP 截圖 1280×800 相符，Text Mode 與主頁一致，進入子頁不改顯示尺寸 |
| 重畫與返回 | 連續導航後文字及像素一致；未知鍵及邊界不重畫；所有子頁返回原選項 |
| 實際 console 恢復 | Observer 確認 attribute=10、cursor=7,3、visible=1、文字／GOP 模式不變，之後才執行 runtime_fini |
| Reset | Cancel 與 Esc 不重啟；Restart 取得 QMP `SHUTDOWN {guest: true, reason: guest-reset}` |
| 無 GOP | 真實 `-vga none` 的 Device 頁顯示 Graphics unavailable，serial 鍵盤仍可導航與返回 |
| `make test`／`make test-host`／`make test-gop` | GOP 靜態、114 個主機案例、乾淨來源畫面與恢復回歸通過 |
| `make -n -W runtime.hpp build-menu-probe` | 包含 probe 重編譯，確認直接相依有效 |

## 證據與限制

主要 log、截圖、QMP 事件及來源 SHA256 保存於 [docs/artifacts/menu-v1](../artifacts/menu-v1/README.md)。
驗收來源以該目錄的 base commit 與工作目錄來源雜湊識別；實作 commit 可由 `git log -- menu.cpp` 查閱。

完整暫存證據：主代理 `/tmp/uefi-menu-qemu-n3mtpczr`；QEMU reviewer `/tmp/uefi-menu-qemu-us0yiqmm`；
watchdog reviewer `/tmp/menu-independent-watchdog-5lcfsncx`；GOP 回歸 `/tmp/uefi-gop-qemu-3qbqr58f`。
`/tmp` 可能清除，持久保存的精選證據請使用 repo 內的 artifacts。

此 OVMF 沒有符合單欄條件的文字模式；40×15 的真實韌體畫面未驗證，由 host fixture 覆蓋。
QEMU Reset 使用 `-no-reboot` 觀察請求，驗證到 guest-reset，不宣稱已測完整第二輪開機。
截圖已由主代理與 QEMU reviewer 視檢；其他實體韌體的字體、模式及返回政策仍需在該機器驗證。

環境為 MinGW GCC 13-posix、Linux GCC 13.3、Clang 18.1、QEMU 8.2.2 與系統 OVMF 4M。
QEMU FAT 暫存檔與 LeakSanitizer 的 sandbox 限制已依權限流程重跑。
GOP 外層 make 曾回報 Makefile 時戳超前 0.65 秒；兩個乾淨來源建置沒有該警告，完整案例通過。
