// TB128FU UEFI MMIO/SPMI probe v9.
//
// The DXE core of this firmware never maps the PMIC arbiter, so a blind read took a translation
// fault. This build asks the CPU arch protocol to map the two candidate windows as device memory
// first, and only reads a window when the mapping call reported success.

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/Cpu.h>

static EFI_GUID gGopGuid = { 0x9042a9de, 0x23dc, 0x4a38,
                              { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };

// Forces a DIR64 relocation so the image carries .reloc.
static UINT32 gTouch[4] = { 1, 2, 3, 4 };
static UINT32 *volatile gTouchAnchor = gTouch;

static EFI_GRAPHICS_OUTPUT_PROTOCOL *gGop = NULL;
static UINTN gWidth;
static UINTN gHeight;

static VOID FillRect (UINTN X0, UINTN Y0, UINTN W, UINTN H, UINT32 Colour)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode = gGop->Mode;
  UINT32 *Base   = (UINT32 *)(UINTN)Mode->FrameBufferBase;
  UINTN   Stride = Mode->Info->PixelsPerScanLine;
  UINTN   x;
  UINTN   y;

  for (y = Y0; (y < Y0 + H) && (y < gHeight); y++) {
    UINT32 *Row = Base + y * Stride;
    for (x = X0; (x < X0 + W) && (x < gWidth); x++) {
      Row[x] = Colour;
    }
  }
}

static VOID BitGrid (UINTN X0, UINTN Y0, UINTN Base, UINTN Bytes)
{
  volatile UINT8 *Ptr = (volatile UINT8 *)(UINTN)Base;
  UINTN Cell = 16;
  UINTN i;
  UINTN b;

  for (i = 0; i < Bytes; i++) {
    UINT8 Value = Ptr[i];
    UINTN X     = X0 + (i % 8) * (8 * (Cell + 2) + 12);
    UINTN Y     = Y0 + (i / 8) * (Cell + 6);
    for (b = 0; b < 8; b++) {
      UINT32 Colour = ((Value & (1u << (7 - b))) != 0) ? 0x0000FF00 : 0x00202020;
      FillRect (X + b * (Cell + 2), Y, Cell, Cell, Colour);
    }
  }
}

EFI_STATUS EFIAPI efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_BOOT_SERVICES           *BS = SystemTable->BootServices;
  EFI_CPU_ARCH_PROTOCOL       *Cpu = NULL;
  EFI_STATUS                   Status;
  EFI_STATUS                   Map1;
  EFI_STATUS                   Map2;
  EFI_STATUS                   Map3;
  UINT64                       W1 = 0x0C400000ULL;
  UINT64                       W2 = 0x001C4000ULL;
  UINT64                       W3 = 0x00404000ULL;
  UINTN                        i;

  (VOID)ImageHandle;
  (VOID)gTouchAnchor;

  Status = BS->LocateProtocol (&gGopGuid, NULL, (VOID **)&gGop);
  if (EFI_ERROR (Status) || (gGop == NULL) || (gGop->Mode == NULL) || (gGop->Mode->Info == NULL)) {
    for (;;) {
      BS->Stall (1000000);
    }
  }
  gWidth  = gGop->Mode->Info->HorizontalResolution;
  gHeight = gGop->Mode->Info->VerticalResolution;

  Map1 = EFI_NOT_FOUND;
  Map2 = EFI_NOT_FOUND;
  Map3 = EFI_NOT_FOUND;
  if (!EFI_ERROR (BS->LocateProtocol (&gEfiCpuArchProtocolGuid, NULL, (VOID **)&Cpu)) && (Cpu != NULL)) {
    Map1 = Cpu->SetMemoryAttributes (Cpu, (EFI_PHYSICAL_ADDRESS)W1, 0x10000, EFI_MEMORY_UC);
    Map2 = Cpu->SetMemoryAttributes (Cpu, (EFI_PHYSICAL_ADDRESS)W2, 0x10000, EFI_MEMORY_UC);
    Map3 = Cpu->SetMemoryAttributes (Cpu, (EFI_PHYSICAL_ADDRESS)W3, 0x10000, EFI_MEMORY_UC);
  }

  FillRect (0, 0, gWidth, gHeight, 0x00000000);
  // three verdict blocks: green when the CPU arch protocol mapped the window
  for (i = 0; i < 3; i++) {
    EFI_STATUS S = (i == 0) ? Map1 : ((i == 1) ? Map2 : Map3);
    UINTN      X = 40 + i * (gWidth / 3);
    FillRect (X, 40, gWidth / 8, gWidth / 8, EFI_ERROR (S) ? 0x00A00000 : 0x0000C000);
  }

  if (!EFI_ERROR (Map1)) {
    BitGrid (40, 40 + gWidth / 8 + 30, (UINTN)W1, 8);
  }
  if (!EFI_ERROR (Map2)) {
    BitGrid (40 + gWidth / 3, 40 + gWidth / 8 + 30, (UINTN)W2, 8);
  }
  if (!EFI_ERROR (Map3)) {
    BitGrid (40 + 2 * (gWidth / 3), 40 + gWidth / 8 + 30, (UINTN)W3, 8);
  }

  for (;;) {
    BS->Stall (200000);
    if (!EFI_ERROR (Map1)) {
      BitGrid (40, 40 + gWidth / 8 + 30, (UINTN)W1, 8);
    }
    if (!EFI_ERROR (Map2)) {
      BitGrid (40 + gWidth / 3, 40 + gWidth / 8 + 30, (UINTN)W2, 8);
    }
    if (!EFI_ERROR (Map3)) {
      BitGrid (40 + 2 * (gWidth / 3), 40 + gWidth / 8 + 30, (UINTN)W3, 8);
    }
  }
}
