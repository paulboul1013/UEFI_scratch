#pragma once

#include "uefi.hpp"

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

const BootEntry *boot_entries(UINTN *count);

// The child handle remains owned until firmware confirms that it is gone.
// Destruction performs no hidden, potentially failing firmware operation.
class BootLoader {
public:
    BootLoader(EFI_HANDLE parent, EFI_SYSTEM_TABLE *table);
    BootLoader(const BootLoader &) = delete;
    BootLoader &operator=(const BootLoader &) = delete;
    BootLoader(BootLoader &&) = delete;
    BootLoader &operator=(BootLoader &&) = delete;

    BootResult run(const BootEntry &entry);
    EFI_STATUS retry_cleanup();
    bool has_pending_image() const;
    bool watchdog_disabled() const;

private:
    EFI_STATUS prepare_path(const BootEntry &entry);
    EFI_STATUS cleanup_image();
    EFI_STATUS disable_watchdog();
    EFI_HANDLE parent_;
    EFI_SYSTEM_TABLE *table_;
    EFI_HANDLE pending_image_;
    bool watchdog_disabled_;
    UINT8 device_path_[2048];
};
