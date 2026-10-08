// Run with g++ -std=c++17 -fshort-wchar tests/gop_host.cpp -o /tmp/gop_host
#include <cassert>
#include <cstdio>
#include <string>

#include "../main.cpp"

namespace {
std::string output;
EFI_STATUS locate_status;
void *located_interface;
unsigned waits;
unsigned reads;
unsigned blts;
unsigned fail_blt;

EFI_STATUS EFIAPI fill(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *self, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixel,
    EFI_GRAPHICS_OUTPUT_BLT_OPERATION operation, UINTN sx, UINTN sy,
    UINTN x, UINTN y, UINTN width, UINTN height, UINTN delta
);

EFI_STATUS EFIAPI write_text(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *, CHAR16 *text)
{
    while (*text) {
        output += static_cast<char>(*text++);
    }
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI locate(EFI_GUID *guid, void *registration, void **result)
{
    assert(guid->Data1 == 0x9042a9de && guid->Data2 == 0x23dc && guid->Data3 == 0x4a38);
    const UINT8 tail[] = {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a};
    for (unsigned i = 0; i < 8; ++i) {
        assert(guid->Data4[i] == tail[i]);
    }
    assert(registration == nullptr);
    assert(*result == nullptr);
    *result = located_interface;
    return locate_status;
}

EFI_STATUS EFIAPI wait_key(UINTN count, EFI_EVENT *events, UINTN *index)
{
    assert(count == 1 && events[0] == reinterpret_cast<EFI_EVENT>(1));
    assert(output.find("[GOP] DRAW_OK") != std::string::npos);
    ++waits;
    *index = 0;
    return EFI_SUCCESS;
}

EFI_STATUS EFIAPI read_key(EFI_SIMPLE_TEXT_INPUT_PROTOCOL *, EFI_INPUT_KEY *key)
{
    ++reads;
    key->UnicodeChar = L' ';
    return EFI_SUCCESS;
}

EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info {};
EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode {};
EFI_GRAPHICS_OUTPUT_PROTOCOL gop {};
EFI_BOOT_SERVICES boot {};
EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console {};
EFI_SIMPLE_TEXT_INPUT_PROTOCOL keyboard {};
EFI_SYSTEM_TABLE system_table {};

EFI_STATUS EFIAPI fill(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *self, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *pixel,
    EFI_GRAPHICS_OUTPUT_BLT_OPERATION operation, UINTN sx, UINTN sy,
    UINTN x, UINTN y, UINTN width, UINTN height, UINTN delta
)
{
    assert(self == &gop && operation == EfiBltVideoFill);
    assert(sx == 0 && sy == 0 && delta == 0);
    assert(output.find("Press any key to exit...") != std::string::npos);
    assert(output.find("[GOP] DRAW_OK") == std::string::npos);
    assert(width > 0 && height > 0);
    assert(x + width <= info.HorizontalResolution);
    assert(y >= info.VerticalResolution / 2 && y + height <= info.VerticalResolution);
    assert(pixel->Reserved == 0);
    assert(pixel->Red == (blts == 0 ? 255 : 0));
    assert(pixel->Green == (blts == 1 ? 255 : 0));
    assert(pixel->Blue == (blts == 2 ? 255 : 0));
    ++blts;
    return blts == fail_blt ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

void reset()
{
    output.clear();
    waits = reads = 0;
    blts = fail_blt = 0;
    locate_status = EFI_SUCCESS;
    located_interface = &gop;
    info = {};
    info.HorizontalResolution = 1280;
    info.VerticalResolution = 800;
    mode = {};
    mode.Info = &info;
    mode.SizeOfInfo = sizeof(info);
    gop = {};
    gop.Mode = &mode;
    gop.Blt = fill;
    boot.LocateProtocol = locate;
    boot.WaitForEvent = wait_key;
    console.OutputString = write_text;
    keyboard.ReadKeyStroke = read_key;
    keyboard.WaitForKey = reinterpret_cast<EFI_EVENT>(1);
    system_table.ConOut = &console;
    system_table.ConIn = &keyboard;
    system_table.BootServices = &boot;
}

void expect_error(EFI_STATUS status, const char *operation, const char *number)
{
    assert(efi_main(nullptr, &system_table) == status);
    assert(output.find(std::string("[GOP] ERROR ") + operation + " status=" + number)
           != std::string::npos);
    assert(waits == 0 && reads == 0);
}
}

int main()
{
    reset();
    assert(efi_main(nullptr, &system_table) == EFI_SUCCESS);
    assert(output.find("[OK] Global constructor executed") != std::string::npos);
    assert(output.find("Hello UEFI") != std::string::npos);
    assert(output.find("[GOP] FOUND") != std::string::npos);
    assert(output.find("[GOP] RESOLUTION 1280x800") != std::string::npos);
    assert(waits == 1 && reads == 1);
    assert(blts == 3);

    reset();
    info.HorizontalResolution = 4294967295U;
    info.VerticalResolution = 4294967295U;
    assert(efi_main(nullptr, &system_table) == EFI_SUCCESS);
    assert(output.find("[GOP] RESOLUTION 4294967295x4294967295") != std::string::npos);

    reset();
    locate_status = EFI_NOT_FOUND;
    located_interface = nullptr;
    expect_error(EFI_NOT_FOUND, "LocateProtocol", "0x800000000000000E");

    reset();
    locate_status = EFI_DEVICE_ERROR;
    expect_error(EFI_DEVICE_ERROR, "LocateProtocol", "0x8000000000000007");

    reset();
    located_interface = nullptr;
    expect_error(EFI_DEVICE_ERROR, "ModeInfo", "0x8000000000000007");

    reset();
    gop.Mode = nullptr;
    expect_error(EFI_DEVICE_ERROR, "ModeInfo", "0x8000000000000007");

    reset();
    mode.Info = nullptr;
    expect_error(EFI_DEVICE_ERROR, "ModeInfo", "0x8000000000000007");

    reset();
    mode.SizeOfInfo = sizeof(info) - 1;
    expect_error(EFI_DEVICE_ERROR, "ModeInfo", "0x8000000000000007");

    reset();
    info.HorizontalResolution = 0;
    expect_error(EFI_DEVICE_ERROR, "Resolution", "0x8000000000000007");

    reset();
    info.VerticalResolution = 0;
    expect_error(EFI_DEVICE_ERROR, "Resolution", "0x8000000000000007");

    reset();
    gop.Blt = nullptr;
    expect_error(EFI_UNSUPPORTED, "Blt", "0x8000000000000003");

    reset();
    info.HorizontalResolution = 4;
    expect_error(EFI_UNSUPPORTED, "Geometry", "0x8000000000000003");

    reset();
    info.VerticalResolution = 3;
    expect_error(EFI_UNSUPPORTED, "Geometry", "0x8000000000000003");

    const char *operations[] = {"Blt(red)", "Blt(green)", "Blt(blue)"};
    for (unsigned i = 1; i <= 3; ++i) {
        reset();
        fail_blt = i;
        expect_error(EFI_DEVICE_ERROR, operations[i - 1], "0x8000000000000007");
        assert(blts == i);
        assert(output.find("[GOP] DRAW_OK") == std::string::npos);
    }

    std::puts("GOP host checks passed.");
}
