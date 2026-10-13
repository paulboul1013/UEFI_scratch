#pragma once

#include "uefi.hpp"

enum class MenuPage {
    FrontPage, LanguagePage, DevicePage, BootPage, MaintenancePage,
    ContinueInfo, ResetConfirm, RecoveryPage, ExitRequested
};

// Runs in the current text mode and restores console settings before return.
EFI_STATUS menu_run(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *table);
