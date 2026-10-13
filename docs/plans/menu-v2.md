# Menu v2：Boot Manager 規格與實作計畫

日期：2026-10-13。狀態：實作、多 subagent review／驗收與人工測試完成。

基準：[menu-v1](menu-v1.md)，已提交於 `84b467f`。
需求：讓 UEFI Station 的 Boot Manager 選擇、載入並啟動另一個 EFI 程式。
實作、建置、host／QEMU 驗收及多 subagent review 已完成。
證據見 [完成紀錄](../history/menu-v2.md) 與 [artifacts](../artifacts/menu-v2/README.md)。

## 1. 目標與成功條件

完成以下可重複操作的流程：

```text
QEMU → OVMF → UEFI Station
                    ↓ Enter：Boot Manager
                 選擇 GOP Demo
                    ↓ Enter：Start
              LoadImage → StartImage
                    ↓ 子程式返回
              恢復模式與選單畫面
                    ↓
              Boot Manager 清單
```

驗收須證明真正的子映像執行，且返回後仍可再次啟動、導航、Reset 或返回韌體。
載入失敗須留在可操作的選單，顯示失敗階段與實際 EFI_STATUS。
韌體資訊、文字大小及圖形資訊均維持實際查詢，延續 v1 的英文介面與鍵盤操作。

## 2. 已採用的範圍

| 項目 | v2 決策 |
|---|---|
| 來源 | 使用目前 Station 所在檔案系統的 DeviceHandle；目前建置／QEMU 組態為選單 ESP |
| 清單 | 使用程式內的固定項目表，不掃描磁碟與目錄；顯示的是已設定項目，不宣稱已確認檔案存在 |
| 第一個正式項目 | `GOP Demo`，路徑 `\EFI\TOOLS\GOP.EFI`，使用現有 main.cpp 的正式映像 |
| 支援映像 | x64 EFI application，包括具備對應 EFI 入口的 OS loader；拒絕 EFI driver |
| 載入方式 | 指定完整檔案 device path；LoadImage 的 BootPolicy 使用 FALSE，不進行預設檔案回退搜尋 |
| 載入參數 | 此版不提供命令列或 initrd 設定；子映像的 LoadOptionsSize=0、LoadOptions=nullptr |
| 返回訊息 | 顯示階段及 EFI_STATUS；StartImage 不要求 ExitData，避免在 UI 加入任意子程式字串處理 |
| Continue | 延續 No default boot entry configured；預設項目屬於後續設定功能 |
| Boot Maintenance | 延續唯讀摘要；不新增設定保存 |
| 韌體變數 | Station 不讀寫 BootOrder、BootNext、BootCurrent 或 Boot#### |
| OS 開機 | 提供呼叫 EFI OS loader 的基礎；此版必要驗收不包含 Linux／Windows 完整開機 |

一般 ELF kernel、ISO、整顆磁碟映像與原生 OS loader 留待各格式的載入計畫。
Station 自身不呼叫 ExitBootServices；若日後選取的 OS loader 成功接管平台，返回流程不再適用。
Linux EFI stub 可提供 EFI 入口，但 kernel 參數、initrd 與 root filesystem 仍需另行設定及驗收。[S6]

上述範圍沿用本次對話已接受的「先 GOP demo，再接 OS loader」順序。
若要把完整 OS 開機列為 v2 必要驗收，先補入指定 OS、映像來源與開機參數，再調整任務。

## 3. Boot Manager 畫面與行為

畫面示意；狀態碼與來源資訊必須來自實際呼叫：

```text
UEFI Station 0.2
<實際韌體、UEFI revision、文字與圖形資訊>
----------------------------------------------------------
Boot Manager
> GOP Demo

Path: \EFI\TOOLS\GOP.EFI
<啟動結果或錯誤訊息>
Status: <實際 EFI_STATUS>
----------------------------------------------------------
Up/Down: Select  Enter: Start  Esc: Back
```

| 操作 | 行為 |
|---|---|
| 主選單 Enter：Boot Manager | 開啟清單，首次反白第一項；只開啟清單不載入映像 |
| 清單 Up／Down | 在設定的項目之間移動，不循環；一個項目時不重畫 |
| 清單 Enter | 執行反白項目；子程式執行期間 Station 不讀取鍵盤 |
| 清單 Esc | 返回主頁，保留 Boot Manager 反白位置 |
| 正常返回 | 回到同一個清單項目，顯示 Image returned 與狀態碼，可再次 Enter |
| 可恢復的失敗 | 留在清單，顯示 Load failed／Start failed 等階段與狀態碼 |
| 未釋放的子映像 | 進入復原頁，只允許重試清理或進入既有 Reset 確認流程 |
| 復原頁 Esc | 保持在復原頁，直到子映像釋放；不把 Esc 當成成功退出 |

更新主頁 Boot Manager 的右側說明，移除尚未加入載入功能的舊文字。
40×15 仍可顯示項目、路徑、結果、狀態碼及有效按鍵；使用短提示避免裁切 Esc。
正常返回不自動重新啟動，也不自動跳回主頁。

## 4. 模組與介面

選單處理頁面、鍵盤、畫面快照及恢復。新增 `boot.hpp`／`boot.cpp` 模組，
集中來源查詢、device path、映像型別、LoadImage／StartImage、watchdog 與映像清理。
測試穿過同一個介面，只在 UEFI 函式表提供假韌體，不另外建立通用插件或磁碟探索框架。

核定時的介面草案如下；實作以 `boot.hpp` 為準，另提供 `boot_entries` 與 `watchdog_disabled`：

```cpp
struct BootEntry {
    const CHAR16 *label;
    const CHAR16 *path;
};

enum class BootStage { Prepare, Load, CheckImage, Start, Cleanup };

struct BootResult {
    BootStage stage;
    EFI_STATUS status;
    EFI_STATUS cleanup_status;
};

class BootLoader {
public:
    BootLoader(EFI_HANDLE parent, EFI_SYSTEM_TABLE *table);
    BootResult run(const BootEntry &entry);
    EFI_STATUS retry_cleanup();
    bool has_pending_image() const;
};
```

固定項目表與數量由 boot 模組提供唯讀存取。BootLoader 的實作欄位保持 private。
每次 menu_run 建立一個 BootLoader，持有未完成清理的子 handle，禁止複製。
建構不呼叫韌體；run 才查詢來源，因此缺少開機能力不影響其他主頁。
不以 destructor 隱藏可失敗的清理；退出判斷需明確檢查 pending image。

入口改為 `menu_run(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *table)`，
station.cpp 傳入真正 image_handle；同步更新 host 與 guest observer 的呼叫。
父程式的 runtime_init／runtime_fini 維持既有契約：子程式返回不執行父程式的 runtime_fini。

## 5. 來源與 device path

1. 用父 image_handle 的 Loaded Image Protocol 取得 DeviceHandle 與 FilePath。[S2]
2. 用該 DeviceHandle 的 Device Path Protocol 取得來源裝置路徑。[S3]
3. 驗證目標絕對檔案路徑，附加 Media File Path node 與 End Entire node。
4. 在 LoadImage 前完成目前映像的路徑比對，拒絕載入自己。

新增查詢使用 OpenProtocol 的 GET_PROTOCOL，並以父 image_handle 作 AgentHandle。
檢查子 handle 是否仍有 Loaded Image Protocol 使用 TEST_PROTOCOL。
這兩個模式不要求 CloseProtocol；取得的介面與資料是借用值，不由 Station 釋放。[S1]

device path 的具體限制是本專案決策：

- 來源採單一 instance，最多 1024 bytes／64 nodes；拒絕 End Instance。
- 每個 node 長度至少 4，驗證長度加總、End node 與整數運算；不使用未對齊的型別指標讀寫。
- 目標為 ASCII 的根目錄路徑，最多 259 個 CHAR16，拒絕 `.`／`..` 路徑段及嵌入控制字元。
- 由 BootLoader 保有最多 2048 bytes 的固定路徑 buffer；不為路徑新增 pool 配置。
- 檢查總長度後才組合 header、UTF-16 檔名及 End node；buffer 不暴露給選單。
- MinGW 建置須確認沒有新增未提供的 stack-probe 或標準函式庫符號。

自載入防護以同一個 DeviceHandle 與正規化 FilePath 比對。
正規化處理 ASCII 大小寫、開頭及重複的反斜線；父 FilePath 可合併連續 File Path nodes。
父路徑無法可靠解讀時拒絕該次啟動，其他頁面仍可操作。
實測須涵蓋大小寫與分段路徑；不要只比較 BOOTX64.EFI 的檔名。
不同路徑的 Station 複本不由路徑比對保證識別，正式建置另檢查 GOP.EFI 不是 Station 映像的誤拷貝。

## 6. 映像生命週期與 watchdog

每次 run 的步驟：

1. 拒絕尚有 pending image 的新啟動；檢查必要韌體方法與來源。
2. 組合完整目標路徑；以空 child handle 接收 LoadImage 結果。
3. LoadImage 回報 error 時不呼叫 StartImage。若同時交付 handle，需清理。
4. 成功但 handle 為空時回報 EFI_DEVICE_ERROR。
5. 取得子映像的 Loaded Image Protocol，確認 ImageCodeType／ImageDataType 為 EfiLoaderCode／EfiLoaderData；driver 或無效資訊回報 EFI_UNSUPPORTED 並卸載。
6. 設定空 LoadOptions，然後嘗試設定 300 秒 watchdog。
7. 設定 watchdog 的 EFI_UNSUPPORTED 代表系統無此能力，可繼續；其他 error 不啟動子映像。
8. 呼叫 StartImage，ExitData 使用 nullptr；不持有子程式的返回字串。
9. StartImage 返回後，先嘗試停用 watchdog，再處理仍存在的映像及 UI 恢復。

EFI application 的退出會由韌體釋放其映像及 handle。[S1]
因此正常 application 返回後不再無條件 UnloadImage，也不讀取舊 Loaded Image 指標。
StartImage 的 error 可能是啟動前拒絕或子程式返回的錯誤；不能只按 EFI_STATUS 推測 handle 是否存活。
返回後使用 TEST_PROTOCOL：仍有 Loaded Image Protocol 才卸載。
在 GUID、旗標與其餘參數均有效的前提下，EFI_UNSUPPORTED 或 EFI_INVALID_PARAMETER 表示該 child handle 已無可用的 Loaded Image Protocol，清除持有紀錄。
其他查詢失敗保留 pending image，不假裝已釋放；不得把所有 OpenProtocol error 都當成已釋放。

LoadImage 的 EFI_SECURITY_VIOLATION 可能交付有效 handle，但禁止啟動；該 handle 仍需清理。[S1]
卸載持續失敗時保留 handle，阻擋新啟動與 Return to Firmware，提供 Retry cleanup／Reset。
Reset 仍使用既有 Cancel／Restart 確認；Cancel 返回復原頁。
子 handle 清理成功後，才能回到清單或退出父 application。
所有退出路徑，包括輸入錯誤與模式恢復失敗，都先嘗試清理 pending image。
仍有 pending image 時禁止從 menu_run 返回。可用 console 下進入復原頁；
若 console 與清理同時永久失效，保留可輸出的診斷並進入受控停止，不自動重啟或宣稱已恢復。
Reset 方法缺少時留在復原頁，繼續提供清理重試；永久失效的複合案例需明確列為不可恢復。

300 秒啟動 watchdog 與返回後停用採 UEFI boot manager 的時序。[S4]
這是 Station 的啟動政策，不表示已實作韌體的 BootOrder 或全部 BDS 行為。
選單等候仍採 watchdog 停用；返回後停用失敗時不再進入一般等待。
若沒有 pending image，經 console 清理後返回主要錯誤；仍有 pending image 時保持復原流程。

映像記憶體型別的拒絕規則對照 EDK II 的 application／driver 分配行為。[S5]
此規則用於本版支援範圍，不替代韌體的映像驗證或簽章政策。

## 7. 子程式返回後的顯示恢復

Station 保存兩層快照：進入 menu 前的呼叫者設定，以及每次 StartImage 前的選單設定。
子程式返回先恢復選單；最終離開 Station 才恢復呼叫者設定。

啟動前的快照包含：

- 文字 Mode、columns／rows、Attribute、CursorColumn／Row、CursorVisible。
- 有效 GOP 的 Mode number、解析度與 PixelFormat，複製純值，不保留 Mode／Info 指標。

啟動前確認必需的模式恢復方法可用；缺少時顯示此項啟動不可用。
GOP 缺少時仍可啟動不依賴圖形的 EFI application，只驗證文字恢復。

返回後先恢復已改變的 GOP，再恢復文字 Mode，重新查詢兩者，最後恢復 console 屬性與游標。
console 方法可能影響 GOP；不能以單一 SetMode 的 EFI_SUCCESS 作為恢復完成證明。
重新取得當前 ConOut／Mode／GOP Info；確認實際文字模式、尺寸及可用 GOP 與快照相符。
更新 renderer 的 columns／rows，並在重畫後再次驗證畫面未造成模式變動。
必要時隱藏 UI 游標；EnableCursor 不支援時延續 v1 的處理。

模式恢復失敗時，不用舊尺寸繪製完整選單，也不顯示啟動成功。
在目前尺寸已查明且安全時顯示短錯誤；完成可執行的清理後返回錯誤。
若仍持有子 handle，採第 6 節復原流程，不直接退出父映像。

## 8. 錯誤與返回契約

BootResult.status 保存啟動過程的第一個主要 error，BootResult.cleanup_status 保存清理的第一個 error。
選單另外保存恢復狀態。主要 error 不被卸載、watchdog 或 console 錯誤覆寫。
沒有較早 error 時，以清理或恢復的 error 作為最終錯誤。
UEFI warning 與 error 分開判斷；warning 不當成致命失敗，仍需驗證輸出 handle／資訊。

| 案例 | 必要結果 |
|---|---|
| 只開啟清單／Esc 返回 | 沒有 LoadImage／StartImage |
| 路徑指向 Station 自己 | 在 LoadImage 前拒絕，無遞迴載入 |
| 檔案不存在、映像損壞或錯誤架構 | 顯示 Load 階段與真實狀態，恢復後可重試 |
| LoadImage error 且有 handle | 不 Start；清理已交付 handle |
| 子映像為 driver | 不 Start；清理 handle |
| 啟動 watchdog 設定失敗 | 不 Start；停用嘗試與映像清理都有紀錄 |
| StartImage 正常返回 | 不重複卸載已消失的 application；重畫清單 |
| 子程式返回 error | 顯示狀態，清理及恢復成功後可重試 |
| 啟動前被韌體拒絕、handle 仍存在 | 卸載一次；若失敗，保留 pending image |
| pending image 未清理 | 阻擋新啟動與父映像退出，只能重試清理或確認 Reset |
| 返回後 watchdog 停用失敗 | 不進入普通選單等待，不覆寫較早 error |
| 顯示模式恢復失敗 | 不使用舊 geometry 繪圖；保留主要狀態並走安全出口／復原流程 |

子映像是否真正進入 entry point 由 host 呼叫紀錄或 QEMU 子映像輸出證明。
產品 UI 不根據 StartImage 的返回碼捏造「OS 已開機」或「子程式已執行」訊息。

## 9. ABI、檔案與建置

uefi.hpp 新增正確的 OpenProtocol、LoadImage、StartImage、UnloadImage 函式型別，
以及 Loaded Image Protocol、Device Path header、EFI_MEMORY_TYPE、GUID、旗標與所需狀態常數。
v2 不呼叫目前仍是 EFI_GENERIC_FN 的欄位。

| x64 檢查 | 預期值 |
|---|---|
| Boot Services：LoadImage／StartImage／UnloadImage | offset 200／208／224 |
| Boot Services：OpenProtocol | offset 280 |
| Device Path header | size 4 |
| Loaded Image：DeviceHandle／FilePath | offset 24／32 |
| Loaded Image：LoadOptionsSize／LoadOptions | offset 48／56 |
| Loaded Image：ImageCodeType／Unload | offset 80／88 |
| Loaded Image Protocol | size 96 |
| EFI_MEMORY_TYPE | size 4 |

值由官方宣告與目前 x64 ABI 推導，實作時以 MinGW 與 host static_assert 驗證。[S2][S3][S7]
延續 snake_case、freestanding、Microsoft x64 ABI、16-bit wchar_t；不引入例外、RTTI 或 hosted C++ 依賴。

```text
boot.hpp / boot.cpp             固定項目、來源、載入與 pending image 所有權
station.cpp / menu.hpp           傳遞父 image_handle
menu.cpp                        清單、啟動、雙層快照、恢復及復原頁
tests/boot_host.cpp              真正 boot 模組的假韌體測試
tests/boot_probe.cpp             先驗證真實 LoadImage／StartImage 的 EFI 入口
tests/boot_child.cpp             僅供測試的返回／錯誤／模式改變子映像
tests/menu_boot_probe.cpp        返回後、下一次等待前的獨立 guest observer
tests/menu_boot_qemu.py          乾淨來源的核心與 UI 啟動驗收
tests/menu_host.cpp              v1 回歸及整合測試
tests/menu_qemu.py               保留 v1 導航驗收，更新新清單與來源清單
docs/knowledge/menu.md           實作完成後更新操作說明
docs/history/menu-v2.md          實作完成後保存 review 與結果
docs/artifacts/menu-v2/          實作完成後保存核心 log、截圖、來源雜湊
```

正式 ESP 為：

```text
output/menu/esp/EFI/BOOT/BOOTX64.EFI  ← Station
output/menu/esp/EFI/TOOLS/GOP.EFI    ← 原 GOP 正式映像
```

build-menu 同時建置並配置兩個映像；原 make build／run／test-gop 保留用途。
GOP.EFI 必須與乾淨建置的 GOP 映像一致，且與 Station 不同；每個映像各自只有一個 efi_main。
測試子映像與觀察者使用獨立 build target／ESP，不添加產品測試開關。

現有命令繼續使用：

```bash
make build-menu
make run-menu
make test-menu-static
make test-menu-host
make test-menu-qemu
make test-menu
make test
make test-host
make test-gop
```

v2 新增的入口：

```bash
make test-boot-host        # 真正 boot 模組的 host 行為／所有權驗證
make test-boot-qemu        # 核心載入流程：python3 tests/menu_boot_qemu.py --core-only
make test-menu-boot-qemu   # 透過真正 menu 操作啟動與返回
```

test-menu 最終納入 test-boot-host 與 test-menu-boot-qemu。
Sanitizer 使用 g++／clang++ 各編譯 `tests/boot_host.cpp boot.cpp`，
以及更新後的 `tests/menu_host.cpp menu.cpp boot.cpp station.cpp`。
旗標沿用 `-std=c++17 -fshort-wchar -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer`。

## 10. 依序實作的任務

### 任務 1：UEFI 載入 ABI

相依：無。範圍：S。檔案：uefi.hpp、tests/boot_host.cpp。

- [x] 補齊第 9 節型別、GUID、常數及 offset／size 檢查。
- [x] 真正宣告可用於 host 假函式表，既有 GOP 與 menu fixture 仍可編譯。
- [x] 驗證：`make test test-host test-menu-static test-menu-host`，及 host ABI 單檔編譯。

### 任務 2：來源、路徑與自載入拒絕

相依：1。範圍：M。檔案：boot.hpp、boot.cpp、tests/boot_host.cpp、Makefile。

- [x] 完成固定項目表、BootLoader 介面與第 5 節來源／路徑契約。
- [x] 缺少協定、超長／畸形路徑、自載入及大小寫變體均在 LoadImage 前拒絕。
- [x] 驗證：新增 `make test-boot-host`，實際檢查傳入韌體的 device path bytes 與 parent handle；另以 `make output/menu/boot.o` 先檢查 MinGW 編譯。

### 任務 3：載入、執行與清理所有權

相依：2。範圍：M。檔案：boot.cpp、tests/boot_host.cpp。

- [x] 實作型別拒絕、LoadImage／StartImage、空 LoadOptions 與 watchdog 時序。
- [x] 覆蓋 application 自動釋放、啟動前拒絕、Security Violation、pending image／retry_cleanup；不 double-unload。
- [x] 驗證：`make test-boot-host`，GCC／Clang ASan、UBSan 與 leak 檢查。

檢查點 A：來源、ABI、所有權與錯誤優先順序已驗證，再接真實韌體。

### 任務 4：真實 EFI 載入探針與 demo 配置

相依：3。範圍：M。檔案：Makefile、tests/boot_probe.cpp、tests/menu_boot_qemu.py、tests/menu_qemu.py。

- [x] build-menu 配置正式 GOP.EFI；host menu 連結 boot.cpp，QEMU 來源清單加入 boot.hpp／boot.cpp／main.cpp。
- [x] 新探針在 OVMF 使用真正父 handle，啟動正式 GOP demo 並接回控制權。
- [x] 驗證：`make test-menu-static test-boot-qemu`；子映像輸出、800×600 畫面及返回 marker 都必須出現。

### 任務 5：傳遞父映像 handle

相依：4。範圍：M。檔案：station.cpp、menu.hpp、menu.cpp、tests/menu_host.cpp、tests/menu_qemu_probe.cpp。

- [x] 更新 menu_run 介面與所有呼叫者，建立每次選單的 BootLoader。
- [x] 此增量保持 v1 頁面行為，尚未把 Enter 接到子映像。
- [x] 驗證：`make test-menu-host test-menu-qemu test-menu-static`，確認既有退出及 runtime 契約。

### 任務 6：可導航的 Boot Manager 清單

相依：5。範圍：M。檔案：menu.cpp、tests/menu_host.cpp、tests/menu_qemu.py。

- [x] 顯示 GOP Demo 與路徑，更新右側說明，清單 Esc 保留主頁位置。
- [x] 清單、40×15 提示及選項邊界可驗證；此增量先顯示啟動尚未接通，避免暴露未完成恢復流程。
- [x] 驗證：`make test-menu-host test-menu-qemu`；沒有 LoadImage／StartImage 意外呼叫。

檢查點 B：真實 loader 可執行，選單可顯示清單，既有頁面仍通過；再接入返回恢復。

### 任務 7：啟動與顯示恢復整合

相依：6。範圍：M。檔案：menu.cpp、tests/menu_host.cpp、tests/boot_child.cpp、Makefile。

- [x] Enter 接通 run；啟動前後雙層快照、模式重查及幾何更新符合第 7 節。
- [x] pending image 的復原頁、Retry cleanup、Reset Cancel／Restart 與退出阻擋均符合契約。
- [x] 驗證：`make test-boot-host test-menu-host test-menu-static`；錯誤與模式改變案例在 sanitizer 下通過。

### 任務 8：QEMU 整合與獨立觀察

相依：7。範圍：M。檔案：tests/menu_boot_qemu.py、tests/menu_boot_probe.cpp、Makefile。

- [x] 透過選單反覆啟動正式 GOP demo；另以測試子程式留下改變的 mode／屬性／游標與短 watchdog。
- [x] observer 在恢復後、下一次等待前比較真實狀態；QMP 畫面與返回 marker 必須與該輪操作一致。
- [x] 驗證：`make test-menu-boot-qemu test-menu`，並保留 v1／GOP 回歸、截圖與每台 VM 的獨立 ESP／VARS。

### 任務 9：文件、review 與交叉驗證

相依：8。範圍：M。文件組：menu 知識頁、menu-v2 歷史／artifact、README、文件索引與本計畫。

- [x] 原計畫完成項目有可追溯證據，正式／測試映像與來源雜湊清楚分開。
- [x] 沿用 v1 的多 subagent review 與交叉驗證，至少涵蓋 ABI／生命週期、UI／恢復、測試可信度三個角度；重要問題解決後重新驗證。
- [x] 驗證：`make test-menu test-boot-qemu test test-host test-gop`、雙編譯器 sanitizer、截圖視檢與文件連結檢查。

檢查點 C：所有必要案例通過，才更新為已實作。
此實作順序允許在介面確定後，分開準備 host fixture、QEMU harness 與文件；共享 UEFI／menu 修改保持序列進行。

## 11. 必要驗收矩陣

| 層級 | 必須覆蓋 |
|---|---|
| 靜態 | PE32+／EFI subsystem／入口／唯一 efi_main；兩個正式 ESP 檔案一致性與子映像來源；ABI／未提供符號檢查 |
| Boot host | 父 handle、協定模式、目標 bytes、長度／對齊／自載入防護、Load／Start 呼叫順序、driver 拒絕、空 LoadOptions |
| 所有權 host | failed load 帶 handle、空 handle、已退出 app、仍存活的未啟動 image、查詢／卸載失敗、復原與退出阻擋 |
| Watchdog host | 菜單 0 → 啟動前 300 → 返回後 0；unsupported／設定失敗／取消失敗與主要錯誤保留 |
| Menu host | 開頁不啟動、選擇／Esc、連續返回、重試、pending image 頁、Reset、文字／GOP 變更、恢復失敗及 v1 功能 |
| 實際 GOP demo | 選單 Enter → 子映像建構及繪圖輸出 → 真實 800×600 三色塊 → 子映像按鍵退出 → 清單恢復；至少連續三輪 |
| 實際 dirty child | 子程式留下改變的文字／GOP／屬性／游標；返回後原尺寸與狀態在 observer 中相符，UI 再次可用 |
| 實際 watchdog | dirty child 設定 2 秒 watchdog 後返回；父選單閒置超過 4 秒仍可操作；該案例確實啟用計時器才算通過 |
| 實際失敗 | 各 VM 的 ESP 分別移除／破壞 GOP.EFI；顯示該輪 Load 狀態且仍可返回或重試；錯誤子映像返回能恢復選單 |
| 無 VGA | 可操作 v1 頁面；另以不使用 GOP 的 EFI 子映像驗證真正載入與文字返回 |
| 回歸 | v1 導航、資訊、Reset、console／runtime；既有 GOP 主機、畫面與恢復 |

實際子程式的 marker 至少區分 entry、返回前及 runtime_fini。
GOP demo 驗收使用既有正式 image；dirty／error child 的 marker 只存在測試映像。
等待下一輪 child marker／結果時，只接受該次 Enter 之後的 serial 範圍，避免舊訊息造成假通過。
截圖須檢查文字清晰、反白、尺寸與重畫；像素相等或 marker 本身不足以證明真實模式恢復。

每個 QEMU 案例使用新的 VARS 與 ESP，從乾淨來源建置，保存完整命令與來源雜湊。
Signed image 拒絕先以 host 注入；未建立專用簽章／Secure Boot 環境前，不宣稱做過實際 Secure Boot 驗收。
目前 OVMF 的單欄模式限制延續 v1，40×15 由 host fixture 覆蓋並明確記錄。

## 12. 實作規則與風險

始終遵守：正確函式型別、限制路徑大小、明確資源所有權、真實資料、每個增量可建置與驗證。
擴大範圍前更新規格：多磁碟搜尋、設定檔解析、NVRAM 寫入、OS 命令列／initrd、完整 OS 開機。
不可移除既有驗收、繞過 firmware image policy、覆蓋系統 OVMF 或使用者現有 VARS。

| 風險 | 處理 |
|---|---|
| 把 Boot Manager 與完整 OS loader 混為一談 | 用正式 GOP round trip 證明 EFI 執行；OS 開機另有驗收範圍 |
| StartImage 返回的 handle 已失效 | 不用舊 protocol 指標，測試存活協定後才卸載 |
| 無法卸載已交付的 child | 保留 pending 所有權，阻擋新載入與父映像退出 |
| 子程式改變 console 與 GOP | 雙層快照、重新取得資料、重畫後核對，不用 v1 快取尺寸直接畫 |
| 留下啟動 watchdog | 返回後先停用，獨立 QEMU 短計時器案例驗證 |
| 複製錯誤映像或直接載入自己 | 正規化來源路徑防護，加上建置時正式 child 一致性檢查 |
| 測試變體污染產品 | 固定正式項目，測試 ESP 才替換 payload，不加產品測試開關 |

## 13. 來源

查核日期：2026-10-13。以下為 primary sources；參考宣告與生命週期，不代表本機 OVMF 與上游最新程式完全相同。

- [S1：UEFI Boot Services：協定、LoadImage／StartImage／UnloadImage／Exit](https://github.com/UEFI/UEFI-Specification-Release/blob/main/source/07_Services_Boot_Services.rst)
- [S2：EFI Loaded Image Protocol](https://github.com/UEFI/UEFI-Specification-Release/blob/main/source/09_Protocols_EFI_Loaded_Image.rst)
- [S3：EDK II Device Path 宣告](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Protocol/DevicePath.h)
- [S4：UEFI Boot Manager watchdog 時序](https://github.com/UEFI/UEFI-Specification-Release/blob/main/source/03_Boot_Manager.rst)
- [S5：EDK II Core image 載入／記憶體型別／退出](https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Core/Dxe/Image/Image.c)
- [S6：Linux EFI boot stub 與參數](https://docs.kernel.org/admin-guide/efi-stub.html)
- [S7：EDK II UEFI 函式型別](https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Uefi/UefiSpec.h)
- [目前 menu.cpp](../../menu.cpp)、[station.cpp](../../station.cpp)、[uefi.hpp](../../uefi.hpp)、[Makefile](../../Makefile)
- [v1 測試與完成紀錄](../history/menu-v1.md)

[S1]: https://github.com/UEFI/UEFI-Specification-Release/blob/main/source/07_Services_Boot_Services.rst
[S2]: https://github.com/UEFI/UEFI-Specification-Release/blob/main/source/09_Protocols_EFI_Loaded_Image.rst
[S3]: https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Protocol/DevicePath.h
[S4]: https://github.com/UEFI/UEFI-Specification-Release/blob/main/source/03_Boot_Manager.rst
[S5]: https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Core/Dxe/Image/Image.c
[S6]: https://docs.kernel.org/admin-guide/efi-stub.html
[S7]: https://github.com/tianocore/edk2/blob/master/MdePkg/Include/Uefi/UefiSpec.h

## 14. 本次規劃檢查

- 已查核上述官方介面、映像生命週期與 watchdog 時序。
- 以 `/tmp` MinGW layout probe 核對第 9 節 offset／size；這是宣告布局推導，不是 v2 功能驗收。
- 本計畫、文件索引及 HTML 概覽的本機連結已檢查；HTML renderer 的 STE 檢查為零警告。
- v1 產品與測試來源的 SHA256 仍與既有驗收紀錄相符；本次沒有修改 EFI 程式或 Makefile。
- v2 建置、host、QEMU 與多 agent code review 尚待實作階段執行，不能把上述命令視為已通過。
