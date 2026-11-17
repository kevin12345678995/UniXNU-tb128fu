// TB128FU GCD/MMIO probe: asks the DXE dispatcher which address windows are described at all,
// then reads only those. The previous version read 0x0C400000 blind and took a translation fault,
// so this one never touches an address the GCD does not list.
//
// Screen layout:
//   three large verdict squares: 0x0C400000 (PMIC arbiter), 0x001C4000 (SPMI controller),
//                                0x00405000 (TLMM GPIO5, the window the stock key driver uses)
//   green = listed in the GCD (readable), red = not listed
//   under each green square: a live bit grid of the first 16 bytes at that base

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

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

static VOID Clear (UINT32 Colour)
{
  FillRect (0, 0, gWidth, gHeight, Colour);
}

static BOOLEAN InGcd (EFI_MEMORY_SPACE_DESCRIPTOR *Map, UINTN Count, UINT64 Address)
{
  UINTN i;

  for (i = 0; i < Count; i++) {
    if ((Address >= Map[i].BaseAddress) &&
        (Address < Map[i].BaseAddress + Map[i].Length)) {
      return TRUE;
    }
  }
  return FALSE;
}

static VOID BitGrid (UINTN X0, UINTN Y0, UINTN Base, UINTN Bytes)
{
  volatile UINT8 *Ptr = (volatile UINT8 *)(UINTN)Base;
  UINTN Cell = 18;
  UINTN i;
  UINTN b;

  for (i = 0; i < Bytes; i++) {
    UINT8 Value = Ptr[i];
    UINTN X     = X0 + (i % 8) * (8 * (Cell + 2) + 16);
    UINTN Y     = Y0 + (i / 8) * (Cell + 6);
    for (b = 0; b < 8; b++) {
      UINT32 Colour = ((Value & (1u << (7 - b))) != 0) ? 0x0000FF00 : 0x00202020;
      FillRect (X + b * (Cell + 2), Y, Cell, Cell, Colour);
    }
  }
}

EFI_STATUS EFIAPI efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_BOOT_SERVICES              *BS = SystemTable->BootServices;
  EFI_DXE_SERVICES               *DS = SystemTable->DxeServicesTable;
  EFI_STATUS                      Status;
  EFI_MEMORY_SPACE_DESCRIPTOR    *Map = NULL;
  UINTN                           Count = 0;
  UINT64                          Candidates[3];
  UINTN                           i;

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

  Candidates[0] = 0x0C400000ULL;   // PMIC arbiter core
  Candidates[1] = 0x001C4000ULL;   // SPMI controller from the device tree
  Candidates[2] = 0x00405000ULL;   // TLMM GPIO block the stock key driver reads

  Clear (0x00000000);
  if ((DS != NULL) && !EFI_ERROR (DS->GetMemorySpaceMap (&Count, &Map)) && (Map != NULL)) {
    for (i = 0; i < 3; i++) {
      BOOLEAN Mapped = InGcd (Map, Count, Candidates[i]);
      UINTN   X      = 40 + i * (gWidth / 3);
      UINTN   Y      = 40;
      UINTN   Size   = gWidth / 8;

      FillRect (X, Y, Size, Size, Mapped ? 0x0000C000 : 0x00A00000);
      if (!Mapped) {
        // red square with a black cross
        FillRect (X + Size / 8, Y + Size / 2 - 6, Size * 3 / 4, 12, 0x00000000);
        FillRect (X + Size / 2 - 6, Y + Size / 8, 12, Size * 3 / 4, 0x00000000);
      }
      if (Mapped) {
        BitGrid (X, Y + Size + 20, (UINTN)Candidates[i], 16);
      }
    }
    BS->FreePool (Map);
  }

  for (;;) {
    BS->Stall (200000);
    if ((DS != NULL) && !EFI_ERROR (DS->GetMemorySpaceMap (&Count, &Map)) && (Map != NULL)) {
      for (i = 0; i < 3; i++) {
        if (InGcd (Map, Count, Candidates[i])) {
          UINTN X = 40 + i * (gWidth / 3);
          UINTN Y = 40 + gWidth / 8 + 20;
          BitGrid (X, Y, (UINTN)Candidates[i], 16);
        }
      }
      BS->FreePool (Map);
    }
  }
}
