// TB128FU UEFI probe v2: compiled against the tree's EDK2 headers (MdePkg) so every struct
// layout is the specification's, not hand written. Fills the panel, then cycles colours and
// grows a bar so the screen itself proves the application runs.

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

static EFI_GUID gGopGuid = { 0x9042a9de, 0x23dc, 0x4a38,
                              { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };

static VOID Paint (EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop, UINT32 Colour, UINTN FilledW)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode = Gop->Mode;
  if (Mode == NULL || Mode->Info == NULL) {
    return;
  }
  UINT32 *Base   = (UINT32 *)(UINTN)Mode->FrameBufferBase;
  UINTN   Width  = Mode->Info->HorizontalResolution;
  UINTN   Height = Mode->Info->VerticalResolution;
  UINTN   Stride = Mode->Info->PixelsPerScanLine;

  for (UINTN y = 0; y < Height; y++) {
    UINT32 *Row = Base + y * Stride;
    for (UINTN x = 0; x < Width; x++) {
      Row[x] = 0x00000000;
    }
  }
  UINTN Y0 = Height / 2 - Height / 40;
  UINTN Y1 = Height / 2 + Height / 40;
  for (UINTN y = Y0; y < Y1 && y < Height; y++) {
    UINT32 *Row = Base + y * Stride;
    for (UINTN x = 0; x < FilledW && x < Width; x++) {
      Row[x] = Colour;
    }
  }
}

EFI_STATUS EFIAPI efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  (VOID)ImageHandle;
  EFI_BOOT_SERVICES *BS = SystemTable->BootServices;

  // Immediate marker: if the panel turns solid red, the application loaded and Boot Services work.
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop = NULL;
  EFI_STATUS Status = BS->LocateProtocol (&gGopGuid, NULL, (VOID **)&Gop);
  if (!EFI_ERROR (Status) && Gop != NULL && Gop->Mode != NULL && Gop->Mode->Info != NULL) {
    Paint (Gop, 0x00FF0000, Gop->Mode->Info->HorizontalResolution);
  } else if (SystemTable->ConOut != NULL) {
    SystemTable->ConOut->OutputString (SystemTable->ConOut, L"TB128FU probe: no GOP
");
  }

  const UINT32 Palette[6] = { 0x00FF0000, 0x0000FF00, 0x000000FF,
                              0x00FFFF00, 0x00FF00FF, 0x00FFFFFF };
  if (Gop != NULL && Gop->Mode != NULL && Gop->Mode->Info != NULL) {
    UINTN Width = Gop->Mode->Info->HorizontalResolution;
    for (UINT32 Step = 0;; Step++) {
      Paint (Gop, Palette[Step % 6], (Width * ((Step % 20) + 1)) / 20);
      BS->Stall (400000);
    }
  }

  for (;;) {
    BS->Stall (1000000);
  }
  return EFI_SUCCESS;
}
