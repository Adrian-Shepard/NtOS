#pragma once
#include <stdint.h>
#include <stddef.h>
#include "../bootinfo.h"

typedef uint8_t  UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uint64_t UINTN;
typedef void     VOID;
typedef UINT64   EFI_STATUS;
typedef VOID    *EFI_HANDLE;
typedef UINT64   EFI_PHYSICAL_ADDRESS;
typedef char     CHAR8;
typedef uint16_t CHAR16;

#define EFI_SUCCESS 0
#define EFI_ERROR(s) ((int64_t)(s) < 0)

typedef struct { UINT32 a; UINT16 b; UINT16 c; UINT8 d[8]; } GUID;
#define EFI_GUID_INIT(a,b,c,d0,d1,d2,d3,d4,d5,d6,d7) \
    { a, b, c, { d0,d1,d2,d3,d4,d5,d6,d7 } }

typedef struct {
    UINT64 Signature;
    UINT32 Revision;
    UINT32 HeaderSize;
    UINT32 CRC32;
    UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    VOID *Reset;
    EFI_STATUS (*OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, CHAR16*);
    VOID *TestString;
    VOID *QueryMode;
    VOID *SetMode;
    VOID *SetAttribute;
    EFI_STATUS (*ClearScreen)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*);
};

typedef struct {
    EFI_TABLE_HEADER Hdr;
    VOID *RaiseTPL;
    VOID *RestoreTPL;
    EFI_STATUS (*AllocatePages)(UINT32, UINT32, UINTN, EFI_PHYSICAL_ADDRESS*);
    EFI_STATUS (*FreePages)(EFI_PHYSICAL_ADDRESS, UINTN);
    EFI_STATUS (*GetMemoryMap)(UINTN*, EFI_MEMORY_DESCRIPTOR*, UINTN*, UINTN*, UINT32*);
    EFI_STATUS (*AllocatePool)(UINT32, UINTN, VOID**);
    EFI_STATUS (*FreePool)(VOID*);
    VOID *CreateEvent;
    VOID *SetTimer;
    VOID *WaitForEvent;
    VOID *SignalEvent;
    VOID *CloseEvent;
    VOID *CheckEvent;
    VOID *InstallProtocolInterface;
    VOID *ReinstallProtocolInterface;
    VOID *UninstallProtocolInterface;
    EFI_STATUS (*HandleProtocol)(EFI_HANDLE, GUID*, VOID**);
    VOID *Reserved;
    VOID *RegisterProtocolNotify;
    VOID *LocateHandle;
    VOID *LocateDevicePath;
    VOID *InstallConfigurationTable;
    EFI_STATUS (*LoadImage)(UINT8, EFI_HANDLE, VOID*, VOID*, UINTN, EFI_HANDLE*);
    EFI_STATUS (*StartImage)(EFI_HANDLE, UINTN*, CHAR16**);
    EFI_STATUS (*Exit)(EFI_HANDLE, EFI_STATUS, UINTN, CHAR16*);
    EFI_STATUS (*UnloadImage)(EFI_HANDLE);
    EFI_STATUS (*ExitBootServices)(EFI_HANDLE, UINTN);
    VOID *GetNextMonotonicCount;
    VOID *Stall;
    VOID *SetWatchdogTimer;
    VOID *ConnectController;
    VOID *DisconnectController;
    VOID *OpenProtocol;
    VOID *CloseProtocol;
    VOID *OpenProtocolInformation;
    VOID *ProtocolsPerHandle;
    VOID *LocateHandleBuffer;
    EFI_STATUS (*LocateProtocol)(GUID*, VOID*, VOID**);
} EFI_BOOT_SERVICES;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    CHAR16 *FirmwareVendor;
    UINT32 FirmwareRevision;
    EFI_HANDLE ConsoleInHandle;
    VOID *ConIn;
    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    EFI_HANDLE StandardErrorHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *StdErr;
    VOID *RuntimeServices;
    EFI_BOOT_SERVICES *BootServices;
    UINTN NumberOfTableEntries;
    VOID *ConfigurationTable;
} EFI_SYSTEM_TABLE;

typedef struct {
    UINT32 Revision;
    EFI_HANDLE ParentHandle;
    VOID *SystemTable;
    EFI_HANDLE DeviceHandle;
    VOID *FilePath;
    VOID *Reserved;
    UINT32 LoadOptionsSize;
    VOID *LoadOptions;
    VOID *ImageBase;
    UINT64 ImageSize;
    UINT32 ImageCodeType;
    UINT32 ImageDataType;
    VOID *Unload;
} EFI_LOADED_IMAGE_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;
struct EFI_FILE_PROTOCOL {
    UINT64 Revision;
    EFI_STATUS (*Open)(EFI_FILE_PROTOCOL*, EFI_FILE_PROTOCOL**, CHAR16*, UINT64, UINT64);
    EFI_STATUS (*Close)(EFI_FILE_PROTOCOL*);
    VOID *Delete;
    EFI_STATUS (*Read)(EFI_FILE_PROTOCOL*, UINTN*, VOID*);
    EFI_STATUS (*Write)(EFI_FILE_PROTOCOL*, UINTN*, VOID*);
    VOID *GetPosition;
    VOID *SetPosition;
    VOID *GetInfo;
    VOID *SetInfo;
    VOID *Flush;
};

typedef struct {
    UINT64 Revision;
    EFI_STATUS (*OpenVolume)(VOID*, EFI_FILE_PROTOCOL**);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    UINT32 RedMask, GreenMask, BlueMask, ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    UINT32 Version;
    UINT32 HorizontalResolution;
    UINT32 VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    UINT32 MaxMode;
    UINT32 Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    UINTN SizeOfInfo;
    EFI_PHYSICAL_ADDRESS FrameBufferBase;
    UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    EFI_STATUS (*QueryMode)(VOID*, UINT32, UINTN*, EFI_GRAPHICS_OUTPUT_MODE_INFORMATION**);
    EFI_STATUS (*SetMode)(VOID*, UINT32);
    VOID *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    EFI_GUID_INIT(0x5B1B31A1,0x9562,0x11d2,0x8E,0x3F,0x00,0xA0,0xC9,0x69,0x72,0x3B)
#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    EFI_GUID_INIT(0x964E5B22,0x6459,0x11d2,0x8E,0x39,0x00,0xA0,0xC9,0x69,0x72,0x3B)
#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    EFI_GUID_INIT(0x9042A9DE,0x23DC,0x4A38,0x96,0xFB,0x7A,0xDE,0xD0,0x80,0x51,0x6A)

#define KERNEL_LOAD_ADDR 0x4000000ull
#define KERNEL_MAX_SIZE  (4ull * 1024 * 1024)
#define KERNEL_PAGES     (KERNEL_MAX_SIZE / 4096)