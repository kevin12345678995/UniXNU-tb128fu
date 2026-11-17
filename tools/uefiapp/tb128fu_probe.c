// TB128FU UEFI probe: proves that our Mu-Silicium firmware hands control to an EFI
// application, that the GOP frame buffer the ABL left scanning out is writable, and that
// Boot Services timing works. Built freestanding for aarch64 (no edk2 build system).
//
//   clang --target=aarch64-unknown-windows -ffreestanding -fshort-wchar -O2 -c probe.c
//   lld-link /subsystem:efi_application /entry:efi_main /nodefaultlib /machine:arm64 ...

typedef unsigned char       UINT8;
typedef unsigned short      UINT16;
typedef unsigned int        UINT32;
typedef unsigned long long  UINT64;
typedef UINT64              UINTN;
typedef UINTN               EFI_STATUS;
typedef void               *EFI_HANDLE;
typedef UINT16              CHAR16;
typedef UINT64              EFI_PHYSICAL_ADDRESS;

typedef struct {
  UINT64 Signature;
  UINT32 Revision;
  UINT32 HeaderSize;
  UINT32 Crc32;
  UINT32 Reserved;
} EFI_TABLE_HEADER;

typedef struct {
  UINT32 MaxMode;
  UINT32 Mode;
  void  *Info;
  UINTN  SizeOfInfo;
  EFI_PHYSICAL_ADDRESS FrameBufferBase;
  UINTN  FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
  void *QueryMode;
  void *SetMode;
  void *Blt;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

// Only the fields up to LocateProtocol and Stall are used; the rest are placeholders so the
// offsets stay exactly as the UEFI specification lays them out.
typedef struct {
  EFI_TABLE_HEADER Hdr;
  void *RaiseTPL;
  void *RestoreTPL;
  void *AllocatePages;
  void *FreePages;
  void *GetMemoryMap;
  void *AllocatePool;
  void *FreePool;
  void *CreateEvent;
  void *SetTimer;
  void *WaitForEvent;
  void *SignalEvent;
  void *CloseEvent;
  void *CheckEvent;
  void *InstallProtocolInterface;
  void *ReinstallProtocolInterface;
  void *UninstallProtocolInterface;
  void *HandleProtocol;
  void *Reserved;
  void *RegisterProtocolNotify;
  void *LocateHandle;
  void *LocateDevicePath;
  void *InstallConfigurationTable;
  void *LoadImage;
  void *StartImage;
  void *Exit;
  void *UnloadImage;
  void *ExitBootServices;
  void *GetNextMonotonicCount;
  void (*Stall) (UINTN Microseconds);
  void *SetWatchdogTimer;
  void *ConnectController;
  void *DisconnectController;
  void *OpenProtocol;
  void *CloseProtocol;
  void *OpenProtocolInformation;
  void *ProtocolsPerHandle;
  void *LocateHandleBuffer;
  EFI_STATUS (*LocateProtocol) (void *Protocol, void *Registration, void **Interface);
} EFI_BOOT_SERVICES;

typedef struct {
  EFI_TABLE_HEADER Hdr;
  CHAR16 *FirmwareVendor;
  UINT32  FirmwareRevision;
  UINT32  Pad0;
  EFI_HANDLE ConsoleInHandle;
  void      *ConIn;
  EFI_HANDLE ConsoleOutHandle;
  void      *ConOut;
  EFI_HANDLE StandardErrorHandle;
  void      *StdErr;
  void      *RuntimeServices;
  EFI_BOOT_SERVICES *BootServices;
  UINTN  NumberOfTableEntries;
  void  *ConfigurationTable;
} EFI_SYSTEM_TABLE;

// {9042A9DE-23DC-4A38-96FB-7ADED080516A}
static const UINT8 gop_guid[16] = {
  0xDE, 0xA9, 0x42, 0x90, 0xDC, 0x23, 0x38, 0x4A,
  0x96, 0xFB, 0x7A, 0xDE, 0xD0, 0x80, 0x51, 0x6A
};

static void fill (EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 colour)
{
  UINT32 *pixels = (UINT32 *)(UINTN)gop->Mode->FrameBufferBase;
  UINTN   count  = gop->Mode->FrameBufferSize / 4;
  for (UINTN i = 0; i < count; i++) {
    pixels[i] = colour;
  }
}

// Horizontal bar from the left, growing with the step: visible proof that the loop and the
// Boot Services stall both run.
static void bar (EFI_GRAPHICS_OUTPUT_PROTOCOL *gop, UINT32 step, UINT32 steps, UINT32 colour)
{
  UINT32 *pixels = (UINT32 *)(UINTN)gop->Mode->FrameBufferBase;
  UINTN   width  = gop->Mode->FrameBufferSize ? 1200 : 1200;
  UINTN   height = 2000;
  UINTN   rows   = height / 10;
  UINTN   y0     = height / 2 - rows / 2;
  UINTN   filled = (width * step) / (steps ? steps : 1);
  for (UINTN y = y0; y < y0 + rows; y++) {
    for (UINTN x = 0; x < filled; x++) {
      pixels[y * width + x] = colour;
    }
  }
}

EFI_STATUS efi_main (EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
  (void)image;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
  EFI_STATUS status = st->BootServices->LocateProtocol ((void *)gop_guid, 0, (void **)&gop);
  if (status != 0 || gop == 0) {
    for (;;) {
      // No GOP: nothing to draw, but the application did load and run.
      st->BootServices->Stall (1000000);
    }
  }

  // Colour cycle: each pass paints the whole panel and grows the bar a little.
  const UINT32 palette[6] = {
    0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFF00, 0x00FF00FF, 0x00FFFFFF
  };
  for (UINT32 pass = 0;; pass++) {
    fill (gop, palette[pass % 6] == 0x00FFFFFF ? 0x00000000 : 0x00000000);
    bar (gop, (pass % 20) + 1, 20, palette[pass % 6]);
    st->BootServices->Stall (300000);
  }
  return 0;
}
