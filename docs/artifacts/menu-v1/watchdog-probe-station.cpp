#include "menu.hpp"
extern "C" EFI_STATUS efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE *table) {
    using Watchdog = EFI_STATUS (EFIAPI *)(UINTN, UINT64, UINTN, CHAR16 *);
    auto watchdog = reinterpret_cast<Watchdog>(table->BootServices->SetWatchdogTimer);
    EFI_STATUS status = watchdog(2, 0, 0, nullptr);
    if ((status & (1ULL << 63)) != 0) return status;
    return menu_run(table);
}
