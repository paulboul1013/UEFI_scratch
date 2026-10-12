// Test-only guest observer. No application helpers are used for verification.
#define efi_main gop_probe_application
#include "../main.cpp"
#undef efi_main

namespace {

struct ProbeMode {
    UINT32 number;
    UINT32 width;
    UINT32 height;
    EFI_GRAPHICS_PIXEL_FORMAT format;
};

bool probe_snapshot(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, ProbeMode &saved)
{
    if (gop == nullptr || gop->Mode == nullptr) {
        return false;
    }
    const EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode = gop->Mode;
    if (mode->Info == nullptr
        || mode->SizeOfInfo < sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION)
        || mode->MaxMode == 0 || mode->Mode >= mode->MaxMode) {
        return false;
    }
    const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = mode->Info;
    if (info->Version != 0 || info->HorizontalResolution == 0
        || info->VerticalResolution == 0
        || static_cast<UINT32>(info->PixelFormat) >= PixelFormatMax) {
        return false;
    }
    saved = {mode->Mode, info->HorizontalResolution, info->VerticalResolution,
             info->PixelFormat};
    return true;
}

// These formatters are deliberately separate from main.cpp's implementation.
CHAR16 *probe_text(CHAR16 *next, const CHAR16 *value)
{
    while (*value != 0) {
        *next++ = *value++;
    }
    return next;
}

CHAR16 *probe_decimal(CHAR16 *next, UINT32 value)
{
    CHAR16 digits[10];
    UINTN count = 0;
    do {
        digits[count++] = static_cast<CHAR16>(L'0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) {
        *next++ = digits[--count];
    }
    return next;
}

CHAR16 *probe_status(CHAR16 *next, EFI_STATUS status)
{
    const CHAR16 hex[] = L"0123456789ABCDEF";
    next = probe_text(next, L" status=0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        *next++ = hex[(status >> shift) & 15];
    }
    return next;
}

EFI_STATUS probe_failure(EFI_SYSTEM_TABLE *table, const CHAR16 *reason,
                         EFI_STATUS status)
{
    CHAR16 line[128] {};
    CHAR16 *next = probe_text(line, L"\r\n[PROBE] RESTORE_FAILED ");
    next = probe_text(next, reason);
    next = probe_status(next, status);
    next = probe_text(next, L"\r\n");
    *next = 0;
    if (table != nullptr && table->ConOut != nullptr
        && table->ConOut->OutputString != nullptr) {
        table->ConOut->OutputString(table->ConOut, line);
    }
    return status == EFI_SUCCESS ? EFI_DEVICE_ERROR : status;
}

} // namespace

extern "C" EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *table)
{
    if (table == nullptr || table->BootServices == nullptr
        || table->BootServices->LocateProtocol == nullptr) {
        return probe_failure(table, L"InitialServices", EFI_DEVICE_ERROR);
    }

    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    void *interface = nullptr;
    EFI_STATUS status = table->BootServices->LocateProtocol(&guid, nullptr, &interface);
    ProbeMode original {};
    if (status != EFI_SUCCESS) {
        return probe_failure(table, L"InitialLocate", status);
    }
    if (!probe_snapshot(static_cast<EFI_GRAPHICS_OUTPUT_PROTOCOL *>(interface), original)) {
        return probe_failure(table, L"InitialMode", EFI_DEVICE_ERROR);
    }

    status = gop_probe_application(image_handle, table);
    if (status != EFI_SUCCESS) {
        return probe_failure(table, L"ApplicationStatus", status);
    }

    // Observe after the real application returns, before startup calls runtime_fini.
    interface = nullptr;
    status = table->BootServices->LocateProtocol(&guid, nullptr, &interface);
    ProbeMode restored {};
    if (status != EFI_SUCCESS) {
        return probe_failure(table, L"ReturnedLocate", status);
    }
    if (!probe_snapshot(static_cast<EFI_GRAPHICS_OUTPUT_PROTOCOL *>(interface), restored)) {
        return probe_failure(table, L"ReturnedMode", EFI_DEVICE_ERROR);
    }
    if (restored.number != original.number || restored.width != original.width
        || restored.height != original.height || restored.format != original.format) {
        return probe_failure(table, L"ReturnedMismatch", EFI_DEVICE_ERROR);
    }

    // 128 code units cover fixed text, four 10-digit values and a 16-digit status.
    CHAR16 line[128] {};
    CHAR16 *next = probe_text(line, L"\r\n[PROBE] RESTORE_VERIFIED mode=");
    next = probe_decimal(next, restored.number);
    *next++ = L' ';
    next = probe_decimal(next, restored.width);
    *next++ = L'x';
    next = probe_decimal(next, restored.height);
    next = probe_text(next, L" format=");
    next = probe_decimal(next, static_cast<UINT32>(restored.format));
    next = probe_status(next, EFI_SUCCESS);
    next = probe_text(next, L"\r\n");
    *next = 0;
    return table->ConOut->OutputString(table->ConOut, line);
}
