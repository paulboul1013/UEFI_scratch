// Test-only observer: compare real firmware state after menu_run returns.
#include "../menu.hpp"
#include "../runtime.hpp"

namespace {

struct ConsoleSnapshot {
    INT32 mode;
    INT32 attribute;
    INT32 column;
    INT32 row;
    BOOLEAN visible;
    UINTN columns;
    UINTN rows;
};

struct GraphicsSnapshot {
    UINT32 mode;
    UINT32 width;
    UINT32 height;
    EFI_GRAPHICS_PIXEL_FORMAT format;
};

EFI_SYSTEM_TABLE *probe_table;

CHAR16 *append_text(CHAR16 *next, const CHAR16 *text)
{
    while (*text != 0) {
        *next++ = *text++;
    }
    return next;
}

CHAR16 *append_number(CHAR16 *next, UINTN value)
{
    CHAR16 digits[20];
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

EFI_STATUS message(const CHAR16 *text)
{
    return probe_table->ConOut->OutputString(
        probe_table->ConOut, const_cast<CHAR16 *>(text));
}

EFI_STATUS fail(const CHAR16 *reason)
{
    message(L"\r\n[MENU_PROBE] RESTORE_FAILED ");
    message(reason);
    message(L"\r\n");
    return EFI_DEVICE_ERROR;
}

bool snapshot_console(ConsoleSnapshot &saved)
{
    const EFI_SIMPLE_TEXT_OUTPUT_MODE *mode = probe_table->ConOut->Mode;
    if (mode == nullptr || mode->Mode < 0) {
        return false;
    }
    saved = {mode->Mode, mode->Attribute, mode->CursorColumn, mode->CursorRow,
             mode->CursorVisible, 0, 0};
    return probe_table->ConOut->QueryMode(
        probe_table->ConOut, static_cast<UINTN>(mode->Mode),
        &saved.columns, &saved.rows) == EFI_SUCCESS;
}

bool snapshot_graphics(GraphicsSnapshot &saved)
{
    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    void *interface = nullptr;
    if (probe_table->BootServices->LocateProtocol(&guid, nullptr, &interface)
        != EFI_SUCCESS || interface == nullptr) {
        return false;
    }
    const EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode =
        static_cast<EFI_GRAPHICS_OUTPUT_PROTOCOL *>(interface)->Mode;
    if (mode == nullptr || mode->Info == nullptr || mode->Mode >= mode->MaxMode
        || mode->SizeOfInfo < sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION)) {
        return false;
    }
    saved = {mode->Mode, mode->Info->HorizontalResolution,
             mode->Info->VerticalResolution, mode->Info->PixelFormat};
    return saved.width != 0 && saved.height != 0 && saved.format < PixelFormatMax;
}

void fini_observer()
{
    // startup.cpp executes this registered callback only after efi_main returns.
    message(L"\r\n[MENU_PROBE] RUNTIME_FINI\r\n");
}

} // namespace

extern "C" EFI_STATUS efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE *table)
{
    if (table == nullptr || table->ConOut == nullptr || table->ConOut->Mode == nullptr
        || table->ConOut->OutputString == nullptr || table->ConOut->QueryMode == nullptr
        || table->ConOut->SetAttribute == nullptr
        || table->ConOut->SetCursorPosition == nullptr
        || table->ConOut->EnableCursor == nullptr || table->BootServices == nullptr
        || table->BootServices->LocateProtocol == nullptr) {
        return EFI_DEVICE_ERROR;
    }
    probe_table = table;
    ConsoleSnapshot caller {};
    if (!snapshot_console(caller) || caller.columns < 8 || caller.rows < 4) {
        return fail(L"InitialConsole");
    }

    // Seed distinctive settings so an unchanged default cannot pass restoration.
    // These operations use the actual console and its supported current mode.
    if (table->ConOut->SetAttribute(table->ConOut, 10) != EFI_SUCCESS
        || table->ConOut->SetCursorPosition(table->ConOut, 7, 3) != EFI_SUCCESS
        || table->ConOut->EnableCursor(table->ConOut, 1) != EFI_SUCCESS) {
        return fail(L"SeedConsole");
    }
    ConsoleSnapshot original {};
    GraphicsSnapshot graphics {};
    if (!snapshot_console(original) || !snapshot_graphics(graphics)) {
        return fail(L"SeedSnapshot");
    }
    if (original.attribute != 10 || original.column != 7 || original.row != 3
        || original.visible != 1 || atexit(fini_observer) != 0) {
        return fail(L"SeedMismatch");
    }

    // Query real firmware modes only. Do not manufacture a smaller virtual console.
    bool small_available = false;
    for (INT32 mode = 0; mode < table->ConOut->Mode->MaxMode; ++mode) {
        UINTN columns = 0;
        UINTN rows = 0;
        if (table->ConOut->QueryMode(table->ConOut, static_cast<UINTN>(mode),
                                   &columns, &rows) == EFI_SUCCESS
            && columns >= 40 && rows >= 15 && (columns < 80 || rows < 25)) {
            small_available = true;
        }
    }
    message(small_available ? L"\r\n[MENU_PROBE] SMALL_MODE_AVAILABLE\r\n"
                            : L"\r\n[MENU_PROBE] SMALL_MODE_UNAVAILABLE\r\n");
    // Printing the availability report moves the cursor. Reapply the seed before
    // entering the real menu; the snapshot above remains the expected result.
    if (table->ConOut->SetCursorPosition(table->ConOut, 7, 3) != EFI_SUCCESS) {
        return fail(L"SeedCursor");
    }
    EFI_STATUS status = menu_run(table);
    if (status != EFI_SUCCESS) {
        return fail(L"MenuStatus");
    }
    ConsoleSnapshot restored {};
    GraphicsSnapshot returned_graphics {};
    if (!snapshot_console(restored) || !snapshot_graphics(returned_graphics)) {
        return fail(L"ReturnedSnapshot");
    }
    if (restored.mode != original.mode || restored.columns != original.columns
        || restored.rows != original.rows || restored.attribute != original.attribute
        || restored.column != original.column || restored.row != original.row
        || restored.visible != original.visible) {
        return fail(L"ConsoleMismatch");
    }
    if (returned_graphics.mode != graphics.mode || returned_graphics.width != graphics.width
        || returned_graphics.height != graphics.height
        || returned_graphics.format != graphics.format) {
        return fail(L"GraphicsMismatch");
    }

    CHAR16 line[256] {};
    CHAR16 *next = append_text(line, L"\r\n[MENU_PROBE] RESTORE_VERIFIED text=");
    next = append_number(next, restored.columns);
    *next++ = L'x';
    next = append_number(next, restored.rows);
    next = append_text(next, L" attribute=");
    next = append_number(next, static_cast<UINTN>(restored.attribute));
    next = append_text(next, L" cursor=");
    next = append_number(next, static_cast<UINTN>(restored.column));
    *next++ = L',';
    next = append_number(next, static_cast<UINTN>(restored.row));
    next = append_text(next, L" visible=");
    next = append_number(next, restored.visible);
    next = append_text(next, L" graphics=");
    next = append_number(next, returned_graphics.mode);
    *next++ = L' ';
    next = append_number(next, returned_graphics.width);
    *next++ = L'x';
    next = append_number(next, returned_graphics.height);
    next = append_text(next, L" status=0\r\n");
    *next = 0;
    status = message(line);
    // Leave the original caller's console settings after collecting evidence.
    table->ConOut->SetAttribute(table->ConOut, static_cast<UINTN>(caller.attribute));
    table->ConOut->SetCursorPosition(table->ConOut, static_cast<UINTN>(caller.column),
                                   static_cast<UINTN>(caller.row));
    table->ConOut->EnableCursor(table->ConOut, caller.visible);
    return status;
}
