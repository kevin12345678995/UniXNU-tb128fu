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
#include <Protocol/DxeServicesTable.h>

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
