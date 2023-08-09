/** @file
  TPM Replay PEI TPM Initialized - Main Functionality Dependent on TPM Initialization

  This logic is primarily in PEI because the most straightforward interface to force events
  into the TCG Event Log (which is constructed in DXE) is via HOBs sent from PEI. Otherwise,
  Tcg2Dxe will preempt and build TCG Spec architectural events before this feature can intercept
  and insert the corresponding architectural events from the TPM replay event log.

  Copyright (c) Microsoft Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiPei.h>

#include <Guid/TcgEventHob.h>
#include <IndustryStandard/Tpm2Acpi.h>          // For locality code
#include <IndustryStandard/TpmPtp.h>            // For locality code
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/IoLib.h>
#include <Library/PcdLib.h>
#include <Library/PeiServicesLib.h>
#include <Library/ReportStatusCodeLib.h>
#include <Library/Tpm2CommandLib.h>
#include <Library/Tpm2DeviceLib.h>
#include <Ppi/TpmInitialized.h>
#include <Protocol/Tcg2Protocol.h>              // For macro definitions in the header file

#include <TpmReplayConfig.h>
#include "../InputChannel/TpmReplayInputChannel.h"
#include "../TpmReplayEventLog.h"
#include "../TpmReplayReportingManager.h"
#include "../TpmReplayTcg.h"
#include "../TpmReplayTcgRegs.h"
#include "TpmReplayPei.h"

// Note: This function only supports TPM2 (does not support TPM1.2)
STATIC
EFI_STATUS
AddNewTpmReplayTcgEvent (
  IN      TPML_DIGEST_VALUES  *DigestList,
  IN OUT  TCG_PCR_EVENT2_HDR  *NewEventHdr,
  IN      UINT8               *NewEventData
  )
{
  VOID                                *HobData;
  TCG_PCR_EVENT2                      *TcgPcrEvent2;
  UINT8                               *DigestBuffer;
  CONST TCG_EfiStartupLocalityEvent   *StartupLocalityEventPtr;

  if (DigestList == NULL || NewEventHdr == NULL || NewEventData == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  DEBUG ((DEBUG_INFO, "[%a] - sizeof (TcgPcrEvent2->PCRIndex) = 0x%x\n", __FUNCTION__, sizeof (TcgPcrEvent2->PCRIndex)));
  DEBUG ((DEBUG_INFO, "[%a] - sizeof (TcgPcrEvent2->EventType) = 0x%x\n", __FUNCTION__, sizeof (TcgPcrEvent2->EventType)));
  DEBUG ((DEBUG_INFO, "[%a] - GetDigestListSize (DigestList) = 0x%x\n", __FUNCTION__, GetDigestListSize (DigestList)));
  DEBUG ((DEBUG_INFO, "[%a] - sizeof (TcgPcrEvent2->EventSize) = 0x%x\n", __FUNCTION__, sizeof (TcgPcrEvent2->EventSize)));
  DEBUG ((DEBUG_INFO, "[%a] - NewEventHdr->EventSize = 0x%x\n", __FUNCTION__, NewEventHdr->EventSize));

  // Check for special event types
  if (NewEventHdr->EventType == EV_NO_ACTION) {
    // Check for events that need special handling
    if (IsStartupLocalityEvent (NewEventHdr, NewEventData)) {
      DEBUG ((DEBUG_INFO, "[%a] - Applying special handling for a EFI Startup Locality Event.\n", __FUNCTION__));

      StartupLocalityEventPtr = (TCG_EfiStartupLocalityEvent *)NewEventData;

      DEBUG ((DEBUG_INFO, "[%a] - Locality Found is %02d.\n", __FUNCTION__, StartupLocalityEventPtr->StartupLocality));
      // HobData = BuildGuidDataHob (
      //             &gTpm2StartupLocalityHobGuid,
      //             (VOID *)&StartupLocalityEventPtr->StartupLocality,
      //             sizeof (StartupLocalityEventPtr->StartupLocality)
      //             );
      // if (HobData == NULL) {
      //   return EFI_OUT_OF_RESOURCES;
      // }
      return EFI_SUCCESS;
    }
  }

  HobData = BuildGuidHob (
              &gTcgEvent2EntryHobGuid,
              sizeof (TcgPcrEvent2->PCRIndex) +
                sizeof (TcgPcrEvent2->EventType) +
                GetDigestListSize (DigestList) +
                sizeof (TcgPcrEvent2->EventSize) +
                NewEventHdr->EventSize
              );
  if (HobData == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  TcgPcrEvent2            = HobData;
  TcgPcrEvent2->PCRIndex  = NewEventHdr->PCRIndex;
  TcgPcrEvent2->EventType = NewEventHdr->EventType;
  DigestBuffer            = (UINT8 *)&TcgPcrEvent2->Digest;
  DigestBuffer            = CopyDigestListToBuffer (DigestBuffer, DigestList, PcdGet32 (PcdTpm2HashMask)); // TODO: Update to use supported PCR algos
  CopyMem (DigestBuffer, &NewEventHdr->EventSize, sizeof (TcgPcrEvent2->EventSize));
  DigestBuffer = DigestBuffer + sizeof (TcgPcrEvent2->EventSize);
  CopyMem (DigestBuffer, NewEventData, NewEventHdr->EventSize);

  return EFI_SUCCESS;
}

EFI_STATUS
BuildTpmReplayConfigHob (
  IN  ACTIVE_PCRS       ActivePcrs
  )
{
  TPM_REPLAY_CONFIG     Config;

  ZeroMem (&Config, sizeof (Config));

  Config.Signature  = TPM_REPLAY_CONFIG_SIGNATURE;
  Config.StructureVersion = TPM_REPLAY_CONFIG_STRUCT_VERSION;
  Config.HeaderLength = sizeof (Config);
  Config.ActivePcrs = ActivePcrs;

  if (BuildGuidDataHob (&gTpmReplayConfigHobGuid, &Config, sizeof (Config)) != NULL) {
    return EFI_SUCCESS;
  }

  return EFI_OUT_OF_RESOURCES;
}

EFI_STATUS
ReplayEventLog (
  IN  CONST TPM_REPLAY_EVENT_LOG  *ReplayEventLog
  )
{
  EFI_STATUS                      Status;
  UINT32                          EventIndex;
  UINT32                          DigestsPackedSize;
  UINT32                          EventsPackedSize;
  UINT32                          PcrSelectIndex;
  UINT32                          SelectedPcrs[PLATFORM_PCR];
  TPML_DIGEST_VALUES              CurrentDigestValues;
  TCG_PCR_EVENT2                  CurrentEvent;
  TPM_REPLAY_ERROR                Error;
  ACTIVE_PCRS                     ActivePcrs;
  VOID                            *CurrentEventData;
  CONST CALCULATED_PCR_STATE      *CurrentPcr;
  CONST PACKED_TCG_PCR_EVENT2     *EventLogMarker;

  Status = EFI_SUCCESS;
  Error = TpmReplayErrorUnknown;

  ActivePcrs.Data = 0;

  if (ReplayEventLog == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  DEBUG ((DEBUG_INFO, "[%a] - Beginning to replay the event log...\n", __func__));

  CurrentPcr = (CALCULATED_PCR_STATE *)((UINTN)ReplayEventLog + ReplayEventLog->OffsetToFinalPcrs);

  for (PcrSelectIndex = 0; PcrSelectIndex < ReplayEventLog->FinalPcrCount; PcrSelectIndex++) {
    // Now, before we process, we should unpack the current PCR digest values.
    REPORT_AND_RETURN_ON_CONDITION (
      !UnpackTpmlDigestValues ((PACKED_TPML_DIGEST_VALUES *)(CurrentPcr + 1), &CurrentDigestValues, &DigestsPackedSize),
      TpmReplayErrorDigestUnpackFailed,
      EFI_LOAD_ERROR
      );
    SelectedPcrs[PcrSelectIndex] = CurrentPcr->PcrIndex;

    EventLogMarker = (PACKED_TCG_PCR_EVENT2 *)((UINTN)ReplayEventLog + ReplayEventLog->OffsetToEventLog);
    for (EventIndex = 0; EventIndex < ReplayEventLog->EventLogCount; EventIndex++) {
      REPORT_AND_RETURN_ON_CONDITION (
        !UnpackTcgPcrEvent2 (EventLogMarker, &CurrentEvent, &EventsPackedSize, &CurrentEventData),
        TpmReplayErrorEventUnpackFailed,
        EFI_LOAD_ERROR
        );

      if (CurrentEvent.PCRIndex != SelectedPcrs[PcrSelectIndex]) {
        EventLogMarker = (PACKED_TCG_PCR_EVENT2 *)((UINTN)EventLogMarker + EventsPackedSize);
        continue;
      }

      DumpEvent (EventLogMarker);

      if (IsStartupLocalityEvent ((TCG_PCR_EVENT2_HDR *)&CurrentEvent, CurrentEventData)) {
        DEBUG ((DEBUG_INFO, "[%a] - Skipping digest extension for startup locality event.\n", __func__));
      } else {
        DEBUG ((DEBUG_INFO, "[%a] - Attempting to extend digest into PCR%d...\n", __func__, CurrentEvent.PCRIndex));
        Status = Tpm2PcrExtend (CurrentEvent.PCRIndex, &CurrentEvent.Digest);
        if (EFI_ERROR (Status)) {
          Error = TpmReplayErrorTpmExtendError;
          goto CheckForTpmError;
        }
        DEBUG ((DEBUG_INFO, "[%a] - Digest extended successfully!\n", __func__));
      }

      DEBUG ((DEBUG_INFO, "[%a] - Creating TCG Event Log Entry...\n", __func__));
      DEBUG ((DEBUG_INFO, "[%a] - Before going in CurrentEvent.EventSize = 0x%x...\n", __func__, CurrentEvent.EventSize));
      Status =  AddNewTpmReplayTcgEvent (
                  &CurrentDigestValues,
                  (TCG_PCR_EVENT2_HDR *)&CurrentEvent,
                  (UINT8 *)CurrentEventData
                  );
      if (EFI_ERROR (Status)) {
        Error = TpmReplayErrorEventLogEntryCreationFailure;
        goto CheckForTpmError;
      }
      ActivePcrs.Data |= (1 << CurrentEvent.PCRIndex);

      DEBUG ((DEBUG_INFO, "[%a] - TCG Event Log Entry Queued Successfully!\n", __func__));

      EventLogMarker = (PACKED_TCG_PCR_EVENT2 *)((UINTN)EventLogMarker + EventsPackedSize);
    }
    CurrentPcr = (CALCULATED_PCR_STATE *)((UINTN)CurrentPcr + sizeof (*CurrentPcr) + DigestsPackedSize);
  }

CheckForTpmError:
  if (Status == EFI_DEVICE_ERROR) {
    DEBUG ((DEBUG_ERROR, "[%a] - Creating TPM error HOB.\n", __func__));
    BuildGuidHob (&gTpmErrorHobGuid, 0);
    REPORT_STATUS_CODE (
      EFI_ERROR_CODE | EFI_ERROR_MINOR,
      (PcdGet32 (PcdStatusCodeSubClassTpmDevice) | EFI_P_EC_INTERFACE_ERROR)
      );
  }
  REPORT_AND_RETURN_IF_STATUS_ERROR (Status, Error, Status);

  Status = BuildTpmReplayConfigHob (ActivePcrs);

  return Status;
}

BOOLEAN
CheckTpmPreconditions (
  VOID
  )
{
  PTP_CRB_INTERFACE_IDENTIFIER    InterfaceId;
  BOOLEAN                         ConditionsMet;
  UINT32                          ResponseSize;
  UINT8*                          CommandBuffer;
  UINT8*                          ResponseBuffer;
  EFI_TPM2_ACPI_CONTROL_AREA      *Tpm2ControlArea;

  CommandBuffer = NULL;
  ConditionsMet = TRUE;
  ResponseBuffer = NULL;
  ResponseSize = 0;

  Tpm2ControlArea = (EFI_TPM2_ACPI_CONTROL_AREA *)(UINTN)FixedPcdGet32 (PcdTpmBaseAddress);

  DEBUG ((DEBUG_INFO, "  TPM control area: 0x%x\n", (UINTN)Tpm2ControlArea));
  if (Tpm2ControlArea == NULL) {
    ASSERT (Tpm2ControlArea != NULL);
    ConditionsMet = FALSE;
    goto Exit;
  }

  if (MmioRead8 ((UINTN)Tpm2ControlArea) == 0xFF) {
    DEBUG ((DEBUG_ERROR, "[%a] - TPM PTP is not present.\n", __func__));
    ConditionsMet = FALSE;
    goto Exit;
  }

  InterfaceId.Uint32         = MmioRead32 ((UINTN)&((PTP_CRB_REGISTERS *)Tpm2ControlArea)->InterfaceId);
  if (InterfaceId.Bits.InterfaceType != PTP_INTERFACE_IDENTIFIER_INTERFACE_TYPE_TIS) {
    ASSERT (InterfaceId.Bits.CapFIFO || InterfaceId.Bits.CapCRB);
  }

  if (Tpm2ControlArea->Error != 0) {
    DEBUG ((DEBUG_ERROR, "  Cannot check, the TPM is in an error state.\n"));
    ConditionsMet = FALSE;
    goto Exit;
  }

  CommandBuffer = (UINT8 *)(UINTN)Tpm2ControlArea->Command;
  ResponseBuffer = (UINT8 *)(UINTN)Tpm2ControlArea->Response;
  ResponseSize = (UINTN)Tpm2ControlArea->ResponseSize;

  DEBUG ((DEBUG_INFO, "  TPM command buffer address: 0x%x\n", (UINTN)CommandBuffer));
  DEBUG ((DEBUG_INFO, "  TPM response buffer address: 0x%x\n", (UINTN)ResponseBuffer));
  DEBUG ((DEBUG_INFO, "  TPM response size: 0x%x\n", ResponseSize));

Exit:
  return ConditionsMet;
}

//
// Basic input validation
//
EFI_STATUS
VerifyReplayEventLogPreConditions (
  IN  CONST TPM_REPLAY_EVENT_LOG    *ReplayEventLog
  )
{
  EFI_STATUS            Status;

  Status = EFI_SUCCESS;

  if (ReplayEventLog == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  DEBUG ((DEBUG_INFO, "[%a] - Beginning TPM Replay Log Pre-Condition Checks.\n", __FUNCTION__));

  DEBUG ((DEBUG_INFO, "[%a] - Confirming log integrity... ", __FUNCTION__));
  if (ReplayEventLog->StructureSignature != TPM_REPLAY_EVENT_LOG_STRUCTURE_SIGNATURE) {
    Status = EFI_LOAD_ERROR;
    goto EndOfChecks;
  }
  DEBUG ((DEBUG_INFO, "Pass\n"));

  DEBUG ((DEBUG_INFO, "[%a] - Checking structure size... ", __FUNCTION__));
  if (ReplayEventLog->StructureSize < sizeof (TPM_REPLAY_EVENT_LOG)) {
      Status = EFI_INVALID_PARAMETER;
      goto EndOfChecks;
  }
  DEBUG ((DEBUG_INFO, "Pass\n"));

  DEBUG ((DEBUG_INFO, "[%a] - Checking if final PCRs are present and valid... ", __FUNCTION__));
  if ((ReplayEventLog->FinalPcrCount != 0) || (ReplayEventLog->OffsetToFinalPcrs != ReplayEventLog->OffsetToEventLog)) {
    if (ReplayEventLog->OffsetToFinalPcrs < sizeof (TPM_REPLAY_EVENT_LOG)) {
      Status = EFI_INVALID_PARAMETER;
      goto EndOfChecks;
    }
    if (ReplayEventLog->FinalPcrCount == 0) {
      Status = EFI_INVALID_PARAMETER;
      goto EndOfChecks;
    }
    DEBUG ((DEBUG_INFO, "Pass - %d final PCR digests present\n", ReplayEventLog->FinalPcrCount));
  } else if ((ReplayEventLog->FinalPcrCount == 0) && (ReplayEventLog->OffsetToFinalPcrs == ReplayEventLog->OffsetToEventLog)) {
    DEBUG ((DEBUG_INFO, "Pass - No final PCR digests are present\n"));
  } else {
      Status = EFI_INVALID_PARAMETER;
      goto EndOfChecks;
  }

  DEBUG ((DEBUG_INFO, "[%a] - Validating that events are present... ", __FUNCTION__));
  if ((ReplayEventLog->EventLogCount == 0) || (ReplayEventLog->OffsetToEventLog < sizeof (TPM_REPLAY_EVENT_LOG))) {
    Status = EFI_NOT_FOUND;
    goto EndOfChecks;
  }
  DEBUG ((DEBUG_INFO, "Pass - %d event log entries present\n", ReplayEventLog->EventLogCount));

EndOfChecks:
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Fail - %r\n", Status));
    CpuDeadLoop (); // TODO - Remove later, here for testing now
  }
  DEBUG ((DEBUG_INFO, "[%a] - End of TPM Replay Log Pre-Condition Checks.\n", __FUNCTION__));

  return Status;
}

EFI_STATUS
VerifyTpmIsReady (
  VOID
  )
{
  EFI_STATUS            Status;
  UINT32                ActivePcrBanks;
  UINT32                TpmSupportedAlgorithms;
  TPML_PCR_SELECTION    Pcrs;

  DEBUG ((DEBUG_INFO, "[%a] - Beginning TPM Readiness Checks.\n", __FUNCTION__));

  // TODO - Change this surrounding messages to DEBUG_VERBOSE
  DEBUG ((DEBUG_INFO, "[%a] - Confirming TPM is available... ", __FUNCTION__));
  Status = Tpm2RequestUseTpm ();
  if (EFI_ERROR (Status)) {
    Status = EFI_NOT_FOUND;
    goto EndOfChecks;
  }
  DEBUG ((DEBUG_INFO, "Yes\n"));

  DEBUG ((DEBUG_INFO, "[%a] - Confirming TPM can pass self-test... ", __FUNCTION__));
  Status = Tpm2SelfTest (NO);
  if (EFI_ERROR (Status)) {
    Status = EFI_DEVICE_ERROR;
    goto EndOfChecks;
  }
  DEBUG ((DEBUG_INFO, "Yes\n"));

  DEBUG ((DEBUG_INFO, "[%a] - Checking PCR capabilities are accessible... ", __FUNCTION__));
  Status = Tpm2GetCapabilityPcrs (&Pcrs);
  if (EFI_ERROR (Status)) {
    Status = EFI_NOT_READY;
    goto EndOfChecks;
  }
  DEBUG ((DEBUG_INFO, "Yes\n"));

  DEBUG ((DEBUG_INFO, "[%a] - Checking TPM PCR bank and algorithm capabilities... ", __FUNCTION__));
  ActivePcrBanks = 0;
  TpmSupportedAlgorithms = 0;
  Status = Tpm2GetCapabilitySupportedAndActivePcrs (&TpmSupportedAlgorithms, &ActivePcrBanks);
  if (EFI_ERROR (Status)) {
    Status = EFI_UNSUPPORTED;
    goto EndOfChecks;
  }
  DEBUG ((DEBUG_INFO, "Good - information is available\n"));

  // Temporary
  DEBUG ((DEBUG_INFO, "  TpmSupportedAlgorithms = 0x%x\n", TpmSupportedAlgorithms));
  DEBUG ((DEBUG_INFO, "  ActivePcrBanks = 0x%x\n", ActivePcrBanks));

  // DEBUG ((DEBUG_INFO, "[%a] - Checking TPM preconditions...\n", __FUNCTION__));
  // if (!CheckTpmPreconditions ()) {
  //   Status = EFI_UNSUPPORTED;
  //   goto EndOfChecks;
  // }
  // DEBUG ((DEBUG_INFO, "[%a] TPM preconditions good.\n", __FUNCTION__));

EndOfChecks:
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "No - %r\n", Status));
    CpuDeadLoop (); // TODO - Remove later, here for testing now
  }
  DEBUG ((DEBUG_INFO, "[%a] - Completing TPM Readiness Checks.\n", __FUNCTION__));

  return Status;
}

/**
  Performs TCG actions that are dependent on TPM being initialized.

  @param[in]  PeiServices  Pointer to PEI Services Table.
  @param[in]  NotifyDesc   Pointer to the descriptor for the notification event that
                           caused callback to this function.
  @param[in]  NotifyPpi    Pointer to the PPI interface associated with this notify descriptor.

**/
EFI_STATUS
EFIAPI
TpmReplayTpmInitializedNotify (
  IN  EFI_PEI_SERVICES              **PeiServices,
  IN  EFI_PEI_NOTIFY_DESCRIPTOR     *NotifyDesc,
  IN  VOID                          *NotifyPpi
  )
{
  EFI_STATUS                        Status;
  TPM_REPLAY_ERROR                  Error;
  UINTN                             EventLogSize;
  TPM_REPLAY_EVENT_LOG              *EventLogData;

  DEBUG ((DEBUG_INFO, "[%a] - Entry\n", __FUNCTION__));

  Error = TpmReplayErrorUnknown;

  // 1. Verify TPM is ready
  Status = VerifyTpmIsReady ();
  REPORT_AND_RETURN_IF_STATUS_ERROR (Status, TpmReplayErrorTpmNotReady, EFI_NOT_READY);

  // 2. Get Replay Event Log Data
  Status = GetReplayEventLog (&EventLogData, &EventLogSize);
  REPORT_AND_RETURN_IF_STATUS_ERROR (Status, TpmReplayErrorReplayEventLogRetrievalFailure, EFI_LOAD_ERROR);

  // 3. Verify Replay Event Log is Valid
  Status = VerifyReplayEventLogPreConditions (EventLogData);
  REPORT_AND_RETURN_IF_STATUS_ERROR (Status, TpmReplayErrorReplayEventLogInvalid, EFI_UNSUPPORTED);

  // 4. Replay the Event Log
  Status = ReplayEventLog (EventLogData);
  REPORT_AND_RETURN_IF_STATUS_ERROR (Status, Error, EFI_DEVICE_ERROR);

  DEBUG ((DEBUG_INFO, "[%a] - PCR measurements successfully made!\n", __FUNCTION__));

  return EFI_SUCCESS;
}
