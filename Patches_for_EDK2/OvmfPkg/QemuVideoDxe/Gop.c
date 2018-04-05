/** @file
  Graphics Output Protocol functions for the QEMU video controller.

  Copyright (c) 2007 - 2016, Intel Corporation. All rights reserved.<BR>

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD License
  which accompanies this distribution. The full text of the license may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "Qemu.h"

STATIC
VOID
QemuVideoCompleteModeInfo (
  IN  QEMU_VIDEO_MODE_DATA           *ModeData,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info
  )
{
  Info->Version = 0;
  if (ModeData->ColorDepth == 8) {
    Info->PixelFormat = PixelBitMask;
    Info->PixelInformation.RedMask = PIXEL_RED_MASK;
    Info->PixelInformation.GreenMask = PIXEL_GREEN_MASK;
    Info->PixelInformation.BlueMask = PIXEL_BLUE_MASK;
    Info->PixelInformation.ReservedMask = 0;
  } else if (ModeData->ColorDepth == 24) {
    Info->PixelFormat = PixelBitMask;
    Info->PixelInformation.RedMask = PIXEL24_RED_MASK;
    Info->PixelInformation.GreenMask = PIXEL24_GREEN_MASK;
    Info->PixelInformation.BlueMask = PIXEL24_BLUE_MASK;
    Info->PixelInformation.ReservedMask = 0;
  } else if (ModeData->ColorDepth == 32) {
    DEBUG ((EFI_D_INFO, "PixelBlueGreenRedReserved8BitPerColor\n"));
    Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
  }
  Info->PixelsPerScanLine = Info->HorizontalResolution;
}


STATIC
EFI_STATUS
QemuVideoCompleteModeData (
  IN  QEMU_VIDEO_PRIVATE_DATA           *Private,
  OUT EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode
  )
{
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR     *FrameBufDesc;
  QEMU_VIDEO_MODE_DATA           *ModeData;

  ModeData = &Private->ModeData[Mode->Mode];
  Info = Mode->Info;
  QemuVideoCompleteModeInfo (ModeData, Info);

  Private->PciIo->GetBarAttributes (
                        Private->PciIo,
                        0,
                        NULL,
                        (VOID**) &FrameBufDesc
                        );

  Mode->FrameBufferBase = FrameBufDesc->AddrRangeMin;
  Mode->FrameBufferSize = Info->HorizontalResolution * Info->VerticalResolution;
  Mode->FrameBufferSize = Mode->FrameBufferSize * ((ModeData->ColorDepth + 7) / 8);
  DEBUG ((EFI_D_INFO, "FrameBufferBase: 0x%Lx, FrameBufferSize: 0x%Lx\n",
    Mode->FrameBufferBase, (UINT64)Mode->FrameBufferSize));

  FreePool (FrameBufDesc);
  return EFI_SUCCESS;
}


//
// Graphics Output Protocol Member Functions
//
EFI_STATUS
EFIAPI
QemuVideoGraphicsOutputQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
/*++

Routine Description:

  Graphics Output protocol interface to query video mode

  Arguments:
    This                  - Protocol instance pointer.
    ModeNumber            - The mode number to return information on.
    Info                  - Caller allocated buffer that returns information about ModeNumber.
    SizeOfInfo            - A pointer to the size, in bytes, of the Info buffer.

  Returns:
    EFI_SUCCESS           - Mode information returned.
    EFI_BUFFER_TOO_SMALL  - The Info buffer was too small.
    EFI_DEVICE_ERROR      - A hardware error occurred trying to retrieve the video mode.
    EFI_NOT_STARTED       - Video display is not initialized. Call SetMode ()
    EFI_INVALID_PARAMETER - One of the input args was NULL.

--*/
{
  QEMU_VIDEO_PRIVATE_DATA  *Private;
  QEMU_VIDEO_MODE_DATA     *ModeData;

  Private = QEMU_VIDEO_PRIVATE_DATA_FROM_GRAPHICS_OUTPUT_THIS (This);

  if (Info == NULL || SizeOfInfo == NULL || ModeNumber >= This->Mode->MaxMode) {
    return EFI_INVALID_PARAMETER;
  }

  *Info = AllocatePool (sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION));
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  ModeData = &Private->ModeData[ModeNumber];
  (*Info)->HorizontalResolution = ModeData->HorizontalResolution;
  (*Info)->VerticalResolution   = ModeData->VerticalResolution;
  QemuVideoCompleteModeInfo (ModeData, *Info);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
QemuVideoGraphicsOutputSetMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  IN  UINT32                       ModeNumber
  )
/*++

Routine Description:

  Graphics Output protocol interface to set video mode

  Arguments:
    This             - Protocol instance pointer.
    ModeNumber       - The mode number to be set.

  Returns:
    EFI_SUCCESS      - Graphics mode was changed.
    EFI_DEVICE_ERROR - The device had an error and could not complete the request.
    EFI_UNSUPPORTED  - ModeNumber is not supported by this device.

--*/
{
  QEMU_VIDEO_PRIVATE_DATA    *Private;
  QEMU_VIDEO_MODE_DATA       *ModeData;
  RETURN_STATUS              Status;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black;
  BOOLEAN                    VideoModeChanged;

  Private = QEMU_VIDEO_PRIVATE_DATA_FROM_GRAPHICS_OUTPUT_THIS (This);

  if (ModeNumber >= This->Mode->MaxMode) {
    return EFI_UNSUPPORTED;
  }

  ModeData = &Private->ModeData[ModeNumber];

  if (Private->LineBuffer) {
    gBS->FreePool (Private->LineBuffer);
  }

  Private->LineBuffer = AllocatePool (4 * ModeData->HorizontalResolution);
  if (Private->LineBuffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  switch (Private->Variant) {
  case QEMU_VIDEO_CIRRUS_5430:
  case QEMU_VIDEO_CIRRUS_5446:
    InitializeCirrusGraphicsMode (Private, &QemuVideoCirrusModes[ModeData->InternalModeIndex]);
    break;
  case QEMU_VIDEO_BOCHS_MMIO:
  case QEMU_VIDEO_BOCHS:
    InitializeBochsGraphicsMode (Private, &QemuVideoBochsModes[ModeData->InternalModeIndex]);
    break;
  default:
    ASSERT (FALSE);
    gBS->FreePool (Private->LineBuffer);
    Private->LineBuffer = NULL;
    return EFI_DEVICE_ERROR;
  }

  // Check if this is a new mode
  VideoModeChanged = (This->Mode->Mode != ModeNumber);

  This->Mode->Mode = ModeNumber;
  This->Mode->Info->HorizontalResolution = ModeData->HorizontalResolution;
  This->Mode->Info->VerticalResolution = ModeData->VerticalResolution;
  This->Mode->SizeOfInfo = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  QemuVideoCompleteModeData (Private, This->Mode);

  //
  // Allocate when using first time.
  //
  if (Private->FrameBufferBltConfigure == NULL) {
    Status = FrameBufferBltConfigure (
               (VOID*) (UINTN) This->Mode->FrameBufferBase,
               This->Mode->Info,
               Private->FrameBufferBltConfigure,
               &Private->FrameBufferBltConfigureSize
    );
    ASSERT (Status == RETURN_BUFFER_TOO_SMALL);
    Private->FrameBufferBltConfigure =
      AllocatePool (Private->FrameBufferBltConfigureSize);
  }

  if (VideoModeChanged) {
    UINTN                    Index;
    UINTN                    HandleCount;
    EFI_HANDLE               *HandleBuffer;
    EFI_STATUS               Status;

    //
    // Set PCDs to Inform GraphicsConsole of video resolution.
    //    
 //   PcdSet32 (PcdVideoHorizontalResolution, This->Mode->Info->HorizontalResolution);
//    PcdSet32 (PcdVideoVerticalResolution, This->Mode->Info->VerticalResolution);

    //
    // Video mode is changed, so restart graphics console driver and higher level driver.
    // Reconnect graphics console driver and higher level driver.
    // Locate all the handles with GOP protocol and reconnect it.
    //
    Status = gBS->LocateHandleBuffer (
                     ByProtocol,
                     &gEfiSimpleTextOutProtocolGuid,
                     NULL,
                     &HandleCount,
                     &HandleBuffer
                     );
    if (!EFI_ERROR (Status)) {
      for (Index = 0; Index < HandleCount; Index++) {
        gBS->DisconnectController (HandleBuffer[Index], NULL, NULL);
      }
      for (Index = 0; Index < HandleCount; Index++) {
        gBS->ConnectController (HandleBuffer[Index], NULL, NULL, TRUE);
      }
      if (HandleBuffer != NULL) {
        FreePool (HandleBuffer);
      }
    }
  }

  //
  // Per UEFI Spec, need to clear the visible portions of the output display to black.
  //
  ZeroMem (&Black, sizeof (Black));
  Status = FrameBufferBlt (
             Private->FrameBufferBltConfigure,
             &Black,
             EfiBltVideoFill,
             0, 0,
             0, 0,
             This->Mode->Info->HorizontalResolution, This->Mode->Info->VerticalResolution,
             0
             );
  ASSERT_RETURN_ERROR (Status);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
QemuVideoGraphicsOutputBlt (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  EFI_GRAPHICS_OUTPUT_BLT_PIXEL         *BltBuffer, OPTIONAL
  IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION     BltOperation,
  IN  UINTN                                 SourceX,
  IN  UINTN                                 SourceY,
  IN  UINTN                                 DestinationX,
  IN  UINTN                                 DestinationY,
  IN  UINTN                                 Width,
  IN  UINTN                                 Height,
  IN  UINTN                                 Delta
  )
/*++

Routine Description:

  Graphics Output protocol instance to block transfer for CirrusLogic device

Arguments:

  This          - Pointer to Graphics Output protocol instance
  BltBuffer     - The data to transfer to screen
  BltOperation  - The operation to perform
  SourceX       - The X coordinate of the source for BltOperation
  SourceY       - The Y coordinate of the source for BltOperation
  DestinationX  - The X coordinate of the destination for BltOperation
  DestinationY  - The Y coordinate of the destination for BltOperation
  Width         - The width of a rectangle in the blt rectangle in pixels
  Height        - The height of a rectangle in the blt rectangle in pixels
  Delta         - Not used for EfiBltVideoFill and EfiBltVideoToVideo operation.
                  If a Delta of 0 is used, the entire BltBuffer will be operated on.
                  If a subrectangle of the BltBuffer is used, then Delta represents
                  the number of bytes in a row of the BltBuffer.

Returns:

  EFI_INVALID_PARAMETER - Invalid parameter passed in
  EFI_SUCCESS - Blt operation success

--*/
{
  EFI_STATUS                      Status;
  EFI_TPL                         OriginalTPL;
  QEMU_VIDEO_PRIVATE_DATA         *Private;

  Private = QEMU_VIDEO_PRIVATE_DATA_FROM_GRAPHICS_OUTPUT_THIS (This);
  //
  // We have to raise to TPL Notify, so we make an atomic write the frame buffer.
  // We would not want a timer based event (Cursor, ...) to come in while we are
  // doing this operation.
  //
  OriginalTPL = gBS->RaiseTPL (TPL_NOTIFY);

  switch (BltOperation) {
  case EfiBltVideoToBltBuffer:
  case EfiBltBufferToVideo:
  case EfiBltVideoFill:
  case EfiBltVideoToVideo:
    Status = FrameBufferBlt (
      Private->FrameBufferBltConfigure,
      BltBuffer,
      BltOperation,
      SourceX,
      SourceY,
      DestinationX,
      DestinationY,
      Width,
      Height,
      Delta
      );
    break;

  default:
    Status = EFI_INVALID_PARAMETER;
    break;
  }

  gBS->RestoreTPL (OriginalTPL);

  return Status;
}

EFI_STATUS
QemuVideoGraphicsOutputConstructor (
  QEMU_VIDEO_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS                   Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutput;
  UINT32	               ModeNumber;
  UINT32                       InitialModeNumber;

  GraphicsOutput            = &Private->GraphicsOutput;
  GraphicsOutput->QueryMode = QemuVideoGraphicsOutputQueryMode;
  GraphicsOutput->SetMode   = QemuVideoGraphicsOutputSetMode;
  GraphicsOutput->Blt       = QemuVideoGraphicsOutputBlt;

  //
  // Initialize the private data
  //
  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  sizeof (EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE),
                  (VOID **) &Private->GraphicsOutput.Mode
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->AllocatePool (
                  EfiBootServicesData,
                  sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),
                  (VOID **) &Private->GraphicsOutput.Mode->Info
                  );
  if (EFI_ERROR (Status)) {
    goto FreeMode;
  }
  Private->GraphicsOutput.Mode->MaxMode = (UINT32) Private->MaxMode;
  Private->GraphicsOutput.Mode->Mode    = GRAPHICS_OUTPUT_INVALIDE_MODE_NUMBER;
  Private->LineBuffer                   = NULL;
  Private->FrameBufferBltConfigure      = NULL;
  Private->FrameBufferBltConfigureSize  = 0;

  //
  // Initialize the hardware
  //

  // Search for 800x600 resolution for initial mode
  InitialModeNumber = 0;
  for (ModeNumber = 0; ModeNumber < Private->MaxMode; ModeNumber++) {
    if (Private->ModeData[ModeNumber].HorizontalResolution == 800 && Private->ModeData[ModeNumber].VerticalResolution == 600) {
      InitialModeNumber = ModeNumber;
      break;
    }
  }

  Status = GraphicsOutput->SetMode (GraphicsOutput, InitialModeNumber);
  if (EFI_ERROR (Status)) {
    goto FreeInfo;
  }

  DrawLogo (
    Private,
    Private->ModeData[Private->GraphicsOutput.Mode->Mode].HorizontalResolution,
    Private->ModeData[Private->GraphicsOutput.Mode->Mode].VerticalResolution
    );

  return EFI_SUCCESS;

FreeInfo:
  FreePool (Private->GraphicsOutput.Mode->Info);

FreeMode:
  FreePool (Private->GraphicsOutput.Mode);
  Private->GraphicsOutput.Mode = NULL;

  return Status;
}

EFI_STATUS
QemuVideoGraphicsOutputDestructor (
  QEMU_VIDEO_PRIVATE_DATA  *Private
  )
/*++

Routine Description:

Arguments:

Returns:

  None

--*/
{
  if (Private->LineBuffer != NULL) {
    FreePool (Private->LineBuffer);
  }

  if (Private->FrameBufferBltConfigure != NULL) {
    FreePool (Private->FrameBufferBltConfigure);
  }

  if (Private->GraphicsOutput.Mode != NULL) {
    if (Private->GraphicsOutput.Mode->Info != NULL) {
      gBS->FreePool (Private->GraphicsOutput.Mode->Info);
    }
    gBS->FreePool (Private->GraphicsOutput.Mode);
  }

  return EFI_SUCCESS;
}


