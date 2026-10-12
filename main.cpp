#include "uefi.hpp"

static int g_constructor_test = 0;


class RuntimeTest {
public:

    RuntimeTest()
    {
        g_constructor_test = 1234;
    }

    ~RuntimeTest()
    {
        g_constructor_test = 0;
    }
};


RuntimeTest g_runtime_test;


// 只使用 stack buffer。此程式沒有連結標準函式庫。
static CHAR16 *append_text(CHAR16 *next, const CHAR16 *text)
{
    while (*text) {
        *next++ = *text++;
    }
    return next;
}

static CHAR16 *append_decimal(CHAR16 *next, UINTN value)
{
    CHAR16 digits[20];
    UINTN count = 0;
    do {
        digits[count++] = L'0' + value % 10;
        value /= 10;
    } while (value != 0);

    while (count != 0) {
        *next++ = digits[--count];
    }
    return next;
}

static EFI_STATUS gop_error(
    EFI_SYSTEM_TABLE *system_table,
    const CHAR16 *operation,
    EFI_STATUS status
)
{
    CHAR16 message[96] {};
    CHAR16 *next = append_text(message, L"[GOP] ERROR ");
    next = append_text(next, operation);
    next = append_text(next, L" status=0x");
    const CHAR16 hex[] = L"0123456789ABCDEF";
    for (int shift = 60; shift >= 0; shift -= 4) {
        *next++ = hex[(status >> shift) & 0xf];
    }
    next = append_text(next, L"\r\n");
    *next = 0;
    system_table->ConOut->OutputString(system_table->ConOut, message);
    return status;
}

// QueryMode 的配置只在單輪內存活；跨 SetMode 只保存純量。
struct ModeSnapshot {
    UINT32 number;
    UINT32 width;
    UINT32 height;
    EFI_GRAPHICS_PIXEL_FORMAT format;
};

static EFI_STATUS read_info(
    const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info, UINTN size,
    UINT32 number, ModeSnapshot &snapshot
)
{
    if (info == nullptr || size < sizeof(*info)) {
        return EFI_DEVICE_ERROR;
    }
    if (info->HorizontalResolution == 0 || info->VerticalResolution == 0) {
        return EFI_DEVICE_ERROR;
    }
    if (info->Version != 0 || info->PixelFormat >= PixelFormatMax) {
        return EFI_UNSUPPORTED;
    }
    snapshot = {number, info->HorizontalResolution, info->VerticalResolution,
                info->PixelFormat};
    return EFI_SUCCESS;
}

static EFI_STATUS read_mode(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, ModeSnapshot &snapshot)
{
    if (gop == nullptr || gop->Mode == nullptr || gop->Mode->MaxMode == 0
        || gop->Mode->Mode >= gop->Mode->MaxMode) {
        return EFI_DEVICE_ERROR;
    }
    return read_info(gop->Mode->Info, gop->Mode->SizeOfInfo, gop->Mode->Mode, snapshot);
}

static bool mode_matches(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, const ModeSnapshot &expected)
{
    ModeSnapshot current {};
    return read_mode(gop, current) == EFI_SUCCESS
        && current.number == expected.number && current.width == expected.width
        && current.height == expected.height && current.format == expected.format;
}

static const CHAR16 *format_label(EFI_GRAPHICS_PIXEL_FORMAT format)
{
    switch (format) {
    case PixelRedGreenBlueReserved8BitPerColor: return L"RGB";
    case PixelBlueGreenRedReserved8BitPerColor: return L"BGR";
    case PixelBitMask: return L"BITMASK";
    case PixelBltOnly: return L"BLT_ONLY";
    default: return L"INVALID";
    }
}

static EFI_STATUS write_text(EFI_SYSTEM_TABLE *table, const CHAR16 *text)
{
    return table->ConOut->OutputString(table->ConOut, const_cast<CHAR16 *>(text));
}

static CHAR16 *append_dimensions(CHAR16 *next, const ModeSnapshot &mode)
{
    next = append_decimal(next, mode.width);
    *next++ = L'x';
    return append_decimal(next, mode.height);
}

static EFI_STATUS print_mode(EFI_SYSTEM_TABLE *table, const ModeSnapshot &mode)
{
    // < 80 CHAR16: 3 UINT32 values (10 digits each), fixed text and longest label.
    CHAR16 message[96] {};
    CHAR16 *next = append_text(message, L"[GOP] MODE ");
    next = append_decimal(next, mode.number);
    *next++ = L' ';
    next = append_dimensions(next, mode);
    next = append_text(next, L" format=");
    next = append_text(next, format_label(mode.format));
    next = append_text(next, L"\r\n");
    *next = 0;
    return write_text(table, message);
}

static EFI_STATUS mode_status(
    EFI_SYSTEM_TABLE *table, bool warning, const CHAR16 *operation,
    UINT32 number, EFI_STATUS status
)
{
    // < 96 CHAR16 for QueryMode/ModeInfo/FreePool and UINT32/UINT64 maxima.
    CHAR16 message[112] {};
    CHAR16 *next = append_text(message, warning ? L"[GOP] WARN " : L"[GOP] ERROR ");
    next = append_text(next, operation);
    next = append_text(next, L" mode=");
    next = append_decimal(next, number);
    next = append_text(next, L" status=0x");
    const CHAR16 hex[] = L"0123456789ABCDEF";
    for (int shift = 60; shift >= 0; shift -= 4) {
        *next++ = hex[(status >> shift) & 0xf];
    }
    next = append_text(next, L"\r\n");
    *next = 0;
    // A reporting failure cannot replace an existing fatal operation error.
    EFI_STATUS output_status = write_text(table, message);
    return warning ? output_status : status;
}

static bool target_mode(const ModeSnapshot &mode)
{
    return mode.width == 800 && mode.height == 600;
}

static EFI_STATUS enumerate_modes(
    EFI_SYSTEM_TABLE *table, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
    UINT32 max_mode, const ModeSnapshot &original, ModeSnapshot &selected
)
{
    bool found = target_mode(original);
    bool incomplete = false;
    selected = original;
    for (UINT32 number = 0; number < max_mode; ++number) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = nullptr;
        UINTN size = 0;
        EFI_STATUS status = gop->QueryMode(gop, number, &size, &info);
        if (status != EFI_SUCCESS) {
            // Failed OUT parameters do not establish ownership and may be unusable.
            incomplete = true;
            status = mode_status(table, true, L"QueryMode", number, status);
        } else {
            ModeSnapshot candidate {};
            EFI_STATUS info_status = read_info(info, size, number, candidate);
            if (info != nullptr) {
                EFI_STATUS free_status = table->BootServices->FreePool(info);
                if (free_status != EFI_SUCCESS) {
                    return mode_status(table, false, L"FreePool", number, free_status);
                }
            }
            // No QueryMode buffer survives into a console call.
            if (info_status != EFI_SUCCESS) {
                incomplete = true;
                status = mode_status(table, true, L"ModeInfo", number, info_status);
            } else {
                if (!found && target_mode(candidate)) {
                    selected = candidate;
                    found = true;
                }
                status = print_mode(table, candidate);
            }
        }
        if (status != EFI_SUCCESS) {
            return gop_error(table, L"OutputString", status);
        }
    }
    if (!found) {
        EFI_STATUS status = write_text(table, incomplete
            ? L"[GOP] FALLBACK enumeration-incomplete\r\n"
            : L"[GOP] FALLBACK target-not-found\r\n");
        if (status != EFI_SUCCESS) {
            return gop_error(table, L"OutputString", status);
        }
    }
    return EFI_SUCCESS;
}

static EFI_STATUS draw_blocks(
    EFI_SYSTEM_TABLE *system_table,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
    UINT32 width,
    UINT32 height
)
{
    if (gop->Blt == nullptr) {
        return gop_error(system_table, L"Blt", EFI_UNSUPPORTED);
    }
    if (width < 5 || height < 4) {
        return gop_error(system_table, L"Geometry", EFI_UNSUPPORTED);
    }

    const UINTN block_width = width / 5;
    const UINTN block_height = height / 4;
    const UINTN gap = width / 20;
    const UINTN left = (width - (3 * block_width + 2 * gap)) / 2;
    const UINTN top = height / 2 + height / 8;

    EFI_GRAPHICS_OUTPUT_BLT_PIXEL colors[3] {};
    colors[0].Red = 255;
    colors[1].Green = 255;
    colors[2].Blue = 255;
    const CHAR16 *operations[] = {L"Blt(red)", L"Blt(green)", L"Blt(blue)"};

    for (UINTN i = 0; i < 3; ++i) {
        EFI_STATUS status = gop->Blt(
            gop, &colors[i], EfiBltVideoFill, 0, 0,
            left + i * (block_width + gap), top,
            block_width, block_height, 0
        );
        if (status != EFI_SUCCESS) {
            return gop_error(system_table, operations[i], status);
        }
    }

    // 不換行，避免 console 在最後一列捲動畫面並移動色塊。
    CHAR16 marker[] = L"[GOP] DRAW_OK";
    EFI_STATUS status = system_table->ConOut->OutputString(system_table->ConOut, marker);
    return status == EFI_SUCCESS ? status : gop_error(system_table, L"OutputString", status);
}


static EFI_STATUS mode_work(
    EFI_SYSTEM_TABLE *table, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
    const ModeSnapshot &original, UINT32 max_mode, bool &set_attempted
)
{
    EFI_STATUS status = write_text(table, L"[GOP] FOUND\r\n");
    if (status != EFI_SUCCESS) {
        return gop_error(table, L"OutputString", status);
    }
    if (gop->QueryMode == nullptr) return gop_error(table, L"QueryMode", EFI_UNSUPPORTED);
    if (gop->SetMode == nullptr) return gop_error(table, L"SetMode", EFI_UNSUPPORTED);
    if (gop->Blt == nullptr) return gop_error(table, L"Blt", EFI_UNSUPPORTED);
    if (table->BootServices->FreePool == nullptr) return gop_error(table, L"FreePool", EFI_UNSUPPORTED);
    if (table->ConOut->ClearScreen == nullptr) return gop_error(table, L"ClearScreen", EFI_UNSUPPORTED);

    CHAR16 original_text[96] {};
    CHAR16 *next = append_text(original_text, L"[GOP] ORIGINAL mode=");
    next = append_decimal(next, original.number);
    *next++ = L' ';
    next = append_dimensions(next, original);
    next = append_text(next, L"\r\n");
    *next = 0;
    status = write_text(table, original_text);
    if (status != EFI_SUCCESS) return gop_error(table, L"OutputString", status);
    if (!mode_matches(gop, original)) return gop_error(table, L"ConsoleMode", EFI_DEVICE_ERROR);

    ModeSnapshot selected {};
    status = enumerate_modes(table, gop, max_mode, original, selected);
    if (status != EFI_SUCCESS) return status;
    if (!mode_matches(gop, original)) return gop_error(table, L"ConsoleMode", EFI_DEVICE_ERROR);

    // GraphicsConsole can retain the original geometry; clear before GOP switching.
    status = table->ConOut->ClearScreen(table->ConOut);
    if (status != EFI_SUCCESS) return gop_error(table, L"ClearScreen", status);
    if (!mode_matches(gop, original)) return gop_error(table, L"ConsoleMode", EFI_DEVICE_ERROR);

    if (gop->Mode->Mode != selected.number) {
        // Even a failed SetMode can have changed the device.
        set_attempted = true;
        status = gop->SetMode(gop, selected.number);
        if (status != EFI_SUCCESS) return gop_error(table, L"SetMode", status);
        if (!mode_matches(gop, selected)) return gop_error(table, L"ModeInfo", EFI_DEVICE_ERROR);
    }
    ModeSnapshot active {};
    if (read_mode(gop, active) != EFI_SUCCESS || !mode_matches(gop, selected)) {
        return gop_error(table, L"ModeInfo", EFI_DEVICE_ERROR);
    }

    // < 128 CHAR16 including UINT32 maxima, longest format label and terminator.
    CHAR16 summary[160] {};
    next = append_text(summary, L"[GOP] ACTIVE mode=");
    next = append_decimal(next, active.number);
    next = append_text(next, L" format=");
    next = append_text(next, format_label(active.format));
    next = append_text(next, L"\r\n[GOP] RESOLUTION ");
    next = append_dimensions(next, active);
    next = append_text(next, L"\r\nPress any key to exit...\r\n");
    *next = 0;
    status = write_text(table, summary);
    if (status != EFI_SUCCESS) return gop_error(table, L"OutputString", status);
    if (!mode_matches(gop, active)) return gop_error(table, L"ConsoleMode", EFI_DEVICE_ERROR);

    status = draw_blocks(table, gop, active.width, active.height);
    if (status != EFI_SUCCESS) return status;
    if (!mode_matches(gop, active)) return gop_error(table, L"ConsoleMode", EFI_DEVICE_ERROR);

    EFI_EVENT events[] = {table->ConIn->WaitForKey};
    UINTN index = 0;
    status = table->BootServices->WaitForEvent(1, events, &index);
    if (status != EFI_SUCCESS) return gop_error(table, L"WaitForEvent", status);
    EFI_INPUT_KEY key {};
    status = table->ConIn->ReadKeyStroke(table->ConIn, &key);
    if (status != EFI_SUCCESS) return gop_error(table, L"ReadKeyStroke", status);
    return EFI_SUCCESS;
}

static EFI_STATUS restore_mode(
    EFI_SYSTEM_TABLE *table, EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
    const ModeSnapshot &original, bool set_attempted, EFI_STATUS primary_status
)
{
    if (!set_attempted && mode_matches(gop, original)) return primary_status;

    CHAR16 message[64] {};
    CHAR16 *next = append_text(message, L"[GOP] RESTORE mode=");
    next = append_decimal(next, original.number);
    next = append_text(next, L"\r\n");
    *next = 0;
    EFI_STATUS output_status = write_text(table, message);
    if (output_status != EFI_SUCCESS) {
        gop_error(table, L"OutputString", output_status);
        if (primary_status == EFI_SUCCESS) primary_status = output_status;
    }
    EFI_STATUS restore_status = gop->SetMode == nullptr
        ? EFI_UNSUPPORTED : gop->SetMode(gop, original.number);
    if (restore_status == EFI_SUCCESS && !mode_matches(gop, original)) {
        restore_status = EFI_DEVICE_ERROR;
    }
    if (restore_status != EFI_SUCCESS) {
        gop_error(table, L"RestoreMode", restore_status);
        if (primary_status == EFI_SUCCESS) primary_status = restore_status;
    }
    // On successful restoration, no further application console calls occur.
    return primary_status;
}

extern "C"
EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    (void)image_handle;
    // Capture the original mode before any OutputString can alter console state.
    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    void *interface = nullptr;
    EFI_STATUS locate_status = system_table->BootServices->LocateProtocol(&guid, nullptr, &interface);
    auto *gop = static_cast<EFI_GRAPHICS_OUTPUT_PROTOCOL *>(interface);
    ModeSnapshot original {};
    EFI_STATUS mode_status = locate_status == EFI_SUCCESS
        ? read_mode(gop, original) : locate_status;
    bool original_known = mode_status == EFI_SUCCESS;
    UINT32 max_mode = original_known ? gop->Mode->MaxMode : 0;
    bool set_attempted = false;

    EFI_STATUS status = write_text(system_table, g_constructor_test == 1234
        ? L"[OK] Global constructor executed\r\n"
        : L"[FAIL] Global constructor NOT executed\r\n");
    if (status == EFI_SUCCESS) status = write_text(system_table, L"Hello UEFI\r\n");
    if (status != EFI_SUCCESS) {
        gop_error(system_table, L"OutputString", status);
    } else if (locate_status != EFI_SUCCESS) {
        status = gop_error(system_table, L"LocateProtocol", locate_status);
    } else if (!original_known) {
        const CHAR16 *operation = L"ModeInfo";
        if (gop != nullptr && gop->Mode != nullptr && gop->Mode->Info != nullptr
            && gop->Mode->SizeOfInfo >= sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION)
            && (gop->Mode->Info->HorizontalResolution == 0
                || gop->Mode->Info->VerticalResolution == 0)) {
            operation = L"Resolution";
        }
        status = gop_error(system_table, operation, mode_status);
    } else {
        status = mode_work(system_table, gop, original, max_mode, set_attempted);
    }
    return original_known
        ? restore_mode(system_table, gop, original, set_attempted, status) : status;
}
