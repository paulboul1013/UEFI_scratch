// Test payloads only. Each variant has its own EFI image and entry point.
#include "../uefi.hpp"
#include "../runtime.hpp"

#if (defined(BOOT_CHILD_DIRTY) + defined(BOOT_CHILD_ERROR) \
     + defined(BOOT_CHILD_TEXT) + defined(BOOT_CHILD_WATCHDOG_HOLD)) != 1
#error Select exactly one BOOT_CHILD variant
#endif

namespace {

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

[[maybe_unused]] void decimal(UINTN value)
{
    char digits[20];
    UINTN count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count != 0) serial_byte(digits[--count]);
}

void fini_observer()
{
    serial("\r\n[BOOT_CHILD] RUNTIME_FINI\r\n");
}

EFI_STATUS failed(const char *reason)
{
    serial("\r\n[BOOT_CHILD] FAILED ");
    serial(reason);
    serial("\r\n");
    return EFI_DEVICE_ERROR;
}

} // namespace

extern "C" EFI_STATUS efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE *table)
{
    if (table == nullptr || table->BootServices == nullptr
        || table->ConOut == nullptr || table->ConOut->Mode == nullptr
        || atexit(fini_observer) != 0) return failed("InitialServices");
#if defined(BOOT_CHILD_TEXT)
    serial("\r\n[BOOT_CHILD] ENTRY TEXT\r\n");
    if (table->ConOut->OutputString(table->ConOut,
                                   const_cast<CHAR16 *>(L"Text child running\r\n"))
        != EFI_SUCCESS) return failed("TextOutput");
#elif defined(BOOT_CHILD_ERROR)
    serial("\r\n[BOOT_CHILD] ENTRY ERROR\r\n");
#elif defined(BOOT_CHILD_WATCHDOG_HOLD)
    serial("\r\n[BOOT_CHILD] ENTRY WATCHDOG_HOLD\r\n");
    if (table->BootServices->SetWatchdogTimer(2, 0x10000, 0, nullptr)
        != EFI_SUCCESS) return failed("WatchdogArm");
    serial("[BOOT_CHILD] WATCHDOG_ARMED seconds=2\r\n");
    EFI_EVENT events[] = {table->ConIn->WaitForKey};
    UINTN index = 0;
    for (;;) table->BootServices->WaitForEvent(1, events, &index);
#else
    serial("\r\n[BOOT_CHILD] ENTRY DIRTY\r\n");
    EFI_GUID guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    void *interface = nullptr;
    if (table->BootServices->LocateProtocol(&guid, nullptr, &interface) != EFI_SUCCESS
        || interface == nullptr) return failed("GraphicsLocate");
    auto *gop = static_cast<EFI_GRAPHICS_OUTPUT_PROTOCOL *>(interface);
    if (gop->Mode == nullptr) return failed("GraphicsMode");
    const UINT32 original_graphics = gop->Mode->Mode;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *console = table->ConOut;
    const INT32 original_text = console->Mode->Mode;
    bool text_changed = false;
    for (INT32 number = 0; number < console->Mode->MaxMode; ++number) {
        UINTN columns = 0;
        UINTN rows = 0;
        if (number != original_text
            && console->QueryMode(console, static_cast<UINTN>(number), &columns, &rows)
                == EFI_SUCCESS
            && columns >= 8 && rows >= 4
            && console->SetMode(console, static_cast<UINTN>(number)) == EFI_SUCCESS
            && console->Mode->Mode != original_text) {
            text_changed = true;
            break;
        }
    }
    if (!text_changed) return failed("NoDifferentTextMode");
    serial("[BOOT_CHILD] TEXT_CHANGED mode=");
    decimal(static_cast<UINTN>(console->Mode->Mode));
    serial("\r\n");
    bool graphics_changed = false;
    for (UINT32 number = 0; number < gop->Mode->MaxMode; ++number) {
        if (number != original_graphics && gop->SetMode(gop, number) == EFI_SUCCESS
            && gop->Mode != nullptr && gop->Mode->Mode != original_graphics) {
            graphics_changed = true;
            break;
        }
    }
    if (!graphics_changed) return failed("NoDifferentGraphicsMode");
    serial("[BOOT_CHILD] GRAPHICS_CHANGED mode=");
    decimal(gop->Mode->Mode);
    serial("\r\n");
    if (console->SetAttribute(console, 0x4e) != EFI_SUCCESS
        || console->SetCursorPosition(console, 7, 3) != EFI_SUCCESS
        || console->EnableCursor(console, 1) != EFI_SUCCESS)
        return failed("ConsoleMutation");
    serial("[BOOT_CHILD] CONSOLE_CHANGED attribute=78 cursor=7,3 visible=1\r\n");
    if (table->BootServices->SetWatchdogTimer(2, 0x10000, 0, nullptr)
        != EFI_SUCCESS) return failed("WatchdogArm");
    serial("[BOOT_CHILD] WATCHDOG_ARMED seconds=2\r\n");
#endif
    serial("[BOOT_CHILD] BEFORE_RETURN status=");
#if defined(BOOT_CHILD_ERROR)
    serial("0x8000000000000007\r\n");
    return EFI_DEVICE_ERROR;
#else
    serial("0x0000000000000000\r\n");
    return EFI_SUCCESS;
#endif
}
