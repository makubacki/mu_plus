/** @file
  TPM Replay Reporting Manager - Manages reporting of TPM Replay status.

  Copyright (c) Microsoft Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "TpmReplayReportingManager.h"

#include <Uefi.h>
#include <Library/DebugLib.h>

VOID
ReportTpmReplayError (
  IN  TPM_REPLAY_ERROR        Error,
  IN  CONST CHAR8             *FunctionName   OPTIONAL
  )
{
  CONST CHAR8   *DebugFunctionName;

  DebugFunctionName = __func__;

  if (FunctionName != NULL) {
    DebugFunctionName = FunctionName;
  }

  DEBUG ((DEBUG_ERROR, "[%a] - TPM Replay error reported (%d).\n", DebugFunctionName, Error));
  ASSERT (FALSE);

  return;
}
