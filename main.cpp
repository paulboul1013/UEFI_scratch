#include "uefi.hpp"

static int g_constructor_test = 0;


class RuntimeTest {
public:

    RuntimeTest()
    {
        g_constructor_test = 1234;
    }

    ~RuntimeTest()
    {
        g_constructor_test = 0;
    }
};


RuntimeTest g_runtime_test;


extern "C"
EFI_STATUS efi_main(
    EFI_HANDLE image_handle,
    EFI_SYSTEM_TABLE *system_table
)
{
    (void)image_handle;

    if (g_constructor_test == 1234) {

        CHAR16 ok[] =
            L"[OK] Global constructor executed\r\n";

        system_table->ConOut->OutputString(
            system_table->ConOut,
            ok
        );

    } else {

        CHAR16 fail[] =
            L"[FAIL] Global constructor NOT executed\r\n";

        system_table->ConOut->OutputString(
            system_table->ConOut,
            fail
        );
    }


    // --------------------------------------------------------
    // 1. Print message
    // --------------------------------------------------------

    CHAR16 message[] =
        L"Hello UEFI\r\n"
        L"Press any key to exit...\r\n";


    EFI_STATUS status =
        system_table
            ->ConOut
            ->OutputString(
                system_table->ConOut,
                message
            );


    if (status != EFI_SUCCESS) {
        return status;
    }


    // --------------------------------------------------------
    // 2. Get keyboard event
    // --------------------------------------------------------

    EFI_EVENT events[1] = {
        system_table->ConIn->WaitForKey
    };


    UINTN index = 0;


    // --------------------------------------------------------
    // 3. Wait until keyboard event is signaled
    // --------------------------------------------------------

    status =
        system_table
            ->BootServices
            ->WaitForEvent(
                1,
                events,
                &index
            );


    if (status != EFI_SUCCESS) {
        return status;
    }


    // --------------------------------------------------------
    // 4. Read actual key
    // --------------------------------------------------------

    EFI_INPUT_KEY key {};


    status =
        system_table
            ->ConIn
            ->ReadKeyStroke(
                system_table->ConIn,
                &key
            );


    if (status != EFI_SUCCESS) {
        return status;
    }


    // --------------------------------------------------------
    // 5. Return to firmware
    // --------------------------------------------------------

    return EFI_SUCCESS;
}