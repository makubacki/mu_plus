/** @file
  TPM Replay - TCG Definitions

  Definitions needed for TCG operations.

  Copyright (c) Microsoft Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patents

**/

#ifndef TPM_REPLAY_TCG_H_
#define TPM_REPLAY_TCG_H_

#include <Base.h>
#include "TpmReplayTcgRegs.h"

#define TCG_PCR_0  0
#define TCG_PCR_1  1
#define TCG_PCR_2  2
#define TCG_PCR_3  3
#define TCG_PCR_4  4
#define TCG_PCR_5  5
#define TCG_PCR_6  6
#define TCG_PCR_7  7

typedef  UINT8  *PACKED_TPML_DIGEST_VALUES;
typedef  UINT8  *PACKED_TCG_PCR_EVENT2;

VOID
DumpEvent (
  IN CONST  PACKED_TCG_PCR_EVENT2  *PackedEvent
  );

CONST TPMT_HA *
FindSelectedAlgorithm (
  IN CONST TPML_DIGEST_VALUES  *DigestValues,
  IN       TPMI_ALG_HASH       HashAlg
  );

CONST PACKED_TCG_PCR_EVENT2 *
GetNextMatchingEvent (
  IN UINT32                       PcrIndex,
  IN CONST PACKED_TCG_PCR_EVENT2  *StartEvent,
  IN CONST PACKED_TCG_PCR_EVENT2  *LastEvent,
  IN OUT   UINT32                 *EventIndex OPTIONAL
  );

UINTN
GetPcrEvent2Size (
  IN CONST  TCG_PCR_EVENT2  *TcgPcrEvent2
  );

UINTN
GetTcgEfiSpecIdEventStructSize (
  IN  CONST  TCG_EfiSpecIDEventStruct  *TcgEfiSpecIdEventStruct
  );

BOOLEAN
IsStartupLocalityEvent (
  IN  CONST TCG_PCR_EVENT2_HDR  *TcgPcrEventHdr,
  IN  CONST VOID                *TcgPcrEventData
  );

BOOLEAN
UnpackTcgPcrEvent2 (
  IN CONST PACKED_TCG_PCR_EVENT2  *PackedEvent,
  OUT      TCG_PCR_EVENT2         *UnpackedEvent,
  OUT      UINT32                 *PackedSize OPTIONAL,
  OUT      VOID                   **EventData OPTIONAL
  );

BOOLEAN
UnpackTpmlDigestValues (
  IN CONST PACKED_TPML_DIGEST_VALUES  *PackedValues,
  OUT      TPML_DIGEST_VALUES         *UnpackedValues,
  OUT      UINT32                     *PackedSize
  );

#endif
