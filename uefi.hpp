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
using EFI_PHYSICAL_ADDRESS = UINT64;

struct EFI_GUID {
    UINT32 Data1;
    UINT16 Data2;
    UINT16 Data3;
    UINT8 Data4[8];
};


#define EFI_SUCCESS 0

constexpr EFI_STATUS EFI_UNSUPPORTED = (1ULL << 63) | 3;
constexpr EFI_STATUS EFI_DEVICE_ERROR = (1ULL << 63) | 7;
constexpr EFI_STATUS EFI_NOT_FOUND = (1ULL << 63) | 14;


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
struct EFI_GRAPHICS_OUTPUT_PROTOCOL;

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
// Graphics Output Protocol (GOP)
// ============================================================

constexpr EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {
    0x9042a9de, 0x23dc, 0x4a38,
    {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
};

enum EFI_GRAPHICS_PIXEL_FORMAT : UINT32 {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
};

struct EFI_PIXEL_BITMASK {
    UINT32 RedMask;
    UINT32 GreenMask;
    UINT32 BlueMask;
    UINT32 ReservedMask;
};

struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
};

// BLT buffer 的 byte 順序固定為 Blue、Green、Red、Reserved。
// 這個順序不受目前 framebuffer 的 PixelFormat 影響。
struct EFI_GRAPHICS_OUTPUT_BLT_PIXEL {
    UINT8 Blue;
    UINT8 Green;
    UINT8 Red;
    UINT8 Reserved;
};

enum EFI_GRAPHICS_OUTPUT_BLT_OPERATION : UINT32 {
    EfiBltVideoFill,
    EfiBltVideoToBltBuffer,
    EfiBltBufferToVideo,
    EfiBltVideoToVideo,
    EfiGraphicsOutputBltOperationMax
};

using EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE =
    EFI_STATUS (EFIAPI *)(
        EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
        UINT32 ModeNumber,
        UINTN *SizeOfInfo,
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info
    );

using EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE =
    EFI_STATUS (EFIAPI *)(
        EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
        UINT32 ModeNumber
    );

using EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT =
    EFI_STATUS (EFIAPI *)(
        EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
        EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer,
        EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
        UINTN SourceX,
        UINTN SourceY,
        UINTN DestinationX,
        UINTN DestinationY,
        UINTN Width,
        UINTN Height,
        UINTN Delta
    );

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE SetMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};


// ============================================================
// Boot Services
// ============================================================

// 未使用的 function pointer 只用來保持正確 layout。
using EFI_GENERIC_FN =
    void (EFIAPI *)();


using EFI_WAIT_FOR_EVENT =
    EFI_STATUS (EFIAPI *)(
        UINTN NumberOfEvents,
        EFI_EVENT *Event,
        UINTN *Index
    );

using EFI_LOCATE_PROTOCOL =
    EFI_STATUS (EFIAPI *)(
        EFI_GUID *Protocol,
        void *Registration,
        void **Interface
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
    EFI_GENERIC_FN SignalEvent;
    EFI_GENERIC_FN CloseEvent;
    EFI_GENERIC_FN CheckEvent;

    // Protocol Handler Services
    EFI_GENERIC_FN InstallProtocolInterface;
    EFI_GENERIC_FN ReinstallProtocolInterface;
    EFI_GENERIC_FN UninstallProtocolInterface;
    EFI_GENERIC_FN HandleProtocol;
    void *Reserved;
    EFI_GENERIC_FN RegisterProtocolNotify;
    EFI_GENERIC_FN LocateHandle;
    EFI_GENERIC_FN LocateDevicePath;
    EFI_GENERIC_FN InstallConfigurationTable;

    // Image Services
    EFI_GENERIC_FN LoadImage;
    EFI_GENERIC_FN StartImage;
    EFI_GENERIC_FN Exit;
    EFI_GENERIC_FN UnloadImage;
    EFI_GENERIC_FN ExitBootServices;

    // Miscellaneous Services
    EFI_GENERIC_FN GetNextMonotonicCount;
    EFI_GENERIC_FN Stall;
    EFI_GENERIC_FN SetWatchdogTimer;

    // Driver Support Services
    EFI_GENERIC_FN ConnectController;
    EFI_GENERIC_FN DisconnectController;

    // Open and Close Protocol Services
    EFI_GENERIC_FN OpenProtocol;
    EFI_GENERIC_FN CloseProtocol;
    EFI_GENERIC_FN OpenProtocolInformation;
    EFI_GENERIC_FN ProtocolsPerHandle;
    EFI_GENERIC_FN LocateHandleBuffer;
    EFI_LOCATE_PROTOCOL LocateProtocol;
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

// GOP 與 Boot Services 的 x86_64 ABI 驗收值。
static_assert(sizeof(EFI_GUID) == 16);
static_assert(sizeof(EFI_GRAPHICS_PIXEL_FORMAT) == 4);
static_assert(sizeof(EFI_GRAPHICS_OUTPUT_BLT_OPERATION) == 4);
static_assert(sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL) == 4);
static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, LocateProtocol) == 320);
static_assert(__builtin_offsetof(EFI_GRAPHICS_OUTPUT_PROTOCOL, Blt) == 16);
static_assert(__builtin_offsetof(EFI_GRAPHICS_OUTPUT_PROTOCOL, Mode) == 24);
static_assert(sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL) == 32);
static_assert(sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION) == 36);
static_assert(__builtin_offsetof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE, Info) == 8);
static_assert(__builtin_offsetof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE, FrameBufferBase) == 24);
static_assert(sizeof(EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE) == 40);
