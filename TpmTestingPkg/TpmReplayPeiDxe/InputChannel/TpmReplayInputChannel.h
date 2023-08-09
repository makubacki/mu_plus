/** @file
  TPM Replay Generic Input Channel Header

  Copyright (c) Microsoft Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef TPM_REPLAY_INPUT_CHANNEL_H_
#define TPM_REPLAY_INPUT_CHANNEL_H_

#include "../TpmReplayEventLog.h"

EFI_STATUS
GetReplayEventLog (
  OUT TPM_REPLAY_EVENT_LOG    **ReplayEventLog,
  OUT UINTN                   *ReplayEventLogSize
  );

#endif
