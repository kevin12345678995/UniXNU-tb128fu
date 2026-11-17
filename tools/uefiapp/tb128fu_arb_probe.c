// TB128FU SPMI probe v10 - now at the address the platform actually maps.
//
// MemoryMapLib.c of this board declares {"PMIC ARB SPMI", 0x01C00000, 0x02800000, MmIO},
// so the PMIC arbiter window is 0x01C00000 and the observer channels sit 0x40000 further up,
// exactly like the layout the sheng board reads (core 0xc400000, obs 0xc440000).
//
// This build walks the APID map for the PM6125 peripherals and, for every APID it finds, shows
// the first 16 register bytes as a live bit grid. Pressing volume up should flip one bit.

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

static EFI_GUID gGopGuid = { 0x9042a9de, 0x23dc, 0x4a38,
                              { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };

static UINT32 gTouch[4] = { 1, 2, 3, 4 };
static UINT32 *volatile gTouchAnchor = gTouch;

#define ARB_CORE 0x01C00000UL
#define ARB_OBS  0x02000000UL

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

static UINT32 Read32 (UINTN Address)
{
  return *(volatile UINT32 *)(UINTN)Address;
}

static UINT8 ArbRead8 (UINTN Apid, UINTN Reg)
{
  volatile UINT32 *Ch = (volatile UINT32 *)(UINTN)(ARB_OBS + 0x20 * Apid);
  UINTN            n;

  Ch[0] = (1u << 27) | ((UINT32)Reg << 4);
  for (n = 0; n < 100000; n++) {
    UINT32 Status = Ch[2];
    if ((Status & 1u) != 0) {
      if ((Status & 0xEu) != 0) {
        return 0xFF;
      }
      return (UINT8)Ch[6];
    }
  }
  return 0xFE;
}

static VOID Grid (UINTN X0, UINTN Y0, UINTN Address, UINTN Bytes)
{
  UINTN Cell = 14;
  UINTN i;
  UINTN b;

  for (i = 0; i < Bytes; i++) {
    UINT8 Value = (UINT8)Read32 (Address + i);
    UINTN X     = X0 + (i % 16) * (8 * (Cell + 2) + 10);
    UINTN Y     = Y0 + (i / 16) * (Cell + 6);
    for (b = 0; b < 8; b++) {
      UINT32 Colour = ((Value & (1u << (7 - b))) != 0) ? 0x0000FF00 : 0x00202020;
      FillRect (X + b * (Cell + 2), Y, Cell, Cell, Colour);
    }
  }
}

static VOID GridApid (UINTN X0, UINTN Y0, UINTN Apid, UINTN Bytes)
{
  UINTN Cell = 14;
  UINTN i;
  UINTN b;

  for (i = 0; i < Bytes; i++) {
    UINT8 Value = ArbRead8 (Apid, i);
    UINTN X     = X0 + (i % 16) * (8 * (Cell + 2) + 10);
    UINTN Y     = Y0 + (i / 16) * (Cell + 6);
    for (b = 0; b < 8; b++) {
      UINT32 Colour = ((Value & (1u << (7 - b))) != 0) ? 0x0000FF00 : 0x00202020;
      FillRect (X + b * (Cell + 2), Y, Cell, Cell, Colour);
    }
  }
}

EFI_STATUS EFIAPI efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_BOOT_SERVICES *BS = SystemTable->BootServices;
  EFI_STATUS         Status;
  UINT32             Version;
  UINT32             Features;
  UINT32             Apids;
  UINTN              PonApid = 0xFFFF;
  UINTN              GpioApid = 0xFFFF;
  UINTN              i;

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

  Version  = Read32 (ARB_CORE);
  Features = Read32 (ARB_CORE + 0x04);
  Apids    = Features & 0x7FF;

  // APID map: each entry holds the PPID in bits 8..19. Look for PON (0x013) and the PM6125
  // GPIO bank (PID 0xC0..0xC8 on PMIC SID 0).
  for (i = 0; (i < Apids) && (i < 1024); i++) {
    UINT32 Map  = Read32 (ARB_CORE + 0x2000 + 4 * i);
    UINT32 Ppid = (Map >> 8) & 0xFFF;
    if (Ppid == 0x013) {
      PonApid = i;
    }
    if ((Ppid >= 0x0C0) && (Ppid <= 0x0C8) && (GpioApid == 0xFFFF)) {
      GpioApid = i;
    }
  }

  for (;;) {
    FillRect (0, 0, gWidth, gHeight, 0x00000000);

    // verdict row: version valid, PON apid found, GPIO apid found
    FillRect (40, 30, 90, 90, ((Version >= 0x70000000) && (Version < 0x80000000)) ? 0x0000C000 : 0x00A00000);
    FillRect (170, 30, 90, 90, (PonApid != 0xFFFF) ? 0x0000C000 : 0x00A00000);
    FillRect (300, 30, 90, 90, (GpioApid != 0xFFFF) ? 0x0000C000 : 0x00A00000);

    Grid (40, 160, ARB_CORE, 32);
    if (PonApid != 0xFFFF) {
      GridApid (40, 320, PonApid, 32);
    }
    if (GpioApid != 0xFFFF) {
      GridApid (40, 480, GpioApid, 32);
    }
    BS->Stall (200000);
  }
}
