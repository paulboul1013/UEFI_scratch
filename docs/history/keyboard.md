# 鍵盤等待與返回韌體：已實作計畫紀錄

[文件索引](../README.md) · 分類：已完成實作

狀態：已完成。時間：2026-09-24。

紀錄來源：[1dd2569](https://github.com/paulboul1013/UEFI_scratch/commit/1dd25690f338906a4d18a59b2ff9b10fd19a56d0) 與目前程式。此文件是依 commit 整理的回顧紀錄。

## 目標

顯示文字後等待按鍵，再返回韌體。使用 UEFI 鍵盤事件取代原先的 `hlt` 無限迴圈。

## 已完成任務

| 任務 | 實作結果 |
|---|---|
| 宣告文字輸入介面 | 加入 `EFI_INPUT_KEY`、`ReadKeyStroke` 與 `WaitForKey` |
| 宣告 Boot Service | 加入 `EFI_EVENT` 與 `WaitForEvent` 的正確函式指標 |
| 保持 ABI layout | 以 `static_assert` 檢查鍵盤事件與 Boot Services 欄位位置 |
| 等待並讀取按鍵 | 等待單一 `WaitForKey` 事件，再呼叫 `ReadKeyStroke()` |
| 檢查狀態並返回 | 文字輸出、等待或讀取失敗時返回狀態，成功時返回 `EFI_SUCCESS` |

對應程式：[uefi.hpp](../../uefi.hpp)、[main.cpp](../../main.cpp)。

## 驗收與目前的回歸檢查

Commit 顯示程式在按鍵後可離開 `efi_main()`，也檢查各韌體呼叫的狀態。

後續新增的 [GOP smoke test](../../tests/gop_qemu.py) 會等待程式畫面、送出按鍵，再檢查韌體選單的 serial 訊息與截圖。主機測試也檢查成功流程進入等待與讀取按鍵。

這些自動化測試是 GOP 第一版新增的入口。操作方式見 [驗證文件](../knowledge/testing.md)。鍵盤事件原理見 [Console 與鍵盤事件](../knowledge/console.md)。
