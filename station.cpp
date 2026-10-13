#include "menu.hpp"

extern "C" EFI_STATUS efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table)
{
    return menu_run(image_handle, system_table);
}
