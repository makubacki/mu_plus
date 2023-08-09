/** @file
  Firmware Volume Measurement Measurement Exclusion Library NULL instance.

  This library instance does not exclude any firmware volumes.

  Copyright (c) Microsoft Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/FvMeasurementExclusionLib.h>

EFI_STATUS
EFIAPI
GetPlatformFvExclusions (
  OUT CONST EFI_PEI_FIRMWARE_VOLUME_INFO_MEASUREMENT_EXCLUDED_FV    **ExcludedFvs,
  OUT UINTN                                                         *ExcludedFvsCount
  )
{
  return EFI_SUCCESS;
}
