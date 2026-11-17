// TB128FU UEFI tool v3: panel test + input probe, no keyboard layout assumptions.
//
// Stage 1 (nothing pressed)   : red/blue checkerboard  -> "no input detected"
// Press any key (Volume Up/Down): solid green with a white bar -> "input works"
// Press again                 : six-colour cycle + growing bar (panel test)
//
// Built against the firmware tree's MdePkg headers, linked with /dynamicbase so the image
// carries a .reloc table (EDK2 refuses reloc-less images when it cannot honour ImageBase).

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

static EFI_GUID gGopGuid = { 0x9042a9de, 0x23dc, 0x4a38,
                              { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };

// Forces a DIR64 relocation so lld emits .reloc.
static UINT32 gPalette[6] = { 0x00FF0000, 0x0000FF00, 0x000000FF,
                              0x00FFFF00, 0x00FF00FF, 0x00FFFFFF };
static UINT32 *volatile gPaletteAnchor = gPalette;

static EFI_GRAPHICS_OUTPUT_PROTOCOL *gGop = NULL;

static VOID FillRect (UINTN X0, UINTN Y0, UINTN W, UINTN H, UINT32 Colour)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode = gGop->Mode;
  UINT32 *Base   = (UINT32 *)(UINTN)Mode->FrameBufferBase;
  UINTN   Width  = Mode->Info->HorizontalResolution;
  UINTN   Height = Mode->Info->VerticalResolution;
  UINTN   Stride = Mode->Info->PixelsPerScanLine;
  UINTN   x;
  UINTN   y;

  for (y = Y0; y < Y0 + H && y < Height; y++) {
    UINT32 *Row = Base + y * Stride;
    for (x = X0; x < X0 + W && x < Width; x++) {
      Row[x] = Colour;
    }
  }
}

static VOID Clear (UINT32 Colour)
{
  FillRect (0, 0, gGop->Mode->Info->HorizontalResolution,
            gGop->Mode->Info->VerticalResolution, Colour);
}

static VOID Checkerboard (VOID)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode = gGop->Mode;
  UINT32 *Base   = (UINT32 *)(UINTN)Mode->FrameBufferBase;
  UINTN   Width  = Mode->Info->HorizontalResolution;
  UINTN   Height = Mode->Info->VerticalResolution;
  UINTN   Stride = Mode->Info->PixelsPerScanLine;
  UINTN   x;
  UINTN   y;

  for (y = 0; y < Height; y++) {
    UINT32 *Row = Base + y * Stride;
    for (x = 0; x < Width; x++) {
      Row[x] = ((((x / 96) + (y / 96)) & 1) != 0) ? 0x00FF0000 : 0x0000FFFF;
    }
  }
}

// Returns TRUE when a key was pressed since the last call.
static BOOLEAN KeyPressed (EFI_SYSTEM_TABLE *St)
{
  EFI_INPUT_KEY Key;

  if (St->ConIn == NULL) {
    return FALSE;
  }
  if (EFI_ERROR (St->ConIn->ReadKeyStroke (St->ConIn, &Key))) {
    return FALSE;
  }
  return TRUE;
}

EFI_STATUS EFIAPI efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_BOOT_SERVICES *BS = SystemTable->BootServices;
  EFI_STATUS         Status;
  UINTN              Width;

  (VOID)ImageHandle;

  Status = BS->LocateProtocol (&gGopGuid, NULL, (VOID **)&gGop);
  if (EFI_ERROR (Status) || (gGop == NULL) || (gGop->Mode == NULL) || (gGop->Mode->Info == NULL)) {
    for (;;) {
      BS->Stall (1000000);
    }
  }
  Width = gGop->Mode->Info->HorizontalResolution;

  // Stage 1: checkerboard. Stays until a key arrives, so it also answers "is there any input?".
  Checkerboard ();
  while (!KeyPressed (SystemTable)) {
    BS->Stall (100000);
  }

  // Stage 2: solid green with a white bar. Seeing this proves the key reached UEFI.
  Clear (0x0000C000);
  FillRect (0, gGop->Mode->Info->VerticalResolution / 2 - 40, Width, 80, 0x00FFFFFF);
  BS->Stall (1500000);
  while (KeyPressed (SystemTable)) {
    BS->Stall (100000);
  }

  // Stage 3: panel test - six colours, growing bar.
  {
    UINT32 Step;

    for (Step = 0;; Step++) {
      Clear (0x00000000);
      FillRect (0, gGop->Mode->Info->VerticalResolution / 2 - 60,
                (Width * ((Step % 20) + 1)) / 20, 120, gPaletteAnchor[Step % 6]);
      BS->Stall (400000);
    }
  }
}
