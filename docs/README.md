# 文件分類索引與整理紀錄

[專案入口](../README.md)

此頁提供主題查詢、已完成實作的歷史索引，以及文件搬移紀錄。

## 分類規則

| 位置 | 用途 | 更新方式 |
|---|---|---|
| [README.md](../README.md) | 專案入口、目前能力與常用指令 | 保持簡短，連結到詳細文件 |
| `docs/knowledge/` | 人類按主題搜尋原理、操作與排錯 | 程式行為改變時更新對應主題 |
| `docs/history/` | 已實作計畫的任務、範圍、commit 與驗收紀錄 | 保留版本與證據來源，不改寫成新版本的結果 |
| `docs/artifacts/` | 已完成實作的核心 log、截圖與來源識別 | 對應歷史驗收紀錄，保留原始結果 |
| `docs/README.md` | 分類、搜尋入口與文件整理紀錄 | 新增、搬移或改名文件時同步更新 |

知識文件描述目前行為。歷史文件描述當時完成的工作，並標明資料來源。
依 commit 回顧整理的紀錄，不標成原始事前計畫。未完成的建議也不列為已完成實作。

## 人類知識查詢

先按問題找文件，再用關鍵字查詢。各知識頁也列出相關原始碼。

| 想查的問題 | 關鍵字 | 文件 |
|---|---|---|
| UEFI 程式如何啟動？專案檔案放在哪裡？ | Firmware、PE Loader、專案結構、執行鏈 | [UEFI 架構與專案導覽](knowledge/architecture.md) |
| 為何不能直接沿用 Linux 的呼叫慣例？ | ABI、System V、Microsoft x64、register、ImageHandle | [ABI 與呼叫慣例](knowledge/abi.md) |
| MinGW 旗標各有什麼用途？ | freestanding、nostdlib、wchar_t、CHAR16、red zone | [編譯器與 Freestanding Environment](knowledge/compiler.md) |
| Object 如何變成 EFI executable？ | linker.ld、PE32+、section、symbol、relocation、entry point | [Linker、PE32+ 與 Entry Point](knowledge/linker.md) |
| 全域建構子與清理何時執行？ | _start、efi_main、init_array、atexit、runtime_fini | [C++ runtime 與生命週期](knowledge/runtime.md) |
| 如何輸出文字並等待鍵盤？ | System Table、ConOut、ConIn、OutputString、WaitForEvent | [System Table、Console 與鍵盤](knowledge/console.md) |
| 如何列舉 GOP、切換模式、恢復與畫色塊？ | QueryMode、FreePool、SetMode、ClearScreen、restore、Blt | [GOP 模式與 BLT 繪圖](knowledge/gop.md) |
| 如何建立 ESP 與啟動虛擬機？ | FAT、BOOTX64.EFI、OVMF、QEMU、VARS、NVRAM | [ESP、OVMF 與 QEMU](knowledge/qemu.md) |
| 如何跑測試、看 log 或檢查 PE？ | make test、test-host、test-gop、serial、QMP、PPM、nm、objdump、GDB | [驗證與 Debug](knowledge/testing.md) |
| 建置或啟動失敗時如何排查？ | multiple definition、undefined reference、init_array、linker warning | [問題排查](knowledge/troubleshooting.md) |

也可在專案根目錄搜尋文件內容：

```bash
rg -n 'GOP|Blt|LocateProtocol' docs/knowledge
rg -n 'atexit|constructor|destructor' docs/knowledge
```

## 歷史已實作計畫

前四份文件依 commits、原 README 與驗收結果整理，是回顧紀錄。第二版另外保存核定的事前計畫與本次完成結果。
第一版及之前的日期是功能 commit 日期；第二版為本次實作與驗收日期。

| 完成日期 | 階段 | 狀態與版本 | 紀錄 |
|---|---|---|---|
| 2026-09-24 | 最小 UEFI 程式、Hello UEFI 與建置流程 | 已完成；`e4c283b` 至 `9067c17` | [最小程式與建置](history/bootstrap.md) |
| 2026-09-24 | 鍵盤事件與返回韌體 | 已完成；`1dd2569` | [鍵盤等待](history/keyboard.md) |
| 2026-10-08 | 最小 C++ runtime 與清理 | 已完成；`2e45776` | [C++ runtime](history/runtime.md) |
| 2026-10-08 | GOP 查詢、解析度與三色塊 | 已完成；`6b74ffc` | [GOP 第一版](history/gop-v1.md) |
| 2026-10-12 | GOP 列舉、800×600、恢復與驗證 | 已完成實作、自動驗收與人工測試 | [GOP 第二版](history/gop-v2.md) · [核定計畫](history/gop-v2-plan.md) |

Commit 是當時程式的依據。文件內連到工作目錄的原始碼，則是目前版本。
歷史驗收結果不能當成每次文件更新後都重新執行的測試結果。

## 尚未實作的方向

這些是原 README 與目前討論留下的候選方向，不代表已核定的實作計畫。

- 直接寫入 framebuffer。
- 多顯示裝置。
- OS 載入與 `ExitBootServices()`。
- UEFI Runtime Services。
- `assert` / `panic`。
- `__cxa_*` helpers。
- QEMU / GDB 深度偵錯流程。現有 `make debug` 已提供啟動入口。

GOP 第二版的核定計畫已保存於 [gop-v2-plan.md](history/gop-v2-plan.md)，完成結果與審查證據見 [gop-v2.md](history/gop-v2.md)。

## 文件整理紀錄

### 2026-10-12：拆分原 README

以 `6b74ffc` 的 README 為基準，將長篇學習筆記拆成主題文件，並建立已完成實作的歷史索引。
README 保留專案介紹、快速操作與文件入口。此次變更只整理文件。

| 原 README 內容 | 新位置 | 分類 |
|---|---|---|
| 目前成果、GOP 介面與預期畫面 | [gop.md](knowledge/gop.md) | 知識 |
| 目前執行鏈、專案結構、Chapter 1、完整資料流 | [architecture.md](knowledge/architecture.md) | 知識 |
| Chapter 2：ABI 與 Calling Convention | [abi.md](knowledge/abi.md) | 知識 |
| Chapter 3：Compiler 與 Freestanding Environment | [compiler.md](knowledge/compiler.md) | 知識 |
| Chapter 4：Linker、PE+、Entry Point；完整 Build 流程 | [linker.md](knowledge/linker.md) | 知識 |
| Chapter 5：`_start` 與 `efi_main()` | [runtime.md](knowledge/runtime.md) | 知識 |
| Chapter 6：System Table、Protocol、Hello UEFI、鍵盤事件 | [console.md](knowledge/console.md) | 知識 |
| 建立 ESP、OVMF / QEMU | [qemu.md](knowledge/qemu.md) | 知識 |
| GOP 測試方式、產物位置、驗證與 Debug 指令 | [testing.md](knowledge/testing.md) | 知識 |
| 曾遇過的錯誤 | [troubleshooting.md](knowledge/troubleshooting.md) | 知識 |
| 已完成的功能與 commit 歷史 | [歷史已實作計畫](#歷史已實作計畫) | 歷史 |
| GOP 第一版驗收結果 | [gop-v1.md](history/gop-v1.md) | 歷史 |
| 尚未實作的項目 | [尚未實作的方向](#尚未實作的方向) | 索引 |

搬移時也依目前程式更正文句：System Table 的使用入口包含 `ConIn` 與 `BootServices`，`Reset` 是宣告欄位，shell 退出碼與 `EFI_STATUS` 分開說明。
建置指令與既有技術內容保留在對應主題。完整拆分前的 README 可由基準 commit 查閱。

### 2026-10-12：GOP 第二版實作與驗收

完成模式列舉、800×600 選擇、切換後驗證與統一恢復出口。更新 GOP、測試與架構知識頁，保留原 README 拆分與第一版歷史。事前計畫與核心測試 log / 截圖另存專案，完整 /tmp 產物路徑列於第二版完成紀錄。
