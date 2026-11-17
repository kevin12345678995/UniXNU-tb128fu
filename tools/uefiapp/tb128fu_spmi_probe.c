// TB128FU UEFI SPMI probe: a live bit-grid of the two PMIC/SPMI register windows this SoC may
// expose, so a photo of the screen tells us which base responds and which bit is the volume key.
//
//   row block 1: 0x0C400000  (PMIC arbiter "core", the layout the sheng board reads)
//   row block 2: 0x001C4000  (SPMI controller node from this board's device tree)
//
// Each byte is drawn as eight small squares (bit 7 left, bit 0 right). The grid refreshes every
// 200 ms, so holding the volume key makes one square flip on screen.

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

static EFI_GUID gGopGuid = { 0x9042a9de, 0x23dc, 0x4a38,
                              { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };

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

static VOID Clear (UINT32 Colour)
{
  FillRect (0, 0, gWidth, gHeight, Colour);
}

// Draws 64 bytes from one base as a 8x64 grid of bits.
static VOID DrawWindow (UINTN Y0, UINTN Base)
{
  volatile UINT8 *Ptr = (volatile UINT8 *)(UINTN)Base;
  UINTN CellW = gWidth / 70;
  UINTN CellH = CellW / 2;
  UINTN Byte;
  UINTN Bit;

  if (CellW < 4) {
    CellW = 4;
  }
  if (CellH < 2) {
    CellH = 2;
  }

  for (Byte = 0; Byte < 64; Byte++) {
    UINT8 Value = Ptr[Byte];
    UINTN BaseX = (Byte % 32) * (8 * (CellW + 1) + 6);
    UINTN BaseY = Y0 + (Byte / 32) * (8 * (CellH + 1) + 10);

    for (Bit = 0; Bit < 8; Bit++) {
      UINTN X = BaseX + Bit * (CellW + 1);
      UINTN Y = BaseY;
      UINT32 Colour;
      if ((Value & (1u << (7 - Bit))) != 0) {
        Colour = 0x0000FF00;
      } else {
        Colour = 0x00202020;
      }
      FillRect (X, Y, CellW, CellH, Colour);
    }
    // one tall marker every 8 bytes, so the photo can be read off positionally
    if ((Byte % 8) == 0) {
      FillRect (BaseX, BaseY - 4, 8 * (CellW + 1), 2, 0x00808080);
    }
  }
}

EFI_STATUS EFIAPI efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_BOOT_SERVICES *BS = SystemTable->BootServices;
  EFI_STATUS         Status;

  (VOID)ImageHandle;

  Status = BS->LocateProtocol (&gGopGuid, NULL, (VOID **)&gGop);
  if (EFI_ERROR (Status) || (gGop == NULL) || (gGop->Mode == NULL) || (gGop->Mode->Info == NULL)) {
    for (;;) {
      BS->Stall (1000000);
    }
  }
  gWidth  = gGop->Mode->Info->HorizontalResolution;
  gHeight = gGop->Mode->Info->VerticalResolution;

  for (;;) {
    Clear (0x00000000);
    DrawWindow (60, 0x0C400000UL);
    FillRect (0, gHeight / 2 - 6, gWidth, 12, 0x00000040);
    DrawWindow (gHeight / 2 + 20, 0x001C4000UL);
    BS->Stall (200000);
  }
}
