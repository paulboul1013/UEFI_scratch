#include "uefi.hpp"

extern "C"
EFI_STATUS efi_main(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *system_table
);

extern "C"
EFI_STATUS _start(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *system_table
)
{
    return efi_main(
        image_handle,
        system_table
    );
}