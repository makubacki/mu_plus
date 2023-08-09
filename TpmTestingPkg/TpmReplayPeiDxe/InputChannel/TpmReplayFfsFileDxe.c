/** @file
  TPM Replay DXE FFS File Logic

  Copyright (c) Microsoft Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiDxe.h>
#include <Library/DebugLib.h>

#include "../TpmReplayEventLog.h"
#include "TpmReplayInputChannelInternal.h"

EFI_STATUS
GetTpmReplayEventLogFfsFile (
  OUT VOID      **Data,
  OUT UINTN     *DataSize
  )
{
  // Todo: Add DXE implementation
  return EFI_UNSUPPORTED;
}
