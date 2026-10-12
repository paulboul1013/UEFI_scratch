# GOP 第二版核定實作計畫（事前文件）

日期：2026-10-12。此頁保存實作前的核定計畫；下方勾選項保留當時狀態。

實作與驗收已完成，結果見 [第二版完成紀錄](gop-v2.md)。已依官方欄位順序更正 FreePool offset：原計畫誤寫 64，正確為 72。

專案：`/home/paulboul/UEFI_scratch`。程式基準：`main`，HEAD `6b74ffc8c295dec29ddd09ff0e06c5b2965f80c0`。
需求來源：第二版交接（原始文件：`/tmp/uefi-gop-v2-handoff.md`）。本文件是下一階段的實作依據。
本次授權範圍是制定計畫。實作、commit 與 push 依後續使用者指示進行。

## 1. 目標與完成條件

列舉單一 GOP 的可用模式，優先使用 `800×600`，以 `Blt()` 顯示三色塊。沒有可用目標時使用原模式。按鍵或遇到後續錯誤時，先恢復原模式，再返回 `_start()` 執行既有 C++ runtime 清理。

第二版完成必須同時符合：

- 每個成功查詢取得的非空 buffer，都在該輪結束前呼叫一次 `FreePool()`；正常流程沒有未釋放的查詢資料。
- 主機測試證明模式選擇、實際呼叫順序、失敗清理與最終 `EFI_STATUS` 符合本文件。
- QEMU 截圖確實為 `800×600`，三色塊完整且等待期間穩定；獨立 guest probe 在應用程式返回後、韌體接手前驗證原模式已恢復。
- 乾淨來源的建置、ABI、PE 與 entry point 檢查通過；文件描述實際完成的行為。

範圍維持 `LocateProtocol()` 取得的單一 GOP 與 BLT 繪圖。直接存取 framebuffer、多顯示裝置、OS 載入、`ExitBootServices()` 與 UEFI Runtime Services 留待後續階段。

## 2. 已確認的程式與環境

| 檔案 | 現況 | 第二版工作 |
|---|---|---|
| `uefi.hpp` | 已有 GOP、QueryMode、SetMode 與 ABI 檢查；FreePool 是佔位型別；文字輸出結構只到 OutputString | 補 FreePool 與 ClearScreen 可呼叫型別及 offset 檢查 |
| `main.cpp` | 讀取目前模式、輸出解析度、畫 RGB、等待按鍵；多條 early return | 加入列舉、選擇、切換與統一恢復出口 |
| `startup.cpp` | `_start → runtime_init → efi_main → runtime_fini`，保留 efi_main 的狀態 | 維持此生命週期，測試恢復發生在 runtime_fini 前 |
| `runtime.cpp` / `runtime.hpp` | 最小 C++ 初始化與 atexit 清理 | 沿用現有實作 |
| `tests/gop_host.cpp` | 包含真正 main.cpp，假韌體檢查 BLT 與基礎錯誤；尚無查詢配置、釋放或切換模型 | 擴充狀態模型、事件紀錄與錯誤注入 |
| `tests/gop_qemu.py` | 乾淨建置、獨立 ESP/VARS、serial/QMP、PPM 像素檢查及無 VGA 案例 | 保留現有檢查，加入目標解析度與返回前恢復驗證 |
| `README.md` / `docs/` | 工作區已有文件拆分；README 有修改，docs 尚未追蹤 | 依目前分類更新第二版文件，保留第一版歷史 |

未找到適用的 `AGENTS.md`。本次確認 MinGW、Linux g++、QEMU 與 OVMF 4M 檔案均存在。
不依賴本機舊 `.o`、ESP 或 EFI 映像作驗收。

本次只重新驗證第一版，結果如下：

- `g++ -std=c++17 -fshort-wchar -Wall -Wextra tests/gop_host.cpp -o /tmp/uefi-gop-v2-plan-host`，接著執行該 binary：通過。
- `python3 tests/gop_qemu.py`：乾淨 `make test`、`1280×800` 三色塊與鍵盤返回、無 VGA 的 `EFI_NOT_FOUND` 均通過。
- 新證據目錄：[本次第一版基準驗證](/tmp/uefi-gop-qemu-qhqkjf0j/)。這不是第二版驗收結果，且 `/tmp` 可能被清除。

## 3. 規格依據與設計界線

以下為規格事實：模式編號範圍是 `0..MaxMode-1`；QueryMode 回傳由被呼叫端配置的資料與其大小；目前的 `Mode/Info` 由韌體持有。SetMode 成功會更新模式並清黑可見畫面。FreePool 用來釋放 pool 配置。文字 ClearScreen 會清除畫面並把文字游標移至 `(0,0)`。
依據：[UEFI 2.11 GOP](https://uefi.org/specs/UEFI/2.11/12_Protocols_Console_Support.html#graphics-output-protocol)、[FreePool](https://uefi.org/specs/UEFI/2.11/07_Services_Boot_Services.html#efi-boot-services-freepool)、[文字 ClearScreen](https://uefi.org/specs/UEFI/2.11/12_Protocols_Console_Support.html#efi-simple-text-output-protocol-clearscreen)。

EDK II 的 GraphicsConsole 另外保存文字模式的 GOP 尺寸與文字起點。ClearScreen 使用保存的尺寸，文字 SetMode 也可能呼叫 GOP SetMode。直接切換 GOP 不等於同步文字 console。
依據：[GraphicsConsole 實作](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Universal/Console/GraphicsConsoleDxe/GraphicsConsole.c)。這是上游實作觀察，不代表已確認本機 OVMF 與 master 完全相同。

因此，本計畫採用「在原模式完成列舉並清屏，再切換 GOP、輸出短摘要、畫圖」的順序。切換後不呼叫文字 Reset、SetMode 或 ClearScreen。支援範圍先以現有 QEMU/OVMF 組態驗收；其他韌體的文字配置相容性不能單靠此測試推論。

`LocateProtocol`、`FreePool`、`WaitForEvent` 是 Boot Services。`QueryMode`、`SetMode`、`Blt` 是 GOP Protocol 方法。`runtime_fini` 是本專案的 C++ 清理，與 UEFI Runtime Services 不同。

## 4. 功能決策

### 4.1 原模式與有效資訊

在第一個 console 輸出之前取得 GOP，並保存原模式編號、寬、高與像素格式的純量值。不要保存跨 SetMode 使用的 `Mode*` 或 `Info*`。
若 LocateProtocol 失敗，仍輸出既有建構子結果與 Hello 訊息，再報告 LocateProtocol 錯誤；沒有 GOP 就不進入模式流程。
成功取得有效 GOP 時，保留初始 `[GOP] FOUND` 訊息。原模式已保存後的文字失敗也必須通過恢復出口。

目前模式必須有非空 GOP、Mode、Info，`SizeOfInfo >= sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION)`、`MaxMode > 0`、`Mode < MaxMode`、非零寬高、`Version == 0` 與有效的 PixelFormat。
非法結構、尺寸或解析度回傳 `EFI_DEVICE_ERROR`；尚不支援的 Version/PixelFormat 回傳 `EFI_UNSUPPORTED`。沒有可靠原模式時，不嘗試切換或恢復。

預先檢查 QueryMode、SetMode、Blt、FreePool、ClearScreen 函式指標。必要方法為空時回傳 `EFI_UNSUPPORTED`，不進行查詢配置。
所有成功 SetMode 後，重新從 `gop->Mode` 取得並驗證資訊。不要以 QueryMode 的候選資訊當作目前模式。

### 4.2 列舉、所有權與釋放

保存初始 MaxMode，依編號遞增查詢。每次先把 `Info = nullptr`、`SizeOfInfo = 0`，每個編號最多查詢一次。

| 結果 | 資料處理 | 後續行為 |
|---|---|---|
| QueryMode 成功且資料有效 | 複製編號、寬高、格式等所需純量，立即 FreePool，再輸出該行 | 納入模式選擇 |
| 成功但 Info 為空 | 不讀取，不 FreePool 空指標 | 警告此模式無效，繼續列舉 |
| 成功但大小過短、Version 不支援、零解析度或格式非法 | 依大小檢查後才讀欄位；非空配置仍 FreePool 一次 | 警告並略過此模式 |
| QueryMode 失敗 | 記錄編號與原始 EFI_STATUS；不讀取或釋放失敗時的 OUT 指標 | 警告並繼續；不假定失敗的 OUT 資料有所有權 |
| FreePool 失敗 | 記錄編號與原始 EFI_STATUS，不重複釋放 | 停止列舉，經恢復出口返回錯誤 |
| 模式文字輸出失敗 | 該輪資料已先釋放 | 停止列舉，經恢復出口返回輸出狀態 |

呼叫 FreePool 的對象只有成功 QueryMode 交付的非空配置，絕不釋放 `gop->Mode`、`gop->Mode->Info` 或 stack buffer。
查詢配置一次只保留一份，不建立 mode array，也不引入標準函式庫。若韌體拒絕 FreePool，只能保證已嘗試一次並回報錯誤，不能宣稱記憶體已釋放。

模式行採固定格式：`[GOP] MODE <n> <w>x<h> format=<RGB|BGR|BITMASK|BLT_ONLY>`。無效與失敗模式也輸出各自編號。
可略過的失敗使用 `[GOP] WARN`，包含操作、模式編號與十六進位狀態；致命失敗使用 `[GOP] ERROR`。所有格式使用有固定容量的 stack buffer，依最長固定字串及 UINT32/UINTN 的最大位數核對容量。
不輸出 framebuffer 位址、bitmask 細節或每行 stride；這些不影響本版 BLT 選擇。

### 4.3 模式選擇

1. 原模式已是有效的 `800×600`：優先沿用原模式，即使其他同解析度模式編號較小。
2. 否則，選擇成功查詢且有效的 `800×600` 中最小編號。
3. 沒有有效目標：選擇原模式。完整列舉時記錄 `target-not-found`；曾有查詢錯誤或無效模式時記錄 `enumeration-incomplete`，不要宣稱裝置一定不支援目標。

不依像素格式排序。RGB、BGR、BITMASK 與 BLT_ONLY 都可用，因為本版只呼叫 Blt。BLT_ONLY 不要求 framebuffer 位址或大小，也不因 stride 為零而拒絕。
只有實際目前編號與選定編號不同時才呼叫 SetMode；原模式已是目標的正常情況不呼叫 SetMode。

SetMode 失敗回傳該狀態，並嘗試恢復原模式。此處不改成成功 fallback，以免掩蓋裝置錯誤，也不假定失敗一定沒有改動顯示狀態。
SetMode 回傳成功，但重新讀取的編號、解析度或格式不符選擇結果，視為 `EFI_DEVICE_ERROR`，先恢復再返回。

### 4.4 畫面與 console 順序

正常順序如下：

```text
取得 GOP、保存原模式 → 建構子/Hello → 原模式摘要與逐模式列舉
→ 在原模式呼叫 ConOut.ClearScreen，重設文字游標
→ 必要時 GOP.SetMode(選定模式) → 重新讀取 Mode/Info
→ 短摘要與按鍵提示 → 再檢查目前模式 → 三次 Blt → DRAW_OK
→ 再檢查目前模式 → 等待/讀取按鍵 → 恢復原模式 → 返回 efi_main
→ startup.cpp 的 runtime_fini → 返回韌體
```

列舉文字可能捲動，完整內容由 serial 留存。清屏發生在 GOP 切換前，避免用文字 console 保存的舊尺寸清除新模式。
進入列舉前與清屏前，確認 GOP 仍符合保存的原模式；若先前文字操作已改變模式，直接走 ConsoleMode 錯誤與恢復流程，不在錯誤尺寸上清屏。
原模式摘要使用 `[GOP] ORIGINAL mode=<n> <w>x<h>`，避免既有解析度 regex 把原模式誤認為繪圖模式。

最終畫面只輸出下列四行；前三行結尾為 CRLF，DRAW_OK 不換行：

```text
[GOP] ACTIVE mode=<n> format=<label>
[GOP] RESOLUTION <w>x<h>
Press any key to exit...
[GOP] DRAW_OK
```

保留既有三色塊位置、尺寸計算與 BLT buffer 順序。等待期間沒有新的程式文字輸出。
在 ClearScreen、摘要、DRAW_OK 等 console 操作後重新確認 GOP 狀態。若文字操作使編號、寬高或格式偏離預期，回報 `ConsoleMode` / `EFI_DEVICE_ERROR`，經恢復出口返回，不反覆切換。
QEMU 必須人工視檢一次最終文字可讀、沒有被裁切或與色塊重疊；serial 成功不代表文字在畫面上可見。

若現有 OVMF 的文字起點造成裁切，先在任務 3 的檢查點重現並修正文字布局。此風險必須在完成任務 3 前解決；不得降低 `800×600` 或畫面可讀的驗收條件，也不得自行擴成完整文字模式管理功能。

### 4.5 恢復與最終狀態

模式工作放在可返回狀態的 helper；取得可靠原模式後，efi_main 的所有路徑均通過同一個恢復出口。使用 stack 狀態保存原模式與是否嘗試過 SetMode。不要把恢復放到全域 atexit 或無法回傳 EFI_STATUS 的 destructor。

以下情況必須嘗試恢復：曾呼叫 SetMode，包括切換失敗；或目前模式偏離原模式；或原模式已知但目前 Mode/Info 無法驗證。
若沒有切換，且目前狀態仍與原模式相符，則不呼叫恢復 SetMode。

恢復前輸出 `[GOP] RESTORE mode=<original>`，這是呼叫意圖，不是成功證據。即使此文字輸出失敗仍執行恢復。
恢復只呼叫一次 `SetMode(original)`，並重新驗證原編號、寬高與格式。成功後不再輸出程式文字，直接返回，避免在恢復後又操作 console。
恢復呼叫失敗或成功後驗證不符時，記錄 `RestoreMode` 錯誤；不重試、不重新繪圖、不等待第二個按鍵。

| 主操作狀態 | 恢復結果 | 最終 EFI_STATUS |
|---|---|---|
| 成功 | 不需要恢復或恢復成功 | EFI_SUCCESS |
| 失敗 | 不需要恢復或恢復成功 | 原主操作狀態 |
| 成功 | 恢復失敗 | 恢復狀態；成功但驗證不符時為 EFI_DEVICE_ERROR |
| 失敗 | 恢復也失敗 | 保留原主操作狀態；另記錄恢復狀態 |

若恢復前的文字輸出失敗且原主操作成功，將該輸出狀態記為主操作失敗。錯誤報告的 OutputString 若再次失敗，不覆蓋已存在的操作錯誤，也不遞迴報錯。
WaitForEvent、ReadKeyStroke、各次 Blt、ClearScreen 與正常文字輸出的失敗均遵循此政策。維持現有 `status != EFI_SUCCESS` 判定方式。

## 5. 實作任務與依賴

依賴主線：`1 → 2 → 3 → 4 → 5 → 6`。每一步完成後程式都可建置與測試；檢查點先處理失敗，再進入下一步。
所有勾選項目前都未完成，不代表本次已開始實作。

### 任務 1：補齊可呼叫介面與假韌體模型

檔案：`uefi.hpp`、`tests/gop_host.cpp`。範圍：S。依賴：無。

- [ ] 加入 `EFI_FREE_POOL = EFI_STATUS (EFIAPI *)(void*)`，替換原欄位，確認 FreePool offset 為 72；既有 WaitForEvent=96、LocateProtocol=320 等檢查保留。
- [ ] 依官方順序補文字介面至 ClearScreen，前置未用方法可用函式指標佔位；ClearScreen 使用正確可呼叫型別且 offset 為 48，原 Reset/OutputString offset 不變。
- [ ] 假 GOP 有至少原模式、800×600 與另一模式；提供 QueryMode、SetMode、FreePool、ClearScreen 及事件紀錄。SetMode 可替換 Mode 與 Info 物件，釋放資料可在後續存取時被偵測。

驗證：`make test-host` 與乾淨來源 `make test`；保留第一版既有成功與錯誤測試。型別依據另核對 [EDK II UefiSpec.h](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Uefi/UefiSpec.h)。

### 任務 2：完成可釋放的模式列舉

檔案：`main.cpp`、`tests/gop_host.cpp`。範圍：S。依賴：1。

- [ ] 遞增列舉、驗證、複製純量並釋放；確認非空成功 buffer 在文字輸出前已釋放，錯誤 OUT 指標不使用。
- [ ] 完成重複目標、原模式已符合、目標不存在及列舉不完整的選擇政策；本步先繼續以原模式完成既有繪圖流程。
- [ ] 列舉後在原模式清屏，再輸出精簡摘要；查詢失敗是警告，FreePool 或輸出失敗是可返回的錯誤。

驗證：`make test-host`；涵蓋短資料、空資料、較大的有效資料、版本/格式、QueryMode 與 FreePool 失敗及大量模式。確認查詢/釋放數量與順序，而非只比對訊息。

檢查點 A：任務 1–2 後，乾淨建置通過；列舉正常流程配置/釋放平衡；原模式的 RGB 與按鍵流程仍成立。

### 任務 3：接上模式切換與統一恢復出口

檔案：`main.cpp`、`tests/gop_host.cpp`。範圍：S。依賴：2。

- [ ] 依選擇結果啟用 SetMode，重新取得實際 Mode/Info；舊指標被替換的案例仍使用新解析度畫圖。
- [ ] 正常按鍵、切換失敗、切換後資料無效與三次 BLT 失敗都通過恢復出口；先證明主錯誤與恢復錯誤的優先順序。
- [ ] 在現有 QEMU/OVMF 下確認實際 800×600、短摘要可讀、三色塊穩定與按鍵返回；處理 console 尺寸或文字位置問題。

驗證：`make test-host`，再執行現有 QEMU 腳本的 smoke test 與視檢截圖。此時完整返回前 probe 尚待任務 5，不宣稱第二版已驗收完成。

檢查點 B：任務 3 後，目標解析度與 console 相容性已確認；所有已接入的切換路徑都有恢復測試。若 QEMU 顯示問題尚未解決，先修復本步。

### 任務 4：補齊失敗矩陣與啟動流程驗證

檔案：`tests/gop_host.cpp`；若測試揭露缺陷，修正 `main.cpp`。範圍：S。依賴：3。

- [ ] 完成第 6 節的主機測試矩陣，明確檢查未進入等待、恢復次數、狀態及事件先後。
- [ ] 用不同錯誤碼注入主錯誤與恢復錯誤，證明沒有覆蓋；驗證恢復成功後沒有新的程式 console 輸出。
- [ ] 在主機測試以名稱替換方式包含 startup.cpp，將 `_start` 改名為測試入口，stub runtime_init/runtime_fini；驗證所有代表性返回路徑都呼叫一次 fini，且恢復在 fini 前。

驗證：`make test-host`；再以 `-fsanitize=address,undefined -fno-omit-frame-pointer` 建置到新的 `/tmp` binary 並執行。假配置模型即使注入 FreePool 失敗也由 fixture 最後釋放，另外斷言產品程式的釋放嘗試，避免測試自己製造假漏洩。
啟動流程測試驗證呼叫契約；不把 stub 測試描述為已執行真正的 runtime destructor。

### 任務 5：加入獨立的 QEMU 恢復驗證

檔案：`tests/gop_qemu.py`、新增 `tests/gop_qemu_probe.cpp`、`Makefile`。範圍：M。依賴：4。

- [ ] 正式映像的正向測試要求實際選中 800×600；保留完整像素矩形、等待穩定與無 VGA 案例。若此 OVMF 沒有目標模式，明確報告該驗收案例未通過，不能把 fallback 當作 800×600 成功。
- [ ] 新增只供測試的 guest wrapper：以預處理器將真正 main.cpp 的 efi_main 改名為 application helper，再由 wrapper 的 efi_main 呼叫它。wrapper 在呼叫前保存 GOP 純量，呼叫後獨立讀取 GOP，比對原模式、寬高及格式，並確認應用回傳成功。
- [ ] 使用獨立 probe 映像與 ESP/VARS 執行，輸出 `[PROBE] RESTORE_VERIFIED ...` 或明確失敗。Makefile 提供 probe 映像建置入口，沿用既有 freestanding 編譯/連結旗標，只連結 startup、runtime 與 probe wrapper，避免重複 main 定義。

驗證：乾淨來源分別建置正式映像與 probe 映像；兩者都執行實際目標畫面與按鍵流程，probe 再檢查返回後狀態。更新 SOURCES 複製清單，保留每個案例的 serial、QEMU log、PPM 及建置 log。
複製新增 `tests/` 來源時，先建立目的父目錄。Probe build 入口使用獨立 object/EFI 路徑，不取代正常 main.o 或正式 EFI。
正式映像的 `make test` 仍檢查真正 main.o 的 efi_main 符號；probe 使用獨立產物檢查，不套用不相符的 main.o 假設。

Probe 不加入產品測試開關、不修改 startup/runtime、不以韌體選單尺寸判定恢復。它讀取的時間點是 efi_main 已返回、runtime_fini 與韌體尚未接手之間。
若初始模式已是 800×600，該 run 可驗證沿用路徑，但不能替代「不同原模式 → 800×600 → 原模式」案例；既有 OVMF 基準初始為 1280×800，實作時仍須重新確認。

檢查點 C：任務 4–5 後，主機錯誤矩陣、sanitizer、正式 QEMU 畫面及 guest probe 均通過。任一失敗都先修復，不進入完成文件。

### 任務 6：整理第二版文件與完成驗收

檔案：`README.md`、`docs/knowledge/gop.md`、`docs/knowledge/testing.md`、`docs/README.md`、新增 `docs/history/gop-v2.md`。範圍：M。依賴：5。

- [ ] 更新目前操作、模式選擇、錯誤政策、測試入口與實測結果。README 保持簡短，詳細內容放到既有知識分類。
- [ ] 驗收完成後才新增第二版完成紀錄，保留來源、日期與證據；第一版歷史不改成第二版結果。把本計畫所需內容保存到版本庫文件，避免只依賴 `/tmp`。
- [ ] 完成第 7 節檢查並記錄實際產物路徑；保留使用者原有 README/docs 修改，不在此任務自行 commit/push。

驗證：核對文件、測試入口、實際畫面與錯誤政策一致，執行 `git diff --check`。程式未再改動且先前檢查已通過時，不重複整輪測試；有修正則重跑受影響的驗證。

## 6. 主機測試矩陣

每個案例直接呼叫真正應用程式，檢查最終狀態、查詢與釋放紀錄、SetMode 的實際編號及 BLT/鍵盤順序。
測試韌體使用不同的舊/新 Mode、Info 與查詢配置物件，避免所有指標指向同一物件而漏掉 stale pointer 問題。

| 類別 | 必須涵蓋的案例 | 核心斷言 |
|---|---|---|
| 基礎錯誤 | 原有 LocateProtocol 失敗、空介面/Mode/Info、短資料、零寬高、小幾何、缺 Blt | 既有行為仍有明確錯誤；沒有非法查詢/等待 |
| 初始狀態 | MaxMode=0、Mode>=MaxMode、必要方法為空、不支援版本/格式 | 不切換、不配置；有效原模式才有恢復能力 |
| 查詢所有權 | 正常配置、較大結構、短結構、成功空指標、Query 失敗且 OUT 指標為不可用值 | 所需資料讀取前檢查大小；只釋放成功非空配置，每份一次 |
| 查詢錯誤 | 第一個/中間模式查詢失敗、全部查詢失敗、無效版本/格式/解析度 | 警告不中止其他模式；有效原模式可 fallback；不錯稱目標不存在 |
| 模式選擇 | 單一目標、多個目標、原模式已是目標、沒有目標 | 原模式目標優先，否則最小有效編號；無目標沿用原模式 |
| 像素格式 | RGB、BGR、BITMASK、BLT_ONLY；BLT_ONLY 無 framebuffer/stride | BLT buffer 都依 Blue、Green、Red、Reserved 排列，無 framebuffer 存取 |
| 記憶體錯誤 | FreePool 失敗、模式行 OutputString 失敗 | 停止後續模式；已成功配置的資料在輸出前完成釋放嘗試 |
| 切換 | SetMode 成功並替換 Mode/Info；失敗且模式不變；失敗但模式已改變 | 以新 Info 繪圖；切換失敗也嘗試原模式恢復 |
| 切換後無效 | Mode/Info 空、短資料、錯編號、錯解析度 | 不畫圖、不等待，先恢復；保留 EFI_DEVICE_ERROR |
| Console | 大量模式、ClearScreen 失敗、摘要/提示/DRAW_OK 失敗、console 改變 GOP | 清屏在切換前；不使用過時資料；失敗後恢復 |
| 繪圖/鍵盤 | 三次 BLT 分別失敗、WaitForEvent 失敗、ReadKeyStroke 失敗、正常按鍵 | 適當停止後續呼叫；有切換就恢復，DRAW_OK 不假成功 |
| 恢復 | 成功、SetMode 失敗、成功但狀態不符、主操作和恢復同時失敗、恢復提示輸出失敗 | 每次恢復一個 SetMode；檢查原模式；主錯誤優先且兩錯誤均嘗試報告 |
| 生命週期 | 正常、無 GOP、列舉致命錯誤、繪圖/鍵盤錯誤、恢復錯誤 | `_start` 保留 status；runtime_fini 一次且在恢復後 |

配置/釋放計數不是唯一證據：還要確認 FreePool 的精確指標、不可釋放韌體 Info、釋放後不再使用，以及錯誤路徑沒有漏過統一出口。
保留第一版的 UINT32 最大值格式化與小幾何案例；這些 fixture 必須移除 800×600 候選，才能繼續驗證原解析度，而不是被新模式選擇自動替換。

## 7. 最終驗收與風險處理

實作階段完成後執行：

```bash
make test-host
make test
python3 tests/gop_qemu.py
git diff --check
```

其中 QEMU 腳本必須執行乾淨來源的正式映像建置，以及新 probe 映像建置；本機增量 `make test` 不能取代這項檢查。
主機 sanitizer 另在 `/tmp` 執行。若沙箱造成 QEMU `/var/tmp` 寫入或 LeakSanitizer ptrace 限制，依當時工具權限流程升級並保存結果，不歸因為程式失敗，也不把未執行的檢查列為通過。

| 風險 | 驗收或處理方式 |
|---|---|
| Console 保留舊尺寸或起點 | 清屏在切換前；不呼叫文字 SetMode/Reset；任務 3 先跑 QEMU 與視檢可讀性 |
| 韌體選單自行切換解析度 | 用 guest probe 在應用返回後獨立讀取 GOP；選單截圖只證明韌體接手 |
| QueryMode/SetMode 指標失效 | 複製純量、查詢資料即時釋放、SetMode 後重新取 Mode/Info |
| 恢復失敗掩蓋原錯誤 | 各自保存狀態，用不同錯誤碼測試優先順序；錯誤報告不覆寫原狀態 |
| 舊建置產物使結果失真 | 正式與 probe 映像都從乾淨來源建置；每個 VM 使用獨立 ESP/VARS |
| 文件工作區已有變動 | 以現有文件分類為準；保留修改；只在第二版驗收後記錄完成 |

目前不需要使用者補充功能偏好。本文件已選定完整行為與失敗政策；後續取得實作指示即可從任務 1 開始。
之後交接引用本文件與新的驗收證據，清楚標明已完成的任務及尚未完成的項目。
