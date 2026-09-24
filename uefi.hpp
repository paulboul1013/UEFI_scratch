#pragma once

using UINT64 = unsigned long long;
using UINT32 = unsigned int;
using UINTN = unsigned long long;

using BOOLEAN = unsigned char;
using CHAR16 = wchar_t;

using EFI_STATUS = UINTN;
using EFI_HANDLE = void *;

#define EFI_SUCCESS 0

// 我們整個 target 已經是 x86_64-w64-mingw32，
// 因此預設就是 Microsoft x64 ABI。
#define EFIAPI


// ---------------------------------------------------------
// EFI_TABLE_HEADER
// ---------------------------------------------------------

struct EFI_TABLE_HEADER {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
};


// ---------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

struct EFI_RUNTIME_SERVICES;
struct EFI_BOOT_SERVICES;
struct EFI_CONFIGURATION_TABLE;


// ---------------------------------------------------------
// Simple Text Output Protocol
// ---------------------------------------------------------

using EFI_TEXT_RESET =
    EFI_STATUS (EFIAPI *)(
        EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
        BOOLEAN ExtendedVerification
    );

using EFI_TEXT_STRING =
    EFI_STATUS (EFIAPI *)(
        EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This,
        CHAR16 *String
    );


// 我們 Part 5 只需要前兩個欄位。
// 重要的是順序必須與 UEFI specification 完全相同。
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET Reset;
    EFI_TEXT_STRING OutputString;
};


// ---------------------------------------------------------
// EFI_SYSTEM_TABLE
// ---------------------------------------------------------

struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;

    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;

    EFI_HANDLE ConsoleInHandle;
    EFI_SIMPLE_TEXT_INPUT_PROTOCOL *ConIn;

    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;

    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;

    EFI_RUNTIME_SERVICES *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;

    UINTN NumberOfTableEntries;
    EFI_CONFIGURATION_TABLE *ConfigurationTable;
};


// x86_64 layout sanity check
static_assert(sizeof(EFI_TABLE_HEADER) == 24);
static_assert(sizeof(void *) == 8);
static_assert(
    __builtin_offsetof(EFI_SYSTEM_TABLE, ConOut) == 64
);