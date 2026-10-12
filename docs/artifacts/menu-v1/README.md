# 選單第一版驗收證據

日期：2026-10-12。[完成紀錄](../../history/menu-v1.md) · [規格](../../plans/menu-v1.md)

| 檔案 | 證明的內容 |
|---|---|
| [menu-test.log](menu-test.log) | 主代理獨立執行 make test-menu 的完整結果 |
| [gop-regression.log](gop-regression.log) | make test、test-host 與 test-gop 回歸結果 |
| [host-gcc-sanitizer.log](host-gcc-sanitizer.log) | GCC ASan／UBSan／LeakSanitizer、退出碼與來源雜湊 |
| [host-clang-sanitizer.log](host-clang-sanitizer.log) | Clang ASan／UBSan／LeakSanitizer、退出碼與來源雜湊 |
| [sources.sha256](sources.sha256)、[base-commit.txt](base-commit.txt) | 本次工作目錄來源與基準 commit 識別 |
| [main.png](main.png)、[device.png](device.png)、[reset.png](reset.png) | 原始 QMP 截圖的無改動 PNG 副本，已視檢 |
| [probe-serial.log](probe-serial.log) | 實際 console／GOP 恢復與後續 runtime_fini |
| [reset-qmp-events.json](reset-qmp-events.json) | 確認 Restart 後的真實 guest-reset 事件 |
| [watchdog-before-events.json](watchdog-before-events.json) | 修正前的 2 秒 watchdog 在無按鍵時觸發 guest-reset |
| [watchdog-after-events.json](watchdog-after-events.json) | 修正後閒置及 Esc 返回期間沒有 guest-reset |
| [watchdog-probe-station.cpp](watchdog-probe-station.cpp)、[watchdog-check.py](watchdog-check.py) | 獨立 reviewer 的測試入口及存活／返回斷言 |

Watchdog 測試入口只用於 `/tmp` 獨立驗證；正式 station.cpp 沒有主動啟用 watchdog。
事件檔本身不能證明等待長度，需與 reviewer 的 Python 四秒等待、存活與返回斷言一起解讀。
Script 保存原始本機路徑供追蹤，未宣稱可直接從 artifacts 目錄執行。

完整來源建置、ESP／VARS、serial、PPM 與其餘畫面保留在完成紀錄列出的 `/tmp` 目錄。
單欄實際韌體畫面未驗證，請見完成紀錄的限制。
