/** @file
  SMM_CORE implementation of Advanced Logger Library.

  Copyright (c) Microsoft Corporation. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiDxe.h>

#include <AdvancedLoggerInternal.h>

#include <Protocol/AdvancedLogger.h>
#include <AdvancedLoggerInternalProtocol.h>

#include <Pi/PiSmmCis.h>

#include <Library/AdvancedLoggerLib.h>
#include <Library/DebugLib.h>
#include <Library/SynchronizationLib.h>
#include <Library/SmmServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>

#include "../AdvancedLoggerCommon.h"

STATIC ADVANCED_LOGGER_INFO  *mLoggerInfo                 = NULL;
STATIC UINT32                mBufferSize                  = 0;
STATIC EFI_PHYSICAL_ADDRESS  mMaxAddress                  = 0;
STATIC BOOLEAN               mInitialized                 = FALSE;
STATIC EFI_EVENT             mAdvancedLoggerProtocolEvent = NULL;
STATIC VOID                  *mAdvancedLoggerProtocolReg  = NULL;

VOID
EFIAPI
AdvancedLoggerWriteProtocol (
  IN        ADVANCED_LOGGER_PROTOCOL  *This,
  IN        UINTN                     ErrorLevel,
  IN  CONST CHAR8                     *Buffer,
  IN        UINTN                     NumberOfBytes
  );

STATIC ADVANCED_LOGGER_PROTOCOL_CONTAINER  mAdvLoggerProtocol = {
  .AdvLoggerProtocol             = {
    .Signature                   = ADVANCED_LOGGER_PROTOCOL_SIGNATURE,
    .Version                     = ADVANCED_LOGGER_PROTOCOL_VERSION,
    .AdvancedLoggerWriteProtocol = AdvancedLoggerWriteProtocol
  },
  .LoggerInfo                    = NULL
};

/**
  AdvancedLoggerWriteProtocol

  @param  This            Pointer to Advanced Logger Protocol,
  @param  ErrorLevel      The error level of the debug message.
  @param  Buffer          The debug message to log.
  @param  NumberOfBytes   Number of bytes in the debug message.

**/
VOID
EFIAPI
AdvancedLoggerWriteProtocol (
  IN        ADVANCED_LOGGER_PROTOCOL  *This,
  IN        UINTN                     ErrorLevel,
  IN  CONST CHAR8                     *Buffer,
  IN        UINTN                     NumberOfBytes
  )
{
  AdvancedLoggerWrite (ErrorLevel, Buffer, NumberOfBytes);
}

/**
    CheckAddress

    The address of the ADVANCE_LOGGER_INFO block pointer is captured before END_OF_DXE.
    LogBuffer, LogCurrentOffset, and LogBufferSize could be written to by un trusted code.  Here, we check that
    the offsets are within the allocated mLoggerInfo space, and that LogBufferSize, which is used in multiple places
    to see if a new message will fit into the log buffer, is valid.

    @param          NONE

    @return         BOOLEAN     TRUE - mInforBlock passes security checks
    @return         BOOLEAN     FALSE- mInforBlock failed security checks

**/
STATIC
BOOLEAN
ValidateInfoBlock (
  VOID
  )
{
  if (mLoggerInfo == NULL) {
    return FALSE;
  }

  if (mLoggerInfo->Signature != ADVANCED_LOGGER_SIGNATURE) {
    return FALSE;
  }

  if (mLoggerInfo->LogBufferOffset != EXPECTED_LOG_BUFFER_OFFSET (mLoggerInfo)) {
    return FALSE;
  }

  if ((PA_FROM_PTR (LOG_CURRENT_FROM_ALI (mLoggerInfo)) > mMaxAddress) ||
      (mLoggerInfo->LogCurrentOffset < mLoggerInfo->LogBufferOffset))
  {
    return FALSE;
  }

  if (mBufferSize == 0) {
    mBufferSize = mLoggerInfo->LogBufferSize;
  } else {
    if (mLoggerInfo->LogBufferSize != mBufferSize) {
      return FALSE;
    }
  }

  return TRUE;
}

/**
  Update cached logger info from the Advanced Logger Protocol.

  @retval EFI_SUCCESS           Logger info successfully updated.
  @retval EFI_NOT_FOUND         The advanced logger protocol was not found.
  @retval EFI_INVALID_PARAMETER Invalid protocol signature or version.

**/
STATIC
EFI_STATUS
UpdateLoggerInfoFromProtocol (
  VOID
  )
{
  ADVANCED_LOGGER_PROTOCOL  *LoggerProtocol;
  EFI_STATUS                Status;

  Status = gBS->LocateProtocol (
                  &gAdvancedLoggerProtocolGuid,
                  NULL,
                  (VOID **)&LoggerProtocol
                  );
  if (EFI_ERROR (Status) || (LoggerProtocol == NULL)) {
    return EFI_NOT_FOUND;
  }

  ASSERT (LoggerProtocol->Signature == ADVANCED_LOGGER_PROTOCOL_SIGNATURE);
  ASSERT (LoggerProtocol->Version == ADVANCED_LOGGER_PROTOCOL_VERSION);

  if ((LoggerProtocol->Signature != ADVANCED_LOGGER_PROTOCOL_SIGNATURE) ||
      (LoggerProtocol->Version != ADVANCED_LOGGER_PROTOCOL_VERSION))
  {
    return EFI_INVALID_PARAMETER;
  }

  mLoggerInfo = LOGGER_INFO_FROM_PROTOCOL (LoggerProtocol);

  if (mLoggerInfo != NULL) {
    mMaxAddress = LOG_MAX_ADDRESS (mLoggerInfo);
    //
    // Force buffer size to be re-evaluated
    //
    mBufferSize = 0;
  }

  return EFI_SUCCESS;
}

/**
  Notification function for Advanced Logger Protocol installation.

  @param[in]  Event    Event whose notification function is being invoked.
  @param[in]  Context  Pointer to the notification function's context.
**/
STATIC
VOID
EFIAPI
OnAdvancedLoggerProtocolNotification (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;

  Status = UpdateLoggerInfoFromProtocol ();
  if (!EFI_ERROR (Status)) {
    DEBUG ((DEBUG_INFO, "%a: Logger buffer updated. LoggerInfo=%p\n", __func__, mLoggerInfo));
    mAdvLoggerProtocol.LoggerInfo = mLoggerInfo;
  }
}

/**
    Get the Logger Information Block published by DxeCore.
 **/
STATIC
VOID
SmmInitializeLoggerInfo (
  VOID
  )
{
  EFI_STATUS  Status;

  if (!mInitialized) {
    //
    // Locate the Logger Information block.
    //
    if (gBS == NULL) {
      return;
    }

    mInitialized = TRUE;

    Status = UpdateLoggerInfoFromProtocol ();
    ASSERT_EFI_ERROR (Status);

    //
    // If mLoggerInfo is NULL at this point, there is no Advanced Logger.
    //

    DEBUG ((DEBUG_INFO, "%a: LoggerInfo=%p, Code=%r\n", __func__, mLoggerInfo, Status));
  }

  if (!ValidateInfoBlock ()) {
    mLoggerInfo = NULL;
  }
}

/**
  Return the Logger Information block

**/
ADVANCED_LOGGER_INFO *
EFIAPI
AdvancedLoggerGetLoggerInfo (
  VOID
  )
{
  SmmInitializeLoggerInfo ();

  return mLoggerInfo;
}

/**
  Helper function to return the log phase for each message.

  This function is intended to be used to distinguish between
  various types of modules.

  @return       Phase of current advanced logger instance.
**/
UINT16
EFIAPI
AdvancedLoggerGetPhase (
  VOID
  )
{
  return ADVANCED_LOGGER_PHASE_SMM_CORE;
}

/**
  The constructor function initializes Logger Information pointer to ensure that the
  pointer is initialized in DXE - either by the constructor, or the first DEBUG message.
  It is also responsible for publishing the Smm Advanced Logger protocol for SMM drivers.

  @param  ImageHandle   The firmware allocated handle for the EFI image.
  @param  SystemTable   A pointer to the EFI System Table.

  @retval EFI_SUCCESS   The constructor always returns EFI_SUCCESS.

**/
EFI_STATUS
EFIAPI
SmmCoreAdvancedLoggerLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  Handle;
  EFI_STATUS  Status;

  ASSERT ((gBS != NULL) && (gSmst != NULL));

  SmmInitializeLoggerInfo ();

  //
  // Register a notification function for Advanced Logger Protocol installation.
  // This allows the SMM Core to pick up the migrated logger buffer when the DXE
  // Core reinstalls the protocol at End of DXE.
  //
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnAdvancedLoggerProtocolNotification,
                  NULL,
                  &mAdvancedLoggerProtocolEvent
                  );

  if (!EFI_ERROR (Status)) {
    Status = gBS->RegisterProtocolNotify (
                    &gAdvancedLoggerProtocolGuid,
                    mAdvancedLoggerProtocolEvent,
                    &mAdvancedLoggerProtocolReg
                    );

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: Register Protocol Notify failed - %r\n", __func__, Status));
      gBS->CloseEvent (mAdvancedLoggerProtocolEvent);
      mAdvancedLoggerProtocolEvent = NULL;
    }
  } else {
    DEBUG ((DEBUG_ERROR, "%a: Create Event for Protocol Notify failed - %r\n", __func__, Status));
  }

  //
  // SMM_CORE does not allow SmmInstallProtocolInterface until after the SmmInitializeMemoryServices
  // call from MemoryAllocationLib.  Leave MemoryAllocationLib in the INF to ensure that
  // MemoryAllocationLib constructor runs before Advanced Logger.
  //

  Handle = NULL;
  Status = gSmst->SmmInstallProtocolInterface (
                    &Handle,
                    &gAdvancedLoggerProtocolGuid,
                    EFI_NATIVE_INTERFACE,
                    &mAdvLoggerProtocol.AdvLoggerProtocol
                    );

  DEBUG ((DEBUG_INFO, "%a: LoggerInfo=%p, Code=%r\n", __func__, mLoggerInfo, Status));
  ASSERT_EFI_ERROR (Status);

  return EFI_SUCCESS;
}
