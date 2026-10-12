# GOP 模式列舉、切換與 BLT 繪圖

[文件索引](../README.md) · 分類：知識

搜尋關鍵字：GOP、QueryMode、FreePool、SetMode、ClearScreen、restore、BLT_ONLY、RGB。
相關原始碼：[uefi.hpp](../../uefi.hpp)、[main.cpp](../../main.cpp)。

## 第二版的正常流程

在第一個文字輸出之前，`LocateProtocol()` 取得單一 GOP，驗證目前模式，保存原編號、寬高與像素格式。之後顯示建構子結果、Hello、原模式與完整列舉。每個有效模式顯示 `[GOP] MODE <n> <w>x<h> format=<label>`；serial 保留捲動前的內容。

模式選擇順序：原模式已是有效 `800×600` 時沿用原模式，否則選最小編號的有效 `800×600`。沒有找到目標時沿用原模式；完整列舉記錄 `target-not-found`，查詢失敗或無效資料造成列舉不完整時記錄 `enumeration-incomplete`。

清屏發生在原模式，之後才按需要呼叫 GOP `SetMode()`。每次成功切換後重新讀取 `gop->Mode/Info`，核對編號、寬高與格式。切換後不呼叫文字 Reset、SetMode 或 ClearScreen；文字 console 可能保存原本的尺寸與起點，因此實際畫面仍需在指定韌體上驗證。

本機 QEMU/OVMF 的原模式為 `mode=0 1280×800`，選中的目標為 `mode=3 800×600`。最終畫面只保留四行：

```text
[GOP] ACTIVE mode=3 format=BGR
[GOP] RESOLUTION 800x600
Press any key to exit...
[GOP] DRAW_OK
```

前三行以 CRLF 結尾，`DRAW_OK` 不換行。三個純紅、純綠、純藍矩形位於下半部；等待期間沒有新的程式文字輸出。按鍵後先恢復原模式，再返回 `_start()` 執行 `runtime_fini()`。

## 介面與資料所有權

`LocateProtocol()`、`FreePool()`、`WaitForEvent()` 是 Boot Services；`QueryMode()`、`SetMode()`、`Blt()` 是 GOP Protocol 方法。`runtime_fini()` 是專案的 C++ 清理，與 UEFI Runtime Services 不同。

`gop->Mode` 與它的 `Info` 由韌體持有，不能釋放。每輪成功 QueryMode 取得的非空資料，先檢查大小與內容、複製純量，再呼叫一次 FreePool，最後才輸出該模式。成功但資料無效時仍釋放非空配置；失敗時不讀取或釋放 OUT 指標。FreePool 失敗只嘗試一次，停止列舉並回報原狀態，不能保證韌體已釋放記憶體。

只保存一輪查詢配置，不建立模式陣列。跨 SetMode 不保留舊 Mode、Info 或 QueryMode buffer 指標。

有效資訊需有完整結構、MaxMode 大於零、編號在範圍內、非零寬高、Version=0 與受支援的格式。RGB、BGR、BITMASK 與 BLT_ONLY 都可繪圖；BLT_ONLY 不要求 framebuffer 或 stride。BLT buffer 的 byte 順序固定為 Blue、Green、Red、Reserved。

## 錯誤與恢復

查詢錯誤及無效候選以 `[GOP] WARN` 記錄操作、編號與十六進位狀態，繼續列舉。FreePool、文字、清屏、切換、BLT 或鍵盤失敗會停止主流程。Console 操作後再次核對 GOP；偏離預期時回傳 ConsoleMode / EFI_DEVICE_ERROR。

只要嘗試過 SetMode、目前狀態偏離原模式，或目前 Mode/Info 無法驗證，就進入統一恢復出口。`[GOP] RESTORE mode=<n>` 是恢復意圖；程式只嘗試一次 SetMode(original)，並再次驗證純量。成功恢復後不再輸出程式文字。

| 主流程 | 恢復 | 最終 EFI_STATUS |
|---|---|---|
| 成功 | 不需要或成功 | EFI_SUCCESS |
| 失敗 | 不需要或成功 | 主流程錯誤 |
| 成功 | 失敗或驗證不符 | 恢復錯誤；驗證不符為 EFI_DEVICE_ERROR |
| 失敗 | 也失敗 | 保留主流程錯誤，另外嘗試記錄恢復錯誤 |

恢復提示的文字失敗仍執行恢復；主流程原本成功時，該文字狀態成為主錯誤。錯誤報告本身失敗不覆寫既有錯誤，也不遞迴報錯。

本版以單一 GOP 與 BLT 為範圍。直接 framebuffer、多顯示裝置、OS 載入、ExitBootServices 與 UEFI Runtime Services 留待後續。

## 規格與驗收入口

型別與 offset 以 [uefi.hpp](../../uefi.hpp) 的 static_assert 為準。FreePool 的 x64 offset 是 **72**，AllocatePool 才是 64；ClearScreen 是 48。
規格來源：[UEFI 2.11 GOP](https://uefi.org/specs/UEFI/2.11/12_Protocols_Console_Support.html#graphics-output-protocol)、[FreePool](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html#efi-boot-services-freepool)、[ClearScreen](https://uefi.org/specs/UEFI/2.11/12_Protocols_Console_Support.html#efi-simple-text-output-protocol-clearscreen)、[EDK II 型別](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Uefi/UefiSpec.h)。

驗證入口與證據見 [測試文件](testing.md)、[第二版完成紀錄](../history/gop-v2.md)；第一版紀錄保留在 [gop-v1.md](../history/gop-v1.md)。
