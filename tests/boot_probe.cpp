// Test-only real LoadImage/StartImage probe. Linked with the production loader.
#include "../boot.hpp"
#include "../runtime.hpp"

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

void status_hex(EFI_STATUS value)
{
    const char digits[] = "0123456789ABCDEF";
    serial("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
        serial_byte(digits[(value >> shift) & 15]);
}

void fini_observer()
{
    serial("\r\n[BOOT_PROBE] RUNTIME_FINI\r\n");
}

} // namespace

extern "C" EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *table)
{
    if (table == nullptr || table->BootServices == nullptr
        || atexit(fini_observer) != 0) return EFI_DEVICE_ERROR;
    serial("\r\n[BOOT_PROBE] ENTRY\r\n");
    BootLoader loader(image_handle, table);
    const BootEntry entry {L"GOP Demo", L"\\EFI\\TOOLS\\GOP.EFI"};
    BootResult result = loader.run(entry);
    serial("\r\n[BOOT_PROBE] RETURN stage=");
    serial_byte(static_cast<char>('0' + static_cast<unsigned>(result.stage)));
    serial(" status=");
    status_hex(result.status);
    serial(" cleanup=");
    status_hex(result.cleanup_status);
    serial(loader.has_pending_image() ? " pending=1\r\n" : " pending=0\r\n");
    // Failed cleanup must not let this parent application return to firmware.
    if (loader.has_pending_image()) {
        serial("[BOOT_PROBE] FAILED PendingImage\r\n");
        for (;;) __asm__ volatile("hlt");
    }
    if (result.status != EFI_SUCCESS) return result.status;
    return result.cleanup_status;
}
