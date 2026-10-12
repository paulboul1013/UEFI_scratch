# 最小 UEFI 程式與建置流程：已實作計畫紀錄

[文件索引](../README.md) · 分類：已完成實作

狀態：已完成。時間：2026-09-24。

紀錄來源：Git commits 與原 README。此文件回顧已完成的任務，並非保存下來的事前計畫原稿。

## 目標

從 Linux / WSL2 交叉編譯 x86_64 UEFI 程式，產生 PE32+ EFI application，並在 QEMU / OVMF 顯示 `Hello UEFI`。

## 已完成任務

| 任務 | 實作結果 | 對應檔案 |
|---|---|---|
| 建立 UEFI 介面與入口 | 宣告必要型別、System Table 與文字輸出 Protocol | [uefi.hpp](../../uefi.hpp) |
| 分離入口與應用程式 | `_start` 呼叫 `efi_main`，避免重複定義入口 | [startup.cpp](../../startup.cpp)、[main.cpp](../../main.cpp) |
| 產生 EFI image | 使用 MinGW 與自訂 linker script，指定 EFI subsystem 與 entry point | [linker.ld](../../linker.ld) |
| 建立可重複操作的建置流程 | 編譯、連結、複製至 ESP、檢查產物、啟動 QEMU | [Makefile](../../Makefile) |
| 保存學習內容 | 原 README 記錄 ABI、編譯器、linker、ESP 與 OVMF 操作 | [知識索引](../README.md#人類知識查詢) |

## 歷史驗收紀錄

原 README 記錄 `file output/BOOTX64.efi` 輸出 PE32+ EFI application，並在 OVMF Boot Manager 選擇磁碟後看到 `Hello UEFI`。

現行建置與啟動方式見 [ESP / OVMF / QEMU](../knowledge/qemu.md)，產物檢查見 [驗證與 Debug](../knowledge/testing.md)。目前程式也包含後續鍵盤、runtime 與 GOP 功能。

## Commit 索引

| Commit | 內容 |
|---|---|
| [e4c283b](https://github.com/paulboul1013/UEFI_scratch/commit/e4c283baa70be4da1ea6d2b77bf4827b32b4516a) | 最小 UEFI 程式 |
| [e312a3f](https://github.com/paulboul1013/UEFI_scratch/commit/e312a3f693cda96e98a5b9a929a4720490bfc8af) | UEFI sections 整理 |
| [30fc10c](https://github.com/paulboul1013/UEFI_scratch/commit/30fc10cba82db720929ad59f0b4bb1887df3c783) | Hello UEFI 入口與文字輸出 |
| [50f01bf](https://github.com/paulboul1013/UEFI_scratch/commit/50f01bfedffd4496b0c0533e0766bf7ba87df42d) | 建置與啟動 Makefile |
| [9067c17](https://github.com/paulboul1013/UEFI_scratch/commit/9067c178f14dad1cd0b2a1c602b1b6ea58d9fc0a) | 原始學習文件 |

若要查看當時的程式，以各 commit 為準。知識文件描述目前專案，不用來還原舊版。
