// Test-only observer. The real firmware still performs all service calls.
// Compare borrowed protocol scalars independently of menu.cpp, at its next wait.
#include "../menu.hpp"
#include "../runtime.hpp"

namespace {

struct Snapshot {
    INT32 text_mode;
    UINTN columns;
    UINTN rows;
    INT32 attribute;
    INT32 column;
    INT32 row;
    BOOLEAN visible;
    bool graphics;
    UINT32 graphics_mode;
    UINT32 width;
    UINT32 height;
    EFI_GRAPHICS_PIXEL_FORMAT format;
};

EFI_SYSTEM_TABLE *probe_table;
EFI_START_IMAGE real_start;
EFI_WAIT_FOR_EVENT real_wait;
EFI_SET_WATCHDOG_TIMER real_watchdog;
Snapshot before_child {};
UINTN round_number;
UINTN last_watchdog = ~UINTN(0);
bool awaiting_restore;
bool observer_failed;

void serial_byte(char value)
{
    UINT8 ready;
    do {
        __asm__ volatile("inb %1, %0" : "=a"(ready) : "Nd"(0x3fd));
    } while ((ready & 0x20) == 0);
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(0x3f8));
}

void serial(const char *value)
{
    while (*value != 0) serial_byte(*value++);
}

void decimal(UINTN value)
{
    char digits[20];
    UINTN count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) serial_byte(digits[--count]);
}

void status_hex(EFI_STATUS value)
{
    const char digits[] = "0123456789ABCDEF";
    serial("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_byte(digits[(value >> shift) & 15]);
}

bool snapshot(Snapshot &saved)
{
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console = probe_table->ConOut;
    if (console == nullptr || console->Mode == nullptr || console->QueryMode == nullptr
        || console->Mode->Mode < 0) return false;
    const EFI_SIMPLE_TEXT_OUTPUT_MODE mode = *console->Mode;
    saved = {};
    saved.text_mode = mode.Mode;
    saved.attribute = mode.Attribute;
    saved.column = mode.CursorColumn;
    saved.row = mode.CursorRow;
    saved.visible = mode.CursorVisible;
    if (console->QueryMode(console, static_cast<UINTN>(mode.Mode),
                           &saved.columns, &saved.rows) != EFI_SUCCESS) return false;
    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    void *interface = nullptr;
    const EFI_STATUS status = probe_table->BootServices->LocateProtocol(
        &guid, nullptr, &interface);
    if (status == EFI_NOT_FOUND || status == EFI_UNSUPPORTED) return true;
    if (status != EFI_SUCCESS || interface == nullptr) return false;
    const auto *gop = static_cast<EFI_GRAPHICS_OUTPUT_PROTOCOL *>(interface);
    if (gop->Mode == nullptr || gop->Mode->Info == nullptr
        || gop->Mode->SizeOfInfo < sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION)
        || gop->Mode->Mode >= gop->Mode->MaxMode) return false;
    saved.graphics = true;
    saved.graphics_mode = gop->Mode->Mode;
    saved.width = gop->Mode->Info->HorizontalResolution;
    saved.height = gop->Mode->Info->VerticalResolution;
    saved.format = gop->Mode->Info->PixelFormat;
    return saved.width != 0 && saved.height != 0 && saved.format < PixelFormatMax;
}

bool same_text(const Snapshot &a, const Snapshot &b)
{
    return a.text_mode == b.text_mode && a.columns == b.columns && a.rows == b.rows;
}

bool same_graphics(const Snapshot &a, const Snapshot &b)
{
    return a.graphics == b.graphics && (!a.graphics
        || (a.graphics_mode == b.graphics_mode && a.width == b.width
            && a.height == b.height && a.format == b.format));
}

EFI_STATUS failed(const char *reason)
{
    serial("\r\n[MENU_BOOT_PROBE] FAILED round=");
    decimal(round_number);
    serial(" ");
    serial(reason);
    serial("\r\n");
    observer_failed = true;
    return EFI_DEVICE_ERROR;
}

EFI_STATUS EFIAPI observed_watchdog(UINTN timeout, UINT64 code, UINTN size, CHAR16 *data)
{
    const EFI_STATUS status = real_watchdog(timeout, code, size, data);
    if (status == EFI_SUCCESS) last_watchdog = timeout;
    serial("\r\n[MENU_BOOT_PROBE] WATCHDOG timeout=");
    decimal(timeout);
    serial(" status=");
    status_hex(status);
    serial("\r\n");
    return status;
}

EFI_STATUS EFIAPI observed_start(EFI_HANDLE child, UINTN *size, CHAR16 **data)
{
    ++round_number;
    if (!snapshot(before_child)) return failed("BeforeSnapshot");
    serial("\r\n[MENU_BOOT_PROBE] BEFORE_START round=");
    decimal(round_number);
    serial("\r\n");
    const EFI_STATUS status = real_start(child, size, data);
    Snapshot after {};
    if (!snapshot(after)) return failed("AfterChildSnapshot");
    serial("\r\n[MENU_BOOT_PROBE] CHILD_RETURN round=");
    decimal(round_number);
    serial(" status=");
    status_hex(status);
    serial(" changed_text=");
    decimal(!same_text(before_child, after));
    serial(" changed_graphics=");
    decimal(!same_graphics(before_child, after));
    serial(" changed_attribute=");
    decimal(before_child.attribute != after.attribute);
    serial(" changed_cursor=");
    decimal(before_child.column != after.column || before_child.row != after.row
            || before_child.visible != after.visible);
    serial("\r\n");
    awaiting_restore = true;
    return status;
}

EFI_STATUS EFIAPI observed_wait(UINTN count, EFI_EVENT *events, UINTN *index)
{
    if (awaiting_restore) {
        awaiting_restore = false;
        Snapshot after {};
        if (!snapshot(after)) return failed("AfterRestoreSnapshot");
        if (!same_text(before_child, after)) return failed("TextModeOrGeometry");
        if (!same_graphics(before_child, after)) return failed("GraphicsModeOrGeometry");
        if (before_child.attribute != after.attribute || before_child.column != after.column
            || before_child.row != after.row || before_child.visible != after.visible)
            return failed("ConsoleSettings");
        if (last_watchdog != 0) return failed("WatchdogNotDisabled");
        serial("\r\n[MENU_BOOT_PROBE] RESTORE_VERIFIED round=");
        decimal(round_number);
        serial(" text=");
        decimal(after.columns);
        serial("x");
        decimal(after.rows);
        serial(" attribute=");
        decimal(static_cast<UINTN>(after.attribute));
        serial(" cursor=");
        decimal(static_cast<UINTN>(after.column));
        serial(",");
        decimal(static_cast<UINTN>(after.row));
        serial(" visible=");
        decimal(after.visible);
        serial(" graphics=");
        if (after.graphics) {
            decimal(after.graphics_mode);
            serial(" ");
            decimal(after.width);
            serial("x");
            decimal(after.height);
        } else serial("absent");
        serial(" watchdog=0 before_wait=1\r\n");
    }
    return real_wait(count, events, index);
}

void fini_observer()
{
    serial("\r\n[MENU_BOOT_PROBE] RUNTIME_FINI\r\n");
}

} // namespace

extern "C" EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *table)
{
    if (table == nullptr || table->BootServices == nullptr
        || table->BootServices->StartImage == nullptr
        || table->BootServices->WaitForEvent == nullptr
        || table->BootServices->SetWatchdogTimer == nullptr
        || table->BootServices->LocateProtocol == nullptr
        || atexit(fini_observer) != 0) return EFI_DEVICE_ERROR;
    probe_table = table;
    real_start = table->BootServices->StartImage;
    real_wait = table->BootServices->WaitForEvent;
    real_watchdog = table->BootServices->SetWatchdogTimer;
    table->BootServices->StartImage = observed_start;
    table->BootServices->WaitForEvent = observed_wait;
    table->BootServices->SetWatchdogTimer = observed_watchdog;
    serial("\r\n[MENU_BOOT_PROBE] ENTRY\r\n");
    const EFI_STATUS status = menu_run(image_handle, table);
    table->BootServices->StartImage = real_start;
    table->BootServices->WaitForEvent = real_wait;
    table->BootServices->SetWatchdogTimer = real_watchdog;
    if (awaiting_restore) return failed("ReturnedWithoutNextWait");
    if (observer_failed) return EFI_DEVICE_ERROR;
    serial("\r\n[MENU_BOOT_PROBE] PARENT_RETURN status=");
    status_hex(status);
    serial("\r\n");
    return status;
}
