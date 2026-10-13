#include "menu.hpp"
#include "boot.hpp"

namespace {
constexpr UINTN normal_color = EFI_BLACK | (EFI_LIGHTGRAY << 4);
constexpr UINTN heading_color = EFI_BLUE | (EFI_LIGHTGRAY << 4);
constexpr UINTN selected_color = EFI_WHITE | (EFI_BLACK << 4);

bool is_error(EFI_STATUS status)
{
    return (status & (1ULL << 63)) != 0;
}

struct Text {
    CHAR16 value[128] {};
    UINTN length = 0;

    void append(const CHAR16 *source)
    {
        if (!source) source = L"Unknown";
        while (*source && length < 127) {
            // Firmware vendor strings cannot inject console control characters.
            CHAR16 character = *source++;
            value[length++] = character >= 32 && character <= 126 ? character : L'?';
        }
        value[length] = 0;
    }

    void number(UINTN number, UINTN base = 10, UINTN digits = 1)
    {
        CHAR16 reversed[32];
        UINTN count = 0;
        do {
            reversed[count++] = L"0123456789ABCDEF"[number % base];
            number /= base;
        } while (number && count < 32);
        while (count < digits && count < 32) reversed[count++] = L'0';
        while (count && length < 127) value[length++] = reversed[--count];
        value[length] = 0;
    }
};

struct Console {
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *out;
    EFI_SIMPLE_TEXT_OUTPUT_MODE saved;
    UINTN columns, rows;
    EFI_STATUS status = EFI_SUCCESS;
    bool attribute_changed = false, position_changed = false, visibility_changed = false;

    void record(EFI_STATUS result)
    {
        if (!is_error(status) && is_error(result)) status = result;
    }

    void line(UINTN column, UINTN row, UINTN width, const CHAR16 *source,
        UINTN color = normal_color, bool pad = false)
    {
        if (is_error(status) || column >= columns - 1 || row >= rows) return;
        // Leave the final column unused, including the cell that can scroll.
        UINTN available = columns - column - 1;
        if (width > available) width = available;
        if (width > 127) width = 127;
        CHAR16 text[128];
        UINTN count = 0;
        while (count < width && source[count]) {
            text[count] = source[count];
            ++count;
        }
        if (pad) while (count < width) text[count++] = L' ';
        text[count] = 0;
        attribute_changed = true;
        record(out->SetAttribute(out, color));
        if (is_error(status)) return;
        position_changed = true;
        record(out->SetCursorPosition(out, column, row));
        if (!is_error(status)) record(out->OutputString(out, text));
    }

    void clear()
    {
        if (is_error(status)) return;
        attribute_changed = true;
        record(out->SetAttribute(out, normal_color));
        if (is_error(status)) return;
        position_changed = true;
        record(out->ClearScreen(out));
    }

    void restore()
    {
        // Attempt every restoration; the first operation error remains primary.
        if (!out || !out->Mode || !out->SetAttribute || !out->SetCursorPosition) {
            record(EFI_UNSUPPORTED);
            return;
        }
        if (attribute_changed) record(out->SetAttribute(out, static_cast<UINTN>(saved.Attribute)));
        if (position_changed) {
            if (saved.CursorColumn >= 0 && saved.CursorRow >= 0 &&
                static_cast<UINTN>(saved.CursorColumn) < columns &&
                static_cast<UINTN>(saved.CursorRow) < rows)
                record(out->SetCursorPosition(out, static_cast<UINTN>(saved.CursorColumn),
                    static_cast<UINTN>(saved.CursorRow)));
            else record(EFI_UNSUPPORTED);
        }
        if (visibility_changed && out->EnableCursor)
            record(out->EnableCursor(out, saved.CursorVisible));
    }
};

const CHAR16 *const labels[] = {
    L"Select Language", L"Device Manager", L"Boot Manager",
    L"Boot Maintenance Manager", L"Continue", L"Reset", L"Return to Firmware"
};
const CHAR16 *const descriptions[] = {
    L"Select the language used by this application. English is available in this version.",
    L"View firmware and graphics information. Device configuration will be added later.",
    L"Choose an entry to boot now. Start a configured EFI application from this device.",
    L"View boot settings. Editing boot entries and boot order will be added later.",
    L"Boot the default entry. No default entry is configured in this version.",
    L"Restart the machine. A confirmation page opens before restart.",
    L"Exit this application and return control to the UEFI caller."
};
const MenuPage pages[] = {
    MenuPage::LanguagePage, MenuPage::DevicePage, MenuPage::BootPage,
    MenuPage::MaintenancePage, MenuPage::ContinueInfo, MenuPage::ResetConfirm,
    MenuPage::ExitRequested
};

void revision(Text &text, UINT32 revision)
{
    text.number(revision >> 16);
    text.append(L".");
    text.number(revision & 0xffff, 10, 2);
}

void wrap(Console &console, UINTN column, UINTN row, UINTN width,
    UINTN lines, const CHAR16 *source)
{
    if (width > 127) width = 127;
    for (UINTN line = 0; line < lines && *source; ++line) {
        CHAR16 text[128];
        UINTN count = 0, last_space = 0;
        while (count < width && source[count]) {
            text[count] = source[count];
            if (source[count] == L' ') last_space = count;
            ++count;
        }
        if (count == width && source[count] && last_space) count = last_space;
        text[count] = 0;
        console.line(column, row + line, width, text, heading_color);
        source += count;
        while (*source == L' ') ++source;
    }
}

void separator(Console &console, UINTN row)
{
    CHAR16 text[128];
    UINTN width = console.columns - 2;
    if (width > 127) width = 127;
    for (UINTN i = 0; i < width; ++i) text[i] = L'-';
    text[width] = 0;
    console.line(1, row, width, text);
}

struct Graphics {
    bool available = false;
    UINT32 mode = 0, width = 0, height = 0;
    const CHAR16 *format = L"Unknown";
    EFI_GRAPHICS_PIXEL_FORMAT pixel_format = PixelFormatMax;
};

Graphics graphics_info(EFI_SYSTEM_TABLE *table)
{
    Graphics graphics;
    if (!table->BootServices->LocateProtocol) return graphics;
    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = nullptr;
    EFI_STATUS result = table->BootServices->LocateProtocol(&guid, nullptr,
        reinterpret_cast<void **>(&gop));
    if (is_error(result) || !gop || !gop->Mode) return graphics;
    auto *mode = gop->Mode;
    if (!mode->Info || mode->SizeOfInfo < sizeof(*mode->Info) ||
        mode->Mode >= mode->MaxMode) return graphics;
    auto *info = mode->Info;
    if (info->Version != 0 || !info->HorizontalResolution || !info->VerticalResolution ||
        info->PixelFormat >= PixelFormatMax) return graphics;
    graphics.available = true;
    graphics.mode = mode->Mode;
    graphics.width = info->HorizontalResolution;
    graphics.height = info->VerticalResolution;
    graphics.pixel_format = info->PixelFormat;
    switch (info->PixelFormat) {
    case PixelRedGreenBlueReserved8BitPerColor: graphics.format = L"RGB"; break;
    case PixelBlueGreenRedReserved8BitPerColor: graphics.format = L"BGR"; break;
    case PixelBitMask: graphics.format = L"BitMask"; break;
    case PixelBltOnly: graphics.format = L"BltOnly"; break;
    default: break;
    }
    return graphics;
}

struct DisplaySnapshot {
    EFI_SIMPLE_TEXT_OUTPUT_MODE text {};
    UINTN columns = 0, rows = 0;
    Graphics graphics;
};

bool usable_console(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *out)
{
    return out && out->Mode && out->QueryMode && out->SetAttribute &&
        out->SetCursorPosition && out->ClearScreen && out->OutputString;
}

EFI_STATUS capture_display(EFI_SYSTEM_TABLE *table, DisplaySnapshot &saved,
    bool launching)
{
    auto *out = table->ConOut;
    if (!usable_console(out) || (launching && !out->SetMode)) return EFI_UNSUPPORTED;
    saved.text = *out->Mode;
    if (saved.text.Mode < 0 || saved.text.Mode >= saved.text.MaxMode)
        return EFI_UNSUPPORTED;
    EFI_STATUS status = out->QueryMode(out, static_cast<UINTN>(saved.text.Mode),
        &saved.columns, &saved.rows);
    if (is_error(status)) return status;
    if (!saved.columns || !saved.rows || saved.columns > 0x7fffffff ||
        saved.rows > 0x7fffffff || saved.text.CursorColumn < 0 || saved.text.CursorRow < 0 ||
        static_cast<UINTN>(saved.text.CursorColumn) >= saved.columns ||
        static_cast<UINTN>(saved.text.CursorRow) >= saved.rows) return EFI_UNSUPPORTED;
    saved.graphics = graphics_info(table);
    if (launching && saved.graphics.available) {
        EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
        EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = nullptr;
        status = table->BootServices->LocateProtocol(&guid, nullptr,
            reinterpret_cast<void **>(&gop));
        if (is_error(status)) return status;
        if (!gop || !gop->SetMode) return EFI_UNSUPPORTED;
    }
    return EFI_SUCCESS;
}

EFI_STATUS verify_display(EFI_SYSTEM_TABLE *table, const DisplaySnapshot &saved,
    Console &console)
{
    DisplaySnapshot current;
    EFI_STATUS status = capture_display(table, current, false);
    if (is_error(status)) return status;
    // Update geometry only from fresh firmware data, including failure paths.
    console.out = table->ConOut;
    console.columns = current.columns;
    console.rows = current.rows;
    if (current.text.Mode != saved.text.Mode || current.columns != saved.columns ||
        current.rows != saved.rows) return EFI_DEVICE_ERROR;
    const auto &a = current.graphics;
    const auto &b = saved.graphics;
    if (b.available && (!a.available || a.mode != b.mode || a.width != b.width ||
        a.height != b.height || a.pixel_format != b.pixel_format)) return EFI_DEVICE_ERROR;
    return EFI_SUCCESS;
}

EFI_STATUS restore_modes(EFI_SYSTEM_TABLE *table, const DisplaySnapshot &saved,
    Console &console)
{
    EFI_STATUS first = EFI_SUCCESS;
    auto record = [&](EFI_STATUS status) {
        if (!is_error(first) && is_error(status)) first = status;
    };
    Graphics current = graphics_info(table);
    if (saved.graphics.available && (!current.available || current.mode != saved.graphics.mode ||
        current.width != saved.graphics.width || current.height != saved.graphics.height ||
        current.pixel_format != saved.graphics.pixel_format)) {
        EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
        EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = nullptr;
        EFI_STATUS status = table->BootServices->LocateProtocol
            ? table->BootServices->LocateProtocol(&guid, nullptr,
                reinterpret_cast<void **>(&gop)) : EFI_UNSUPPORTED;
        record(status);
        if (!is_error(status)) record(gop && gop->SetMode
            ? gop->SetMode(gop, saved.graphics.mode) : EFI_UNSUPPORTED);
    }
    // Reacquire ConOut after GOP SetMode; console SetMode can itself affect GOP.
    auto *out = table->ConOut;
    if (!usable_console(out)) record(EFI_UNSUPPORTED);
    else if (out->Mode->Mode != saved.text.Mode) record(out->SetMode
        ? out->SetMode(out, static_cast<UINTN>(saved.text.Mode)) : EFI_UNSUPPORTED);
    record(verify_display(table, saved, console));
    return first;
}

void header(Console &console, EFI_SYSTEM_TABLE *table, const Graphics &graphics)
{
    console.clear();
    console.line(1, 0, console.columns - 2, L"UEFI Station 0.2", heading_color);
    Text firmware;
    firmware.append(L"Firmware: ");
    firmware.append(table->FirmwareVendor);
    console.line(1, 1, console.columns - 2, firmware.value);
    Text mode;
    mode.append(L"UEFI: ");
    revision(mode, table->Hdr.Revision);
    mode.append(L"   Text: ");
    mode.number(console.columns);
    mode.append(L"x");
    mode.number(console.rows);
    mode.append(L"   Graphics: ");
    if (graphics.available) {
        mode.number(graphics.width);
        mode.append(L"x");
        mode.number(graphics.height);
    } else mode.append(L"unavailable");
    console.line(1, 2, console.columns - 2, mode.value);
    separator(console, 3);
    separator(console, console.rows - 3);
}

void device_page(Console &console, EFI_SYSTEM_TABLE *table, const Graphics &graphics)
{
    Text vendor;
    vendor.append(L"Firmware Vendor: ");
    vendor.append(table->FirmwareVendor);
    console.line(1, 5, console.columns - 2, vendor.value);
    Text firmware;
    firmware.append(L"Firmware Revision: 0x");
    firmware.number(table->FirmwareRevision, 16, 8);
    console.line(1, 6, console.columns - 2, firmware.value);
    Text uefi;
    uefi.append(L"UEFI Revision: ");
    revision(uefi, table->Hdr.Revision);
    console.line(1, 7, console.columns - 2, uefi.value);
    Text text_mode;
    text_mode.append(L"Text Mode: ");
    text_mode.number(console.columns);
    text_mode.append(L"x");
    text_mode.number(console.rows);
    console.line(1, 8, console.columns - 2, text_mode.value);
    if (!graphics.available) {
        console.line(1, 9, console.columns - 2, L"Graphics unavailable");
        return;
    }
    Text mode;
    mode.append(L"Graphics Mode: ");
    mode.number(graphics.mode);
    console.line(1, 9, console.columns - 2, mode.value);
    Text resolution;
    resolution.append(L"Resolution: ");
    resolution.number(graphics.width);
    resolution.append(L"x");
    resolution.number(graphics.height);
    console.line(1, 10, console.columns - 2, resolution.value);
    Text format;
    format.append(L"Pixel Format: ");
    format.append(graphics.format);
    console.line(1, 11, console.columns - 2, format.value);
}

struct BootView {
    UINTN selected = 0;
    bool attempted = false;
    BootResult result {BootStage::Prepare, EFI_SUCCESS, EFI_SUCCESS};
    EFI_STATUS restoration = EFI_SUCCESS;
};

const CHAR16 *boot_notice(const BootResult &result)
{
    if (!is_error(result.status)) return L"Image returned";
    switch (result.stage) {
    case BootStage::Prepare: return L"Prepare failed";
    case BootStage::Load: return L"Load failed";
    case BootStage::CheckImage: return L"Image check failed";
    case BootStage::Start: return L"Start failed";
    case BootStage::Cleanup: return L"Cleanup failed";
    }
    return L"Boot failed";
}

void status_line(Console &console, UINTN row, const CHAR16 *label, EFI_STATUS status)
{
    Text text;
    text.append(label);
    text.append(L"0x");
    text.number(status, 16, 16);
    console.line(1, row, console.columns - 2, text.value);
}

void draw(Console &console, EFI_SYSTEM_TABLE *table, MenuPage page,
    UINTN selected, UINTN confirmation, const CHAR16 *notice, const BootView &boot)
{
    Graphics graphics = graphics_info(table);
    header(console, table, graphics);
    if (page == MenuPage::FrontPage) {
        bool full = console.columns >= 80 && console.rows >= 25;
        for (UINTN i = 0; i < 7; ++i) {
            Text item;
            item.append(i == selected ? L"> " : L"  ");
            item.append(labels[i]);
            console.line(1, 4 + i, full ? 33 : console.columns - 2, item.value,
                i == selected ? selected_color : normal_color, true);
            if (full) console.line(35, 4 + i, 1, L"|");
        }
        wrap(console, full ? 38 : 1, full ? 4 : 11,
            full ? console.columns - 40 : console.columns - 2,
            full ? console.rows - 8 : console.rows - 14, descriptions[selected]);
        console.line(1, console.rows - 2, console.columns - 2,
            full ? L"Up/Down: Select    Enter: Open    Esc: Back / Return"
                 : L"Up/Down: Select Enter: Open Esc: Exit", heading_color);
        return;
    }
    console.line(1, 4, console.columns - 2,
        page == MenuPage::ResetConfirm ? L"Reset" : labels[selected], heading_color);
    if (page == MenuPage::ResetConfirm) {
        console.line(1, 6, console.columns - 2, L"Restart the machine?");
        console.line(1, 8, 16, confirmation == 0 ? L"> Cancel" : L"  Cancel",
            confirmation == 0 ? selected_color : normal_color, true);
        console.line(1, 9, 16, confirmation == 1 ? L"> Restart" : L"  Restart",
            confirmation == 1 ? selected_color : normal_color, true);
        if (notice) console.line(1, 11, console.columns - 2, notice);
        console.line(1, console.rows - 2, console.columns - 2,
            console.columns >= 80 ? L"Up/Down: Select  Enter: Confirm  Esc: Back"
                : L"Up/Down Select Enter Confirm Esc Back", heading_color);
    } else if (page == MenuPage::RecoveryPage) {
        console.line(1, 5, console.columns - 2, L"Image cleanup required");
        console.line(1, 6, console.columns - 2,
            confirmation == 0 ? L"> Retry cleanup" : L"  Retry cleanup",
            confirmation == 0 ? selected_color : normal_color);
        console.line(1, 7, console.columns - 2,
            confirmation == 1 ? L"> Reset" : L"  Reset",
            confirmation == 1 ? selected_color : normal_color);
        status_line(console, 9, L"Status: ", boot.result.status);
        status_line(console, 10, L"Cleanup: ", boot.result.cleanup_status);
        if (notice) console.line(1, 11, console.columns - 2, notice);
        console.line(1, console.rows - 2, console.columns - 2,
            L"Up/Down: Select  Enter: Confirm", heading_color);
    } else {
        switch (page) {
        case MenuPage::LanguagePage:
            console.line(1, 6, console.columns - 2, L"Current language: English");
            console.line(1, 8, console.columns - 2, L"Traditional Chinese: Not available yet");
            break;
        case MenuPage::DevicePage:
            device_page(console, table, graphics);
            break;
        case MenuPage::BootPage:
        {
            UINTN count = 0;
            const BootEntry *entries = boot_entries(&count);
            for (UINTN i = 0; i < count && i < console.rows - 12; ++i) {
                Text item;
                item.append(i == boot.selected ? L"> " : L"  ");
                item.append(entries[i].label);
                console.line(1, 5 + i, console.columns - 2, item.value,
                    i == boot.selected ? selected_color : normal_color);
            }
            if (boot.selected < count) {
                Text path;
                path.append(L"Path: ");
                path.append(entries[boot.selected].path);
                console.line(1, 7, console.columns - 2, path.value);
            }
            if (boot.attempted) {
                console.line(1, 9, console.columns - 2, boot_notice(boot.result));
                status_line(console, 10, L"Status: ", boot.result.status);
                if (is_error(boot.result.cleanup_status))
                    status_line(console, 11, L"Cleanup: ", boot.result.cleanup_status);
            }
            break;
        }
        case MenuPage::MaintenancePage:
            console.line(1, 6, console.columns - 2, L"Default Boot Entry: Not configured");
            console.line(1, 8, console.columns - 2, L"Boot settings: Not available yet");
            break;
        case MenuPage::ContinueInfo:
            console.line(1, 6, console.columns - 2, L"No default boot entry configured");
            console.line(1, 8, console.columns - 2, L"Default entry boot: Not available yet");
            break;
        default: break;
        }
        console.line(1, console.rows - 2, console.columns - 2,
            page != MenuPage::BootPage ? L"Esc: Back" : console.columns >= 80
                ? L"Up/Down: Select  Enter: Start  Esc: Back"
                : L"Up/Down: Select Enter: Start Esc: Back", heading_color);
    }
}

EFI_STATUS restore_display(EFI_SYSTEM_TABLE *table, const DisplaySnapshot &saved,
    Console &console)
{
    EFI_STATUS first = restore_modes(table, saved, console);
    auto *out = table->ConOut;
    if (!usable_console(out)) return is_error(first) ? first : EFI_UNSUPPORTED;
    auto record = [&](EFI_STATUS status) {
        if (!is_error(first) && is_error(status)) first = status;
    };
    record(out->SetAttribute(out, static_cast<UINTN>(saved.text.Attribute)));
    // Never position a cursor with stale geometry after a failed mode restore.
    DisplaySnapshot current;
    EFI_STATUS queried = capture_display(table, current, false);
    record(queried);
    if (!is_error(queried) && saved.text.CursorColumn >= 0 && saved.text.CursorRow >= 0 &&
        static_cast<UINTN>(saved.text.CursorColumn) < current.columns &&
        static_cast<UINTN>(saved.text.CursorRow) < current.rows)
        record(out->SetCursorPosition(out, static_cast<UINTN>(saved.text.CursorColumn),
            static_cast<UINTN>(saved.text.CursorRow)));
    if (out->EnableCursor) {
        EFI_STATUS status = out->EnableCursor(out, saved.text.CursorVisible);
        if (status != EFI_UNSUPPORTED) record(status);
        if (!is_error(status) && table->ConOut && table->ConOut->Mode &&
            table->ConOut->Mode->CursorVisible != saved.text.CursorVisible)
            record(EFI_DEVICE_ERROR);
    }
    // A successful setter alone is not proof that firmware restored the value.
    out = table->ConOut;
    if (!usable_console(out)) record(EFI_UNSUPPORTED);
    else if (out->Mode->Attribute != saved.text.Attribute ||
        out->Mode->CursorColumn != saved.text.CursorColumn ||
        out->Mode->CursorRow != saved.text.CursorRow) record(EFI_DEVICE_ERROR);
    record(verify_display(table, saved, console));
    return first;
}

[[noreturn]] void controlled_stop(EFI_SYSTEM_TABLE *table, EFI_STATUS primary,
    EFI_STATUS cleanup)
{
    // A pending child prevents returning to firmware. This path is reserved for
    // permanent input/console failure when even the recovery UI is unavailable.
    auto *out = table->ConOut;
    if (out && out->OutputString) {
        Text message;
        message.append(L"Recovery unavailable; child still owned. Status: 0x");
        message.number(primary, 16, 16);
        message.append(L" Cleanup: 0x");
        message.number(cleanup, 16, 16);
        // Direct text output uses the firmware's current cursor and geometry.
        // Do not attempt positioning with dimensions that could not be queried.
        out->OutputString(out, message.value);
    }
    for (;;) __asm__ volatile("pause");
}
}

EFI_STATUS menu_run(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *table)
{
    if (!table) return EFI_INVALID_PARAMETER;
    auto *out = table->ConOut;
    auto *in = table->ConIn;
    if (!out || !out->Mode || !out->QueryMode || !out->SetAttribute ||
        !out->SetCursorPosition || !out->ClearScreen || !out->OutputString ||
        !in || !in->ReadKeyStroke || !in->WaitForKey || !table->BootServices ||
        !table->BootServices->WaitForEvent) return EFI_UNSUPPORTED;
    if (out->Mode->Mode < 0 || out->Mode->Mode >= out->Mode->MaxMode) return EFI_UNSUPPORTED;
    UINTN columns = 0, rows = 0;
    EFI_STATUS result = out->QueryMode(out, static_cast<UINTN>(out->Mode->Mode), &columns, &rows);
    if (is_error(result)) return result;
    if (!columns || !rows || columns > 0x7fffffff || rows > 0x7fffffff ||
        out->Mode->CursorColumn < 0 || out->Mode->CursorRow < 0 ||
        static_cast<UINTN>(out->Mode->CursorColumn) >= columns ||
        static_cast<UINTN>(out->Mode->CursorRow) >= rows) return EFI_UNSUPPORTED;

    Console console {out, *out->Mode, columns, rows};
    DisplaySnapshot caller;
    result = capture_display(table, caller, false);
    if (is_error(result)) return result;
    BootLoader loader(image_handle, table);
    if (columns < 40 || rows < 15) {
        console.line(0, 0, columns - 1, L"Text mode too small (need 40x15).");
        console.record(EFI_UNSUPPORTED);
        console.restore();
        return console.status;
    }
    // Boot managers arm a watchdog before StartImage. An interactive menu may
    // wait indefinitely; EFI_UNSUPPORTED specifically means no watchdog exists.
    result = table->BootServices->SetWatchdogTimer
        ? table->BootServices->SetWatchdogTimer(0, 0, 0, nullptr) : EFI_UNSUPPORTED;
    if (!table->BootServices->SetWatchdogTimer ||
        (is_error(result) && result != EFI_UNSUPPORTED)) {
        console.line(1, 0, columns - 2, L"Unable to disable boot watchdog.");
        // The watchdog error preceded any diagnostic output error.
        console.status = result;
        console.restore();
        return console.status;
    }
    if (out->EnableCursor) {
        // A failing method can have changed state before reporting failure.
        console.visibility_changed = true;
        result = out->EnableCursor(out, 0);
        if (result == EFI_UNSUPPORTED) console.visibility_changed = false;
        else console.record(result);
    }
    MenuPage page = MenuPage::FrontPage;
    MenuPage reset_origin = MenuPage::FrontPage;
    UINTN selected = 0, confirmation = 0;
    bool redraw = true;
    const CHAR16 *notice = nullptr;
    EFI_STATUS action_status = EFI_SUCCESS;
    EFI_STATUS fatal_status = EFI_SUCCESS;
    BootView boot;
    DisplaySnapshot menu_display;
    bool verify_redraw = false, child_attempted = false;
    auto stop = [&]() {
        controlled_stop(table, is_error(fatal_status) ? fatal_status :
            is_error(boot.result.status) ? boot.result.status : console.status,
            boot.result.cleanup_status);
    };
    auto fatal = [&](EFI_STATUS status) {
        // A recovery error can occur after its final cleanup released the child.
        // Keep that attempt's primary error through the entire recovery path.
        if ((loader.has_pending_image() || page == MenuPage::RecoveryPage ||
             (page == MenuPage::ResetConfirm && reset_origin == MenuPage::RecoveryPage)) &&
            is_error(boot.result.status)) status = boot.result.status;
        if (!is_error(fatal_status)) fatal_status = status;
        if (loader.has_pending_image()) {
            EFI_STATUS cleanup = loader.retry_cleanup();
            if (!is_error(boot.result.cleanup_status) && is_error(cleanup))
                boot.result.cleanup_status = cleanup;
        }
        if (!loader.has_pending_image()) {
            page = MenuPage::ExitRequested;
            return;
        }
        // Rendering recovery is allowed only with freshly validated geometry.
        DisplaySnapshot current;
        if (is_error(capture_display(table, current, false)) ||
            current.columns < 40 || current.rows < 15) stop();
        console.out = table->ConOut;
        console.columns = current.columns;
        console.rows = current.rows;
        console.status = EFI_SUCCESS;
        page = MenuPage::RecoveryPage;
        confirmation = 0;
        redraw = true;
    };
    if (is_error(console.status)) fatal(console.status);
    while (page != MenuPage::ExitRequested) {
        if (redraw) draw(console, table, page, selected, confirmation, notice, boot);
        if (is_error(console.status)) {
            // A second failed recovery render cannot provide usable controls.
            if (page == MenuPage::RecoveryPage) {
                loader.retry_cleanup();
                if (loader.has_pending_image()) stop();
            }
            fatal(console.status);
            continue;
        }
        if (verify_redraw) {
            verify_redraw = false;
            result = verify_display(table, menu_display, console);
            if (is_error(result)) {
                fatal(is_error(boot.result.status) ? boot.result.status : result);
                continue;
            }
        }
        redraw = false;
        // The child may replace the input protocol, too.
        in = table->ConIn;
        if (!in || !in->ReadKeyStroke || !in->WaitForKey) {
            loader.retry_cleanup();
            if (loader.has_pending_image()) stop();
            fatal(EFI_UNSUPPORTED);
            continue;
        }
        UINTN index = 0;
        EFI_EVENT event = in->WaitForKey;
        result = table->BootServices->WaitForEvent(1, &event, &index);
        if (is_error(result) || index != 0) {
            result = is_error(result) ? result : EFI_DEVICE_ERROR;
            if (page == MenuPage::RecoveryPage) {
                loader.retry_cleanup();
                if (loader.has_pending_image()) stop();
            }
            fatal(result);
            continue;
        }
        EFI_INPUT_KEY key {};
        result = in->ReadKeyStroke(in, &key);
        if (result == EFI_NOT_READY) continue;
        if (is_error(result)) { fatal(result); continue; }
        if (key.ScanCode == SCAN_ESC || (key.ScanCode == 0 && key.UnicodeChar == 27)) {
            if (page == MenuPage::RecoveryPage) continue;
            page = page == MenuPage::ResetConfirm ? reset_origin :
                page == MenuPage::FrontPage ? MenuPage::ExitRequested : MenuPage::FrontPage;
            redraw = true;
        } else if (key.UnicodeChar == 0 && (key.ScanCode == SCAN_UP || key.ScanCode == SCAN_DOWN)) {
            UINTN count = 0;
            boot_entries(&count);
            UINTN *choice = page == MenuPage::FrontPage ? &selected :
                page == MenuPage::BootPage ? &boot.selected : &confirmation;
            UINTN last = page == MenuPage::FrontPage ? 6 :
                page == MenuPage::BootPage ? (count ? count - 1 : 0) : 1;
            if (page != MenuPage::FrontPage && page != MenuPage::ResetConfirm &&
                page != MenuPage::BootPage && page != MenuPage::RecoveryPage) continue;
            UINTN previous = *choice;
            if (key.ScanCode == SCAN_UP && *choice > 0) --*choice;
            if (key.ScanCode == SCAN_DOWN && *choice < last) ++*choice;
            redraw = previous != *choice;
        } else if (key.ScanCode == 0 && key.UnicodeChar == L'\r') {
            if (page == MenuPage::FrontPage) {
                page = pages[selected];
                confirmation = 0;
                notice = nullptr;
                reset_origin = MenuPage::FrontPage;
                redraw = true;
            } else if (page == MenuPage::BootPage) {
                UINTN count = 0;
                const BootEntry *entries = boot_entries(&count);
                if (boot.selected >= count) continue;
                boot.attempted = true;
                result = capture_display(table, menu_display, true);
                if (is_error(result)) {
                    boot.result = {BootStage::Prepare, result, EFI_SUCCESS};
                    redraw = true;
                    continue;
                }
                child_attempted = true;
                boot.result = loader.run(entries[boot.selected]);
                boot.restoration = restore_display(table, menu_display, console);
                if (is_error(boot.restoration) || !loader.watchdog_disabled()) {
                    EFI_STATUS first = is_error(boot.result.status) ? boot.result.status :
                        is_error(boot.result.cleanup_status) ? boot.result.cleanup_status : boot.restoration;
                    fatal(first);
                    continue;
                }
                verify_redraw = true;
                if (loader.has_pending_image()) {
                    page = MenuPage::RecoveryPage;
                    confirmation = 0;
                }
                redraw = true;
            } else if (page == MenuPage::RecoveryPage && confirmation == 0) {
                result = loader.retry_cleanup();
                if (!is_error(boot.result.cleanup_status) && is_error(result))
                    boot.result.cleanup_status = result;
                if (!loader.has_pending_image()) page = is_error(fatal_status)
                    ? MenuPage::ExitRequested : MenuPage::BootPage;
                redraw = true;
            } else if (page == MenuPage::RecoveryPage) {
                reset_origin = MenuPage::RecoveryPage;
                page = MenuPage::ResetConfirm;
                confirmation = 0;
                notice = nullptr;
                redraw = true;
            } else if (page == MenuPage::ResetConfirm && confirmation == 0) {
                page = reset_origin;
                redraw = true;
            } else if (page == MenuPage::ResetConfirm) {
                if (!table->RuntimeServices || !table->RuntimeServices->ResetSystem) {
                    notice = L"Restart unavailable";
                    if (reset_origin == MenuPage::RecoveryPage) {
                        page = MenuPage::RecoveryPage;
                        confirmation = 0;
                    }
                } else {
                    table->RuntimeServices->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, nullptr);
                    notice = L"Restart failed: ResetSystem returned.";
                    action_status = EFI_DEVICE_ERROR;
                }
                redraw = true;
            }
        }
    }
    if (loader.has_pending_image()) stop();
    if (child_attempted) {
        result = restore_modes(table, caller, console);
        if (!is_error(fatal_status) && is_error(result)) fatal_status = result;
    }
    console.out = table->ConOut;
    DisplaySnapshot final_display;
    bool safe_console = !is_error(capture_display(table, final_display, false));
    if (safe_console) {
        console.columns = final_display.columns;
        console.rows = final_display.rows;
    }
    if (is_error(fatal_status) && safe_console && console.columns >= 40 && console.rows >= 15) {
        console.status = EFI_SUCCESS;
        console.line(1, console.rows - 4, console.columns - 2, L"Menu error: returning to firmware.");
    }
    if (is_error(fatal_status)) console.status = fatal_status;
    // A ResetSystem return is an earlier failure than any later input/cleanup.
    if (is_error(action_status)) console.status = action_status;
    if (safe_console) console.restore();
    else console.record(EFI_UNSUPPORTED);
    return console.status;
}
