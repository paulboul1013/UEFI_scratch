# GOP 第二版最終文件與證據審查

日期：2026-10-12。審查角色：abi_review。結論：文件、實作與保存證據一致，沒有尚未處理的阻擋問題。

核對範圍：README.md、docs/README.md、knowledge/gop.md、testing.md、architecture.md、history/gop-v2.md、gop-v2-plan.md，以及 docs/artifacts/gop-v2/ 的 log、畫面與來源識別。

- 現況文件正確描述列舉、800×600 選擇、逐輪 FreePool、清屏先於切換、重新讀取與統一恢復。沒有將第一版沿用模式描述當成目前行為。
- 114 個主機案例、嚴格編譯、ASan/UBSan/LeakSanitizer、正式與 probe 乾淨建置及 QEMU 結果，均有對應保存證據。此輪只核對最終結果與來源，沒有重跑整套測試。先前本人獨立執行的 102 案例與乾淨靜態檢查另有紀錄。
- source-sha256.json 的全部 10 個來源雜湊符合目前程式。最終 QEMU build 與 probe-build 的各 8 份來源，也逐一符合相同雜湊。
- 4 份 PNG 與對應的原始 PPM 像素相同；尺寸均為 800×600，上半部各有 3016 個非黑像素。4 份 PPM 的 SHA256 都是 59aaddc8189fda3b1cf56508a1108e95619ed683af0d96080bcd86b5175fba46，符合完成紀錄。
- probe serial 證明應用返回後、runtime_fini 與韌體接手前，原 mode0 1280×800 format1 已恢復，且應用回傳 EFI_SUCCESS。文件沒有以韌體選單尺寸作為恢復證據。
- 預期失敗控制的 log 確認：v1 被新契約拒絕、省略恢復被 probe 拒絕、錯誤 PE entry 與 ESP 不一致被靜態檢查拒絕。文件正確區分這些預期失敗與正式驗收結果。
- 已檢查 21 份 Markdown 文件的本地連結目標，沒有不存在的目標。最終 git diff --check 通過。
- 核定計畫保留事前勾選狀態；首段清楚指向完成紀錄。比較原 /tmp 計畫後，差异只有保存說明、可攜引用與 FreePool offset 64→72。正常流程、失敗政策及驗收條件沒有改寫。
- 文件清楚限定主機 startup 測試使用 runtime stubs；FreePool 拒絕時只保證嘗試一次，fixture 清理不算產品成功釋放；QEMU 結果只代表本次 OVMF 組態，不宣稱已在 QEMU 驗證所有像素格式。
- optional：architecture 的樹狀清單最初漏列 gop_qemu_probe.cpp，已由 root 補上，並同步列出 artifacts。

原有文件整理保留其 knowledge/history 分類、既有主題與第一版歷史。沒有可用的實作前完整檔案快照，故本審查驗證內容與結構的連續性，沒有聲稱原有未追蹤文件逐 byte 未變。
