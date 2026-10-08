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


// 只使用 stack buffer。此程式沒有連結標準函式庫。
static CHAR16 *append_text(CHAR16 *next, const CHAR16 *text)
{
    while (*text) {
        *next++ = *text++;
    }
    return next;
}

static CHAR16 *append_decimal(CHAR16 *next, UINTN value)
{
    CHAR16 digits[20];
    UINTN count = 0;
    do {
        digits[count++] = L'0' + value % 10;
        value /= 10;
    } while (value != 0);

    while (count != 0) {
        *next++ = digits[--count];
    }
    return next;
}

static EFI_STATUS gop_error(
    EFI_SYSTEM_TABLE *system_table,
    const CHAR16 *operation,
    EFI_STATUS status
)
{
    CHAR16 message[96] {};
    CHAR16 *next = append_text(message, L"[GOP] ERROR ");
    next = append_text(next, operation);
    next = append_text(next, L" status=0x");
    const CHAR16 hex[] = L"0123456789ABCDEF";
    for (int shift = 60; shift >= 0; shift -= 4) {
        *next++ = hex[(status >> shift) & 0xf];
    }
    next = append_text(next, L"\r\n");
    *next = 0;
    system_table->ConOut->OutputString(system_table->ConOut, message);
    return status;
}

static EFI_STATUS print_resolution(
    EFI_SYSTEM_TABLE *system_table,
    UINT32 width,
    UINT32 height
)
{
    CHAR16 message[64] {};
    CHAR16 *next = append_text(message, L"[GOP] FOUND\r\n[GOP] RESOLUTION ");
    next = append_decimal(next, width);
    *next++ = L'x';
    next = append_decimal(next, height);
    next = append_text(next, L"\r\n");
    *next = 0;
    return system_table->ConOut->OutputString(system_table->ConOut, message);
}

static EFI_STATUS draw_blocks(
    EFI_SYSTEM_TABLE *system_table,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
    UINT32 width,
    UINT32 height
)
{
    if (gop->Blt == nullptr) {
        return gop_error(system_table, L"Blt", EFI_UNSUPPORTED);
    }
    if (width < 5 || height < 4) {
        return gop_error(system_table, L"Geometry", EFI_UNSUPPORTED);
    }

    const UINTN block_width = width / 5;
    const UINTN block_height = height / 4;
    const UINTN gap = width / 20;
    const UINTN left = (width - (3 * block_width + 2 * gap)) / 2;
    const UINTN top = height / 2 + height / 8;

    EFI_GRAPHICS_OUTPUT_BLT_PIXEL colors[3] {};
    colors[0].Red = 255;
    colors[1].Green = 255;
    colors[2].Blue = 255;
    const CHAR16 *operations[] = {L"Blt(red)", L"Blt(green)", L"Blt(blue)"};

    for (UINTN i = 0; i < 3; ++i) {
        EFI_STATUS status = gop->Blt(
            gop, &colors[i], EfiBltVideoFill, 0, 0,
            left + i * (block_width + gap), top,
            block_width, block_height, 0
        );
        if (status != EFI_SUCCESS) {
            return gop_error(system_table, operations[i], status);
        }
    }

    // 不換行，避免 console 在最後一列捲動畫面並移動色塊。
    CHAR16 marker[] = L"[GOP] DRAW_OK";
    return system_table->ConOut->OutputString(system_table->ConOut, marker);
}


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
        L"Hello UEFI\r\n";


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


    // LocateProtocol 是 Boot Service。回傳的 GOP 是 Protocol 介面。
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    void *interface = nullptr;
    status = system_table->BootServices->LocateProtocol(
        &gop_guid, nullptr, &interface
    );
    if (status != EFI_SUCCESS) {
        return gop_error(system_table, L"LocateProtocol", status);
    }

    auto *gop = static_cast<EFI_GRAPHICS_OUTPUT_PROTOCOL *>(interface);
    if (gop == nullptr || gop->Mode == nullptr || gop->Mode->Info == nullptr
        || gop->Mode->SizeOfInfo < sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION)) {
        return gop_error(system_table, L"ModeInfo", EFI_DEVICE_ERROR);
    }

    // Mode 與 Info 由韌體持有。本程式只讀取，不釋放或修改。
    const auto *info = gop->Mode->Info;
    const UINT32 width = info->HorizontalResolution;
    const UINT32 height = info->VerticalResolution;
    if (width == 0 || height == 0) {
        return gop_error(system_table, L"Resolution", EFI_DEVICE_ERROR);
    }
    status = print_resolution(system_table, width, height);
    if (status != EFI_SUCCESS) {
        return status;
    }

    CHAR16 prompt[] = L"Press any key to exit...\r\n";
    status = system_table->ConOut->OutputString(system_table->ConOut, prompt);
    if (status != EFI_SUCCESS) {
        return status;
    }

    // 先完成主要文字輸出，再畫色塊。之後只輸出固定成功標記。
    status = draw_blocks(system_table, gop, width, height);
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
