/** @file
  TPM Replay PEI FFS File Logic

  Copyright (c) Microsoft Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiPei.h>
#include <Library/DebugLib.h>
#include <Library/PeiServicesLib.h>

#include "../TpmReplayEventLog.h"
#include "TpmReplayInputChannelInternal.h"

EFI_STATUS
GetTpmReplayEventLogFfsFile (
  OUT VOID      **Data,
  OUT UINTN     *DataSize
  )
{
  EFI_STATUS                  Status;
  EFI_STATUS                  FindFvStatus;
  UINTN                       FvInstance;
  EFI_PEI_FILE_HANDLE         FileHandle;
  EFI_PEI_FV_HANDLE           FvHandle;
  EFI_FV_INFO                 FvInfo;
  EFI_COMMON_SECTION_HEADER   *SectionHeader;
  VOID                        *SectionData;

  for (FvInstance = 0, FindFvStatus = EFI_SUCCESS; !EFI_ERROR (FindFvStatus); FvInstance++) {
    FindFvStatus = PeiServicesFfsFindNextVolume (FvInstance, &FvHandle);
    if (!EFI_ERROR (FindFvStatus)) {
      ASSERT (((EFI_FIRMWARE_VOLUME_HEADER *)FvHandle)->Signature == EFI_FVH_SIGNATURE);

      Status = PeiServicesFfsGetVolumeInfo (FvHandle, &FvInfo);
      if (!EFI_ERROR (Status)) {
        DEBUG ((DEBUG_VERBOSE, "[%a] Current FV Name = %g\n", __FUNCTION__, &FvInfo.FvName));

        FileHandle = NULL;
        Status = PeiServicesFfsFindFileByName (&gTpmReplayVendorGuid, FvHandle, &FileHandle);
        if (!EFI_ERROR (Status)) {
          Status = PeiServicesFfsFindSectionData (EFI_SECTION_RAW, FileHandle, &SectionData);
          if (!EFI_ERROR (Status)) {
            SectionHeader = (EFI_COMMON_SECTION_HEADER *)((UINTN)SectionData - sizeof (EFI_RAW_SECTION2));
            if (IS_SECTION2 (SectionHeader)) {
              *DataSize = SECTION2_SIZE (SectionHeader);
            } else {
              SectionHeader = (EFI_COMMON_SECTION_HEADER *)((UINTN)SectionData - sizeof (EFI_RAW_SECTION));
              *DataSize = SECTION_SIZE (SectionHeader);
            }

            if (SectionHeader->Type != EFI_SECTION_RAW) {
              ASSERT (SectionHeader->Type == EFI_SECTION_RAW);
              return EFI_COMPROMISED_DATA;
            }
            *Data = SectionData;

            return EFI_SUCCESS;
          }
        }
      }
    }
  }

  return EFI_NOT_FOUND;
}
