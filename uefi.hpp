#pragma once


// ============================================================
// Basic Types
// ============================================================

using UINT8  = unsigned char;
using UINT16 = unsigned short;
using UINT32 = unsigned int;
using UINT64 = unsigned long long;
using UINTN  = unsigned long long;
using INT32 = int;

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
constexpr EFI_STATUS EFI_INVALID_PARAMETER = (1ULL << 63) | 2;
constexpr EFI_STATUS EFI_NOT_READY = (1ULL << 63) | 6;
constexpr EFI_STATUS EFI_DEVICE_ERROR = (1ULL << 63) | 7;
constexpr EFI_STATUS EFI_NOT_FOUND = (1ULL << 63) | 14;
constexpr EFI_STATUS EFI_LOAD_ERROR = (1ULL << 63) | 1;
constexpr EFI_STATUS EFI_OUT_OF_RESOURCES = (1ULL << 63) | 9;
constexpr EFI_STATUS EFI_ACCESS_DENIED = (1ULL << 63) | 15;
constexpr EFI_STATUS EFI_ALREADY_STARTED = (1ULL << 63) | 20;
constexpr EFI_STATUS EFI_ABORTED = (1ULL << 63) | 21;
constexpr EFI_STATUS EFI_SECURITY_VIOLATION = (1ULL << 63) | 26;


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
struct EFI_SYSTEM_TABLE;

// Device path headers use byte-sized length fields and may be unaligned.
struct EFI_DEVICE_PATH_PROTOCOL {
    UINT8 Type;
    UINT8 SubType;
    UINT8 Length[2];
};

constexpr EFI_GUID EFI_DEVICE_PATH_PROTOCOL_GUID = {
    0x09576e91, 0x6d3f, 0x11d2,
    {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};
constexpr EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {
    0x5b1b31a1, 0x9562, 0x11d2,
    {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
};
constexpr UINT8 MEDIA_DEVICE_PATH = 0x04;
constexpr UINT8 MEDIA_FILEPATH_DP = 0x04;
constexpr UINT8 END_DEVICE_PATH_TYPE = 0x7f;
constexpr UINT8 END_INSTANCE_DEVICE_PATH_SUBTYPE = 0x01;
constexpr UINT8 END_ENTIRE_DEVICE_PATH_SUBTYPE = 0xff;
constexpr UINT32 EFI_OPEN_PROTOCOL_GET_PROTOCOL = 0x00000002;
constexpr UINT32 EFI_OPEN_PROTOCOL_TEST_PROTOCOL = 0x00000004;

enum EFI_MEMORY_TYPE : UINT32 {
    EfiReservedMemoryType, EfiLoaderCode, EfiLoaderData,
    EfiBootServicesCode, EfiBootServicesData, EfiRuntimeServicesCode,
    EfiRuntimeServicesData, EfiConventionalMemory, EfiUnusableMemory,
    EfiACPIReclaimMemory, EfiACPIMemoryNVS, EfiMemoryMappedIO,
    EfiMemoryMappedIOPortSpace, EfiPalCode, EfiPersistentMemory,
    EfiUnacceptedMemoryType, EfiMaxMemoryType
};

using EFI_IMAGE_UNLOAD = EFI_STATUS (EFIAPI *)(EFI_HANDLE ImageHandle);

struct EFI_LOADED_IMAGE_PROTOCOL {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    EFI_SYSTEM_TABLE *SystemTable;
    EFI_HANDLE DeviceHandle;
    EFI_DEVICE_PATH_PROTOCOL *FilePath;
    void *Reserved;
    UINT32 LoadOptionsSize;
    void *LoadOptions;
    void *ImageBase;
    UINT64 ImageSize;
    EFI_MEMORY_TYPE ImageCodeType;
    EFI_MEMORY_TYPE ImageDataType;
    EFI_IMAGE_UNLOAD Unload;
};


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

// 未呼叫的方法只保留指標，以維持規格中的欄位順序。
using EFI_GENERIC_FN = void (EFIAPI *)();
using EFI_TEXT_CLEAR_SCREEN =
    EFI_STATUS (EFIAPI *)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This);

using EFI_TEXT_QUERY_MODE = EFI_STATUS (EFIAPI *)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber, UINTN *Columns, UINTN *Rows);
using EFI_TEXT_SET_MODE = EFI_STATUS (EFIAPI *)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN ModeNumber);
using EFI_TEXT_SET_ATTRIBUTE = EFI_STATUS (EFIAPI *)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Attribute);
using EFI_TEXT_SET_CURSOR_POSITION = EFI_STATUS (EFIAPI *)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, UINTN Column, UINTN Row);
using EFI_TEXT_ENABLE_CURSOR = EFI_STATUS (EFIAPI *)(
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, BOOLEAN Visible);

struct EFI_SIMPLE_TEXT_OUTPUT_MODE {
    INT32 MaxMode;
    INT32 Mode;
    INT32 Attribute;
    INT32 CursorColumn;
    INT32 CursorRow;
    BOOLEAN CursorVisible;
};

constexpr UINTN EFI_BLACK = 0;
constexpr UINTN EFI_BLUE = 1;
constexpr UINTN EFI_LIGHTGRAY = 7;
constexpr UINTN EFI_WHITE = 15;
constexpr UINT16 SCAN_UP = 0x01;
constexpr UINT16 SCAN_DOWN = 0x02;
constexpr UINT16 SCAN_ESC = 0x17;

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_TEXT_RESET Reset;
    EFI_TEXT_STRING OutputString;
    EFI_GENERIC_FN TestString;
    EFI_TEXT_QUERY_MODE QueryMode;
    EFI_TEXT_SET_MODE SetMode;
    EFI_TEXT_SET_ATTRIBUTE SetAttribute;
    EFI_TEXT_CLEAR_SCREEN ClearScreen;
    EFI_TEXT_SET_CURSOR_POSITION SetCursorPosition;
    EFI_TEXT_ENABLE_CURSOR EnableCursor;
    EFI_SIMPLE_TEXT_OUTPUT_MODE *Mode;
};

// Runtime Services through ResetSystem, in the UEFI-defined order.
enum EFI_RESET_TYPE : UINT32 {
    EfiResetCold, EfiResetWarm, EfiResetShutdown, EfiResetPlatformSpecific
};
using EFI_RESET_SYSTEM = void (EFIAPI *)(
    EFI_RESET_TYPE ResetType, EFI_STATUS ResetStatus, UINTN DataSize, void *ResetData);

struct EFI_RUNTIME_SERVICES {
    EFI_TABLE_HEADER Hdr;
    EFI_GENERIC_FN GetTime;
    EFI_GENERIC_FN SetTime;
    EFI_GENERIC_FN GetWakeupTime;
    EFI_GENERIC_FN SetWakeupTime;
    EFI_GENERIC_FN SetVirtualAddressMap;
    EFI_GENERIC_FN ConvertPointer;
    EFI_GENERIC_FN GetVariable;
    EFI_GENERIC_FN GetNextVariableName;
    EFI_GENERIC_FN SetVariable;
    EFI_GENERIC_FN GetNextHighMonotonicCount;
    EFI_RESET_SYSTEM ResetSystem;
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

// 只有成功 QueryMode 交付的非空配置才可用 FreePool 釋放。
using EFI_FREE_POOL = EFI_STATUS (EFIAPI *)(void *Buffer);


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

using EFI_SET_WATCHDOG_TIMER = EFI_STATUS (EFIAPI *)(
    UINTN Timeout, UINT64 WatchdogCode, UINTN DataSize, CHAR16 *WatchdogData);

using EFI_OPEN_PROTOCOL = EFI_STATUS (EFIAPI *)(
    EFI_HANDLE Handle, EFI_GUID *Protocol, void **Interface,
    EFI_HANDLE AgentHandle, EFI_HANDLE ControllerHandle, UINT32 Attributes);
using EFI_LOAD_IMAGE = EFI_STATUS (EFIAPI *)(
    BOOLEAN BootPolicy, EFI_HANDLE ParentImageHandle,
    EFI_DEVICE_PATH_PROTOCOL *DevicePath, void *SourceBuffer, UINTN SourceSize,
    EFI_HANDLE *ImageHandle);
using EFI_START_IMAGE = EFI_STATUS (EFIAPI *)(
    EFI_HANDLE ImageHandle, UINTN *ExitDataSize, CHAR16 **ExitData);
using EFI_UNLOAD_IMAGE = EFI_STATUS (EFIAPI *)(EFI_HANDLE ImageHandle);


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
    EFI_FREE_POOL FreePool;

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
    EFI_LOAD_IMAGE LoadImage;
    EFI_START_IMAGE StartImage;
    EFI_GENERIC_FN Exit;
    EFI_UNLOAD_IMAGE UnloadImage;
    EFI_GENERIC_FN ExitBootServices;

    // Miscellaneous Services
    EFI_GENERIC_FN GetNextMonotonicCount;
    EFI_GENERIC_FN Stall;
    EFI_SET_WATCHDOG_TIMER SetWatchdogTimer;

    // Driver Support Services
    EFI_GENERIC_FN ConnectController;
    EFI_GENERIC_FN DisconnectController;

    // Open and Close Protocol Services
    EFI_OPEN_PROTOCOL OpenProtocol;
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
static_assert(sizeof(EFI_DEVICE_PATH_PROTOCOL) == 4);
static_assert(sizeof(EFI_MEMORY_TYPE) == 4);
static_assert(sizeof(EFI_LOADED_IMAGE_PROTOCOL) == 96);
static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL, DeviceHandle) == 24);
static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL, FilePath) == 32);
static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL, LoadOptionsSize) == 48);
static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL, LoadOptions) == 56);
static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL, ImageCodeType) == 80);
static_assert(__builtin_offsetof(EFI_LOADED_IMAGE_PROTOCOL, Unload) == 88);
static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, LoadImage) == 200);
static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, StartImage) == 208);
static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, UnloadImage) == 224);
static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, OpenProtocol) == 280);
static_assert(sizeof(CHAR16) == 2);
static_assert(sizeof(EFI_SIMPLE_TEXT_OUTPUT_MODE) == 24);
static_assert(__builtin_offsetof(EFI_SIMPLE_TEXT_OUTPUT_MODE, CursorVisible) == 20);
static_assert(__builtin_offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, QueryMode) == 24);
static_assert(__builtin_offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, SetAttribute) == 40);
static_assert(__builtin_offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, SetCursorPosition) == 56);
static_assert(__builtin_offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, EnableCursor) == 64);
static_assert(__builtin_offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, Mode) == 72);
static_assert(sizeof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL) == 80);
static_assert(sizeof(EFI_RESET_TYPE) == 4);
static_assert(__builtin_offsetof(EFI_RUNTIME_SERVICES, ResetSystem) == 104);
static_assert(sizeof(EFI_RUNTIME_SERVICES) == 112);
static_assert(__builtin_offsetof(EFI_SYSTEM_TABLE, RuntimeServices) == 88);
static_assert(__builtin_offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, Reset) == 0);
static_assert(__builtin_offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, OutputString) == 8);
static_assert(__builtin_offsetof(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL, ClearScreen) == 48);
static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, FreePool) == 72);
static_assert(__builtin_offsetof(EFI_BOOT_SERVICES, SetWatchdogTimer) == 256);
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
