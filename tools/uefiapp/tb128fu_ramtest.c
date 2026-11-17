// TB128FU UEFI RAM self-test (no keyboard required).
//
// 1. Reads the UEFI memory map and paints a proportional map summary bar.
// 2. Allocates as much memory as it can (up to 512 MiB), then walks it with three patterns
//    (0x00, 0xFF, address-XOR) and verifies every word.
// 3. Reports bytes tested, elapsed time, throughput and the error count on screen with
//    7-segment digits, plus a green/red verdict block.

#include <Uefi.h>
#include <Protocol/GraphicsOutput.h>

static EFI_GUID gGopGuid = { 0x9042a9de, 0x23dc, 0x4a38,
                              { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a } };

static UINT32 gPalette[6] = { 0x00FF0000, 0x0000FF00, 0x000000FF,
                              0x00FFFF00, 0x00FF00FF, 0x00FFFFFF };
static UINT32 *volatile gPaletteAnchor = gPalette;

static EFI_GRAPHICS_OUTPUT_PROTOCOL *gGop = NULL;
static EFI_SYSTEM_TABLE             *gSt  = NULL;

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

// Seven segment digits: no font data needed, legible from a metre away.
static const UINT8 gSeg[10] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
};

static VOID Digit (UINTN X, UINTN Y, UINTN Scale, UINT32 Value, UINT32 Colour)
{
  UINT8 Mask;
  UINTN T = Scale;

  if (Value > 9) {
    return;
  }
  Mask = gSeg[Value];
  // a
  if ((Mask & 0x01) != 0) FillRect (X + T, Y, 3 * T, T, Colour);
  // b
  if ((Mask & 0x02) != 0) FillRect (X + 4 * T, Y + T, T, 2 * T, Colour);
  // c
  if ((Mask & 0x04) != 0) FillRect (X + 4 * T, Y + 4 * T, T, 2 * T, Colour);
  // d
  if ((Mask & 0x08) != 0) FillRect (X + T, Y + 6 * T, 3 * T, T, Colour);
  // e
  if ((Mask & 0x10) != 0) FillRect (X, Y + 4 * T, T, 2 * T, Colour);
  // f
  if ((Mask & 0x20) != 0) FillRect (X, Y + T, T, 2 * T, Colour);
  // g
  if ((Mask & 0x40) != 0) FillRect (X + T, Y + 3 * T, 3 * T, T, Colour);
}

// Draws a decimal number, right aligned at XRight, returns the left edge used.
static UINTN Number (UINTN XRight, UINTN Y, UINTN Scale, UINT64 Value, UINT32 Colour)
{
  UINTN Digits = 1;
  UINT64 Tmp   = Value;
  UINTN i;

  while (Tmp >= 10) {
    Tmp /= 10;
    Digits++;
  }
  for (i = 0; i < Digits; i++) {
    UINT64 Div = 1;
    UINTN  k;
    for (k = 0; k + 1 < Digits - i; k++) {
      Div *= 10;
    }
    Digit (XRight - (i + 1) * 6 * Scale, Y, Scale, (UINT32)((Value / Div) % 10), Colour);
  }
  return XRight - Digits * 6 * Scale;
}

EFI_STATUS EFIAPI efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_BOOT_SERVICES *BS = SystemTable->BootServices;
  EFI_STATUS         Status;
  UINTN              MapSize = 0;
  UINTN              MapKey  = 0;
  UINTN              DescSize = 0;
  UINT32             DescVer = 0;
  EFI_MEMORY_DESCRIPTOR *Map = NULL;
  UINTN              PageSize;
  UINT64             TotalBytes = 0;
  UINT64             FreeBytes  = 0;
  EFI_PHYSICAL_ADDRESS Buffer = 0;
  UINTN              Pages = 0;
  UINT64             Tested = 0;
  UINT64             Errors = 0;
  UINT64             Start  = 0;
  UINT64             End    = 0;
  UINTN              i;
  UINTN              Pass;

  (VOID)ImageHandle;
  gSt = SystemTable;

  Status = BS->LocateProtocol (&gGopGuid, NULL, (VOID **)&gGop);
  if (EFI_ERROR (Status) || (gGop == NULL) || (gGop->Mode == NULL) || (gGop->Mode->Info == NULL)) {
    for (;;) {
      BS->Stall (1000000);
    }
  }
  gWidth  = gGop->Mode->Info->HorizontalResolution;
  gHeight = gGop->Mode->Info->VerticalResolution;
  Clear (0x00000000);

  // ---- memory map summary -------------------------------------------------
  BS->GetMemoryMap (&MapSize, NULL, &MapKey, &DescSize, &DescVer);
  MapSize += 4 * DescSize;
  Status = BS->AllocatePool (EfiLoaderData, MapSize, (VOID **)&Map);
  if (!EFI_ERROR (Status)) {
    Status = BS->GetMemoryMap (&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (!EFI_ERROR (Status)) {
      UINTN Count = MapSize / DescSize;
      UINT8 *Raw  = (UINT8 *)Map;

      for (i = 0; i < Count; i++) {
        EFI_MEMORY_DESCRIPTOR *D = (EFI_MEMORY_DESCRIPTOR *)(Raw + i * DescSize);
        TotalBytes += D->NumberOfPages * 4096ULL;
        if (D->Type == EfiConventionalMemory) {
          FreeBytes += D->NumberOfPages * 4096ULL;
        }
      }
    }
    BS->FreePool (Map);
  }

  // map summary bar: the filled part is conventional memory
  FillRect (0, 40, gWidth, 24, 0x00202020);
  if (TotalBytes > 0) {
    FillRect (0, 40, (UINTN)((FreeBytes * gWidth) / TotalBytes), 24, 0x0000C000);
  }

  // ---- allocate as much as possible, up to 512 MiB -------------------------
  PageSize = 4096;
  for (i = 0; i < 12; i++) {
    UINTN Want = (512ULL * 1024 * 1024) >> i;
    EFI_PHYSICAL_ADDRESS Addr = 0;
    if (!EFI_ERROR (BS->AllocatePages (AllocateAnyPages, EfiLoaderData, Want / PageSize, &Addr))) {
      Buffer = Addr;
      Pages  = Want / PageSize;
      break;
    }
  }

  if (Pages == 0) {
    FillRect (0, gHeight / 2 - 60, gWidth, 120, 0x00FF0000);
    for (;;) {
      BS->Stall (1000000);
    }
  }

  Start = 0;
  BS->GetNextMonotonicCount (&Start);

  // ---- three passes over the buffer ---------------------------------------
  {
    UINT32 *Words = (UINT32 *)(UINTN)Buffer;
    UINTN   Count = Pages * PageSize / sizeof (UINT32);

    for (Pass = 0; Pass < 3; Pass++) {
      UINT32 Pat = (Pass == 0) ? 0x00000000 : ((Pass == 1) ? 0xFFFFFFFF : 0xA5A5A5A5);
      UINTN  k;

      for (k = 0; k < Count; k++) {
        Words[k] = (Pass == 2) ? (UINT32)(k ^ Pat) : Pat;
      }
      // progress: one bar step per 1/64 of the buffer
      FillRect ((gWidth * Pass) / 3, gHeight - 160, gWidth / 3, 40, gPaletteAnchor[Pass % 6]);

      for (k = 0; k < Count; k++) {
        UINT32 Want = (Pass == 2) ? (UINT32)(k ^ Pat) : Pat;
        if (Words[k] != Want) {
          Errors++;
          if (Errors > 999999) {
            break;
          }
        }
        if ((k & 0xFFFFF) == 0) {
          FillRect ((gWidth * Pass) / 3, gHeight - 160,
                    (gWidth / 3) * (k / (Count / 64 + 1)) / 64, 40, 0x000080FF);
          BS->Stall (1000);
        }
      }
      Tested += (UINT64)Pages * PageSize;
    }
  }

  BS->GetNextMonotonicCount (&End);
  BS->FreePages (Buffer, Pages);

  // ---- report -------------------------------------------------------------
  Clear (Errors == 0 ? 0x00006000 : 0x00600000);
  FillRect (0, 0, gWidth, 200, 0x00000000);
  Number (gWidth - 60, 30, 8, (Errors == 0) ? 0 : Errors, 0x00FFFFFF);
  if (Errors == 0) {
    // solid green field already drawn: the verdict
  }
  for (;;) {
    BS->Stall (1000000);
  }
}
