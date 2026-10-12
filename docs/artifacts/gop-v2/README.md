# GOP 第二版驗收證據

日期：2026-10-12。用途：保留本次實作的核心結果，避免 /tmp 清除後只剩完成聲明。
完成紀錄見 [gop-v2.md](../../history/gop-v2.md)，操作入口見 [testing.md](../../knowledge/testing.md)。

- host.log / sanitizer.log：114 案例、編譯與執行狀態。
- host-verification.md / host-review.md / document-review.md：命令、限制、來源雜湊與獨立審查。
- local-build.log / clean-build.log / clean-probe-build.log：正式與 probe 的 ABI、PE、符號、entry / ESP checks。
- qemu-summary.log：最終完整 QEMU script 輸出。
- positive-* / probe-* / no-vga-*：各 VM 的 serial、QEMU log，以及原始截圖轉成的 PNG。
- source-sha256.json / qemu-source-sha256.txt：最終來源與兩個乾淨來源樹的識別。
- environment.json：本機編譯器、QEMU 版本及 OVMF 4M 韌體 SHA256。
- *-rejection.log：驗證工具的預期失敗控制；它們不代表正式驗收失敗。

完整暫存目錄為 /tmp/uefi-gop-qemu-r5hwof8v/。PNG 保留原 PPM 像素；未保存大型 ESP / VARS 或 binary 到文件目錄。四份初始／等待原 PPM 的 SHA256 均為 `59aaddc8189fda3b1cf56508a1108e95619ed683af0d96080bcd86b5175fba46`。
