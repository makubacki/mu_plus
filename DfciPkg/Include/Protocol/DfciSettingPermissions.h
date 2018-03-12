/** @file
  Defines the System Settings Permissions Protocol.

  This protocol allows modules to get the Permission for a given setting. 

Copyright (c) 2015, Microsoft Corporation. All rights reserved.<BR>

**/
#ifndef __DFCI_SETTING_PERMISSIONS_H__
#define __DFCI_SETTING_PERMISSIONS_H__  


/**
Define the DFCI_SETTING_PERMISSIONS_PROTOCOL related structures
**/
typedef struct _DFCI_SETTING_PERMISSIONS_PROTOCOL DFCI_SETTING_PERMISSIONS_PROTOCOL;

/*
Get the Permission Mask for a given setting

@param[in]  This:           Permission Protocol
@param[in]  Id:             Setting ID to Get Permission for
@param[OUT] PermissionMask  Permission Mask for the requested setting.     

@retval EFI_SUCCESS - PermissionMask Parameter value is set to the value in System Permissions
@retval Error -  Error and PermissionMask Parameter is not updated. 

*/
typedef
EFI_STATUS
(EFIAPI *DFCI_SETTING_PERMISSIONS_GET_PERMISSION) (
  IN  CONST DFCI_SETTING_PERMISSIONS_PROTOCOL *This,
  IN  DFCI_SETTING_ID_ENUM                     Id,
  OUT DFCI_PERMISSION_MASK                    *PermissionMask
);

/*
Clear All the System Permissions

@param[in]  This:           Permission Protocol
@param[in]  AuthToken       Token used for authority.

@retval EFI_SUCCESS - System permissions cleared
@retval Error -  Error

*/
typedef
EFI_STATUS
(EFIAPI *DFCI_SETTING_PERMISSIONS_RESET_PERMISSIONS) (
  IN  CONST DFCI_SETTING_PERMISSIONS_PROTOCOL *This,
  IN  CONST DFCI_AUTH_TOKEN                   *AuthToken
  );

//
// Protocol def
//
#pragma pack (push, 1)
struct _DFCI_SETTING_PERMISSIONS_PROTOCOL {
  DFCI_SETTING_PERMISSIONS_GET_PERMISSION     GetPermission;
  DFCI_SETTING_PERMISSIONS_RESET_PERMISSIONS  ResetPermissions;
};
#pragma pack (pop)


extern EFI_GUID     gDfciSettingPermissionsProtocolGuid;

#endif // __DFCI_SETTING_PERMISSIONS_H__