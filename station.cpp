#include "menu.hpp"

extern "C" EFI_STATUS efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE *system_table)
{
    return menu_run(system_table);
}
