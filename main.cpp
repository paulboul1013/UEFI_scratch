#include "uefi.hpp"

extern "C"
EFI_STATUS efi_main(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *system_table
)
{
    (void)image_handle;

    CHAR16 message[] =
        L"Hello UEFI\r\n";

    system_table->ConOut->OutputString(
        system_table->ConOut,
        message
    );

    for (;;) {
        __asm__ __volatile__("hlt");
    }

    return EFI_SUCCESS;
}