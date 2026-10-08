#include "uefi.hpp"
#include "runtime.hpp"


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
    // --------------------------------------------------------
    // Initialize C/C++ runtime
    // --------------------------------------------------------

    runtime_init();


    // --------------------------------------------------------
    // Run application
    // --------------------------------------------------------

    EFI_STATUS status =
        efi_main(
            image_handle,
            system_table
        );


    // --------------------------------------------------------
    // Cleanup C/C++ runtime
    // --------------------------------------------------------

    runtime_fini();


    return status;
}