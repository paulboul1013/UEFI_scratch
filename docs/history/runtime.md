# 最小 C++ runtime：已實作計畫紀錄

[文件索引](../README.md) · 分類：已完成實作

狀態：已完成。時間：2026-10-08。

紀錄來源：[2e45776](https://github.com/paulboul1013/UEFI_scratch/commit/2e45776af744e183db6aa95bdb96fc8136e5e602) 與原 README。此文件是依 commit 整理的回顧紀錄。

## 目標

在進入 `efi_main()` 前執行全域建構子。應用程式返回後執行已登記的清理函式，再返回韌體。

## 已完成任務

| 任務 | 實作結果 |
|---|---|
| 初始化 runtime | `runtime_init()` 遍歷 linker 提供的初始化陣列，略過空函式指標 |
| 提供最小 `atexit()` | 保存最多 32 個清理函式，容量不足時回傳失敗 |
| 完成清理 | `runtime_fini()` 以反向登記順序呼叫函式，在呼叫前移除登記 |
| 整合 entry point | `_start()` 執行初始化、`efi_main()` 與清理，保留應用程式的返回狀態 |
| 加入全域物件檢查 | 建構子設定測試值，應用程式輸出建構子執行結果 |
| 整合建置 | Makefile 編譯與連結 `runtime.cpp`，加入相關 header 相依 |

對應程式：[runtime.cpp](../../runtime.cpp)、[runtime.hpp](../../runtime.hpp)、[startup.cpp](../../startup.cpp)、[main.cpp](../../main.cpp)、[Makefile](../../Makefile)。

## 驗收與證據範圍

原 README 記錄最小 runtime 已整合。GOP 第一版的已完成驗收也包含建構子成功與按鍵後返回韌體。

目前 [QEMU 測試](../../tests/gop_qemu.py) 檢查建構子成功訊息與韌體返回。Repository 未保存獨立的 runtime 清理測試，因此不將此 QEMU 結果解讀為每個清理 callback 的獨立驗證。

目前生命週期與 `.init_array` / `atexit()` 的關係見 [runtime 知識文件](../knowledge/runtime.md)。本紀錄不代表已加入完整 C++ 標準函式庫、例外或 RTTI。
