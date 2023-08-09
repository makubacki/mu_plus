/** @file
  Firmware Volume Measuremeent Exclusion Library

  Provides a simple interface for platforms to have a list of firmware volumes
  excluded from measurement by the traditional TCG driver infrastructure.

  Copyright (c) Microsoft Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef FV_MEASUREMENT_EXCLUSION_LIB_H
#define FV_MEASUREMENT_EXCLUSION_LIB_H

#include <Ppi/FirmwareVolumeInfoMeasurementExcluded.h>

EFI_STATUS
EFIAPI
GetPlatformFvExclusions (
  OUT CONST EFI_PEI_FIRMWARE_VOLUME_INFO_MEASUREMENT_EXCLUDED_FV    **ExcludedFvs,
  OUT UINTN                                                         *ExcludedFvsCount
  );

#endif
