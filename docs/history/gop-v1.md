# GOP 第一版：已實作計畫紀錄

[文件索引](../README.md) · 分類：已完成實作

狀態：已完成並提交。時間：2026-10-08。

基準：[2e45776](https://github.com/paulboul1013/UEFI_scratch/commit/2e45776af744e183db6aa95bdb96fc8136e5e602)。完成：[6b74ffc](https://github.com/paulboul1013/UEFI_scratch/commit/6b74ffc8c295dec29ddd09ff0e06c5b2965f80c0)。

紀錄來源：上述 commit 與原 README 的驗收紀錄。原始計畫曾存於 `/tmp`，本次整理時已不存在。此文件依已完成工作回顧任務，並非原始計畫全文。

## 目標與範圍

取得單一顯示裝置的 GOP，顯示目前解析度，並在畫面下半部畫出紅、綠、藍三個色塊。保留全域物件檢查、文字輸出、等待按鍵與 C++ runtime 清理流程。

本版沿用目前模式。模式列舉、模式切換、直接寫入 framebuffer、多顯示裝置與 OS 載入不在此版本範圍。

## 已完成任務與順序

| 順序 | 任務 | 完成結果 | 檔案 |
|---|---|---|---|
| 1 | 補齊型別與 ABI | 加入 GUID、GOP 結構與函式型別，補齊 `LocateProtocol` 前的 Boot Services 欄位，加入 layout 檢查 | [uefi.hpp](../../uefi.hpp) |
| 2 | 查詢 GOP 與輸出解析度 | 呼叫 `LocateProtocol()`，檢查狀態、介面與模式資訊，以 stack buffer 格式化數值 | [main.cpp](../../main.cpp) |
| 3 | 填滿三色塊 | 計算下半部矩形，以 `EfiBltVideoFill` 填色，逐次檢查狀態 | [main.cpp](../../main.cpp) |
| 4 | 保存驗證入口與文件 | 加入主機錯誤路徑測試與乾淨來源 QEMU 測試，更新 Makefile 與文件 | [tests/gop_host.cpp](../../tests/gop_host.cpp)、[tests/gop_qemu.py](../../tests/gop_qemu.py)、[Makefile](../../Makefile) |

## 已記錄的驗收結果

以下是第一版完成時的結果，不表示本次文件拆分重新執行了 QEMU。

- 乾淨來源的 `make test` 通過，UEFI ABI 檢查、PE 格式與 entry point 檢查通過。
- 主機測試通過成功流程、查詢失敗、無效模式資訊、無效解析度與各次 `Blt()` 失敗的檢查。
- QEMU 顯示建構子成功、`Hello UEFI`、`[GOP] FOUND` 與 `1280×800`。
- 截圖尺寸符合解析度訊息。三個矩形為純紅、純綠、純藍，等待按鍵時保持可見。
- 按鍵後返回韌體選單。該 OVMF 組態將選單解析度切換為 `640×480`。
- `-vga none` 案例回傳 `EFI_NOT_FOUND`，程式立即返回，沒有進入繪圖或按鍵等待。
- `git diff --check` 通過。

無 GOP 案例的訊息：

```text
[GOP] ERROR LocateProtocol status=0x800000000000000E
```

## 重複驗證與閱讀入口

測試指令、產物位置、QEMU 期限與韌體組態限制集中在 [驗證與 Debug](../knowledge/testing.md)。圖形介面、資料所有權與預期畫面見 [GOP 知識文件](../knowledge/gop.md)。

測試每次產生新的 `/tmp/uefi-gop-qemu-*` 目錄。舊暫存紀錄可能已清除，應用現行腳本產生新的證據。
