# Menu v2 驗收證據

日期：2026-10-13。[完成紀錄](../../history/menu-v2.md) · [計畫](../../plans/menu-v2.md)

| 檔案／目錄 | 用途 |
|---|---|
| `final-tests.log` | 主代理完整 make test-menu、test-boot-qemu、test、test-host、test-gop |
| `final-boot-qemu.log` | serial 等待條件修正後的最終 menu-boot 全流程 |
| `sanitizer.log` | GCC／Clang、boot 53 案／menu 132 案、ASan／UBSan／leak |
| `static-build.log` | 強制重建正式 Station／GOP 與 PE／入口／helper 檢查 |
| `source-sha256.json`、`base-commit.txt`、`environment.json` | 最終測試來源、基準與編譯器／韌體環境 |
| `boot-qemu/` | 最終 clean-build log、來源／映像／韌體雜湊與各 VM 命令、初始 ESP 雜湊、serial、QMP、PNG |
| `core-qemu/` | 另一次 core-only 的真正 LoadImage／StartImage 與返回證據 |
| `navigation-qemu/`、`gop-qemu/` | 既有選單與 GOP 回歸證據 |
| `runs.json` | 完整暫存建置、ESP、VARS、PPM 的原始位置 |
| [review-regressions.log](review-regressions.log) | 五個 review probes 的修正前失敗與修正後通過 |
| `menu-before-review.cpp`、`review-regression-driver.cpp` | 重現上述五個問題的歷史實作與 probe；不加入正式建置 |

PNG 是原始 QMP screendump 的無改動轉換，已視檢；raw PPM、EFI 映像及 VARS 留在各暫存目錄。
每個 VM 都使用獨立 ESP 與新的系統模板 VARS；實際缺檔、損壞及測試 child 由該 VM 的 `esp-sha256.json` 識別。
dirty 的「取消 watchdog」需同時解讀 4.5 秒閒置斷言與獨立 watchdog-control 的 QMP guest-reset，事件檔單獨不證明等待時長。

在專案根目錄可重跑 review probes；歷史實作預期五個操作都失敗，最終實作預期都通過：

```bash
g++ -I. -std=c++17 -fshort-wchar -Wall -Wextra -Werror \
  docs/artifacts/menu-v2/review-regression-driver.cpp \
  docs/artifacts/menu-v2/menu-before-review.cpp boot.cpp station.cpp -o /tmp/menu-v2-red
/tmp/menu-v2-red 0  # 另以 1、2、3、4 檢查其餘案例；失敗為預期

g++ -I. -std=c++17 -fshort-wchar -Wall -Wextra -Werror \
  docs/artifacts/menu-v2/review-regression-driver.cpp menu.cpp boot.cpp station.cpp -o /tmp/menu-v2-green
/tmp/menu-v2-green 0  # 另以 1、2、3、4 執行；皆應成功
```
