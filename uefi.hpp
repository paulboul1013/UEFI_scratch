#pragma once


// ============================================================
// Basic Types
// ============================================================

using UINT8  = unsigned char;
using UINT16 = unsigned short;
using UINT32 = unsigned int;
using UINT64 = unsigned long long;
using UINTN  = unsigned long long;

using BOOLEAN = UINT8;

using CHAR16 = wchar_t;

using EFI_STATUS = UINTN;
using EFI_HANDLE = void *;
using EFI_EVENT  = void *;


#define EFI_SUCCESS 0


// 我們使用 x86_64-w64-mingw32-g++
// 整個 target 已經使用 Microsoft x64 ABI。
#define EFIAPI


// ============================================================
// EFI_TABLE_HEADER
// ============================================================

struct EFI_TABLE_HEADER {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
};


// ============================================================
// Forward Declarations
// ============================================================

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

struct EFI_RUNTIME_SERVICES;
struct EFI_BOOT_SERVICES;

struct EFI_CONFIGURATION_TABLE;


// ============================================================
// Simple Text Input Protocol
// ============================================================

struct EFI_INPUT_KEY {
    UINT16 ScanCode;
    CHAR16 UnicodeChar;
};


using EFI_INPUT_RESET =
    EFI_STATUS (EFIAPI *)(
        EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
        BOOLEAN ExtendedVerification
    );


using EFI_INPUT_READ_KEY =
    EFI_STATUS (EFIAPI *)(
        EFI_SIMPLE_TEXT_INPUT_PROTOCOL *This,
        EFI_INPUT_KEY *Key
    );


struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_INPUT_RESET Reset;
    EFI_INPUT_READ_KEY ReadKeyStroke;
    EFI_EVENT WaitForKey;
};


// ============================================================
// Simple Text Output Protocol
// ============================================================

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


struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET Reset;
    EFI_TEXT_STRING OutputString;
};


// ============================================================
// Boot Services
// ============================================================

// 這些 function pointer 在 Part 5.5 還不會呼叫。
// 目前只是用來保持 EFI_BOOT_SERVICES 正確 layout。
using EFI_GENERIC_FN =
    void (EFIAPI *)();


using EFI_WAIT_FOR_EVENT =
    EFI_STATUS (EFIAPI *)(
        UINTN NumberOfEvents,
        EFI_EVENT *Event,
        UINTN *Index
    );


struct EFI_BOOT_SERVICES {
    EFI_TABLE_HEADER Hdr;

    // Task Priority Services
    EFI_GENERIC_FN RaiseTPL;
    EFI_GENERIC_FN RestoreTPL;

    // Memory Services
    EFI_GENERIC_FN AllocatePages;
    EFI_GENERIC_FN FreePages;
    EFI_GENERIC_FN GetMemoryMap;
    EFI_GENERIC_FN AllocatePool;
    EFI_GENERIC_FN FreePool;

    // Event & Timer Services
    EFI_GENERIC_FN CreateEvent;
    EFI_GENERIC_FN SetTimer;

    EFI_WAIT_FOR_EVENT WaitForEvent;
};


// ============================================================
// EFI_SYSTEM_TABLE
// ============================================================

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


// ============================================================
// Layout Checks
// ============================================================

static_assert(
    sizeof(EFI_TABLE_HEADER) == 24
);

static_assert(
    sizeof(void *) == 8
);

static_assert(
    __builtin_offsetof(
        EFI_SIMPLE_TEXT_INPUT_PROTOCOL,
        WaitForKey
    ) == 16
);

static_assert(
    __builtin_offsetof(
        EFI_SYSTEM_TABLE,
        ConOut
    ) == 64
);

static_assert(
    __builtin_offsetof(
        EFI_BOOT_SERVICES,
        WaitForEvent
    ) == 96
);