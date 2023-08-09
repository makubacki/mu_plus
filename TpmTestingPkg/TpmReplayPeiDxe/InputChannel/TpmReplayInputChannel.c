/** @file
  TPM Replay Generic Input Channel Logic

  Copyright (c) Microsoft Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>
#include <Library/DebugLib.h>

#include "../TpmReplayEventLog.h"
#include "TpmReplayInputChannel.h"
#include "TpmReplayInputChannelInternal.h"

EFI_STATUS
GetReplayEventLog (
  OUT TPM_REPLAY_EVENT_LOG    **ReplayEventLog,
  OUT UINTN                   *ReplayEventLogSize
  )
{
  EFI_STATUS    Status;
  UINTN         ReplayEventLogDataSize;
  VOID          *ReplayEventLogData;

  ReplayEventLogData = NULL;
  ReplayEventLogDataSize = 0;

  // First priority: UEFI variable set on the DUT
  Status = GetTpmReplayEventLogUefiVariable (&ReplayEventLogData, &ReplayEventLogDataSize);
  ASSERT (Status == EFI_SUCCESS || Status == EFI_NOT_FOUND);
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "[%a] - Using TPM replay event log from UEFI variable.\n", __func__));
    goto Done;
  }

  // Second priority: FFS in the FW image
  Status = GetTpmReplayEventLogFfsFile (&ReplayEventLogData, &ReplayEventLogDataSize);
  ASSERT (Status == EFI_SUCCESS || Status == EFI_NOT_FOUND);
  if (!EFI_ERROR (Status)) {
    goto Done;
  }

  Status = EFI_NOT_FOUND;

Done:
  *ReplayEventLog = ReplayEventLogData;
  *ReplayEventLogSize = ReplayEventLogDataSize;

  return Status;
}
