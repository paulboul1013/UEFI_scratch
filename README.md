# UEFI From Scratch

從 Linux / WSL2 以 MinGW 交叉編譯 x86_64 UEFI application，在 QEMU / OVMF 執行。
目前已完成文字輸出、鍵盤事件、最小 C++ runtime 與 GOP 第二版。

## 快速開始

需要 `x86_64-w64-mingw32-g++`、MinGW binutils、QEMU 與 OVMF。
主機測試另需 Linux `g++`，QEMU smoke test 另需 Python 3。
以下指令在專案根目錄執行：

```bash
make
make run
```

建置會產生 `output/BOOTX64.efi`，並複製至 `esp/EFI/BOOT/BOOTX64.EFI`。
程式列舉 GOP 模式，優先使用 `800×600` 顯示紅綠藍三個色塊；找不到有效目標時沿用原模式。按任意鍵後先恢復原模式，再清理 C++ runtime 並返回韌體。

QEMU / OVMF 實測從原模式 `1280×800` 切至 `800×600`。列舉與初始訊息保留在 serial，清屏後的最終畫面如下。

```text
[GOP] ACTIVE mode=3 format=BGR
[GOP] RESOLUTION 800x600
Press any key to exit...
[GOP] DRAW_OK
```

## 驗證

```bash
make test        # 建置、ABI 與 PE / entry point 檢查
make test-host   # 模式、所有權、錯誤恢復與啟動順序的主機測試
make test-gop    # 正式與 probe 乾淨建置、畫面與返回前恢復驗證
```

完整操作、log 位置與環境限制見 [驗證與 Debug](docs/knowledge/testing.md)。
手動 ESP / OVMF 操作見 [QEMU 文件](docs/knowledge/qemu.md)。其他建置入口可用 `make help` 查看。

## 文件入口

| 需求 | 入口 |
|---|---|
| 按問題與關鍵字搜尋知識 | [人類知識查詢](docs/README.md#人類知識查詢) |
| 查看已完成的實作任務、commit 與驗收結果 | [歷史已實作計畫](docs/README.md#歷史已實作計畫) |
| 查看文件分類、搬移去向與整理紀錄 | [文件分類索引](docs/README.md) |

初次閱讀可從 [專案架構](docs/knowledge/architecture.md) 開始，再讀 [ABI](docs/knowledge/abi.md)、[C++ runtime](docs/knowledge/runtime.md) 與 [GOP](docs/knowledge/gop.md)。

第二版的實作計畫、審查與驗收見 [完成紀錄](docs/history/gop-v2.md)。後續候選方向見 [尚未實作的方向](docs/README.md#尚未實作的方向)。
