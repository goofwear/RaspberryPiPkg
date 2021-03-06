/** @file
 *
 *  Copyright (c), 2018, Andrei Warkentin <andrey.warkentin@gmail.com>
 *
 *  This program and the accompanying materials
 *  are licensed and made available under the terms and conditions of the BSD License
 *  which accompanies this distribution.  The full text of the license may be found at
 *  http://opensource.org/licenses/bsd-license.php
 *
 *  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
 *
 **/

#include <Uefi.h>
#include <Library/HiiLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/DevicePathLib.h>
#include <Protocol/RaspberryPiFirmware.h>
#include <IndustryStandard/RpiFirmware.h>
#include "ConfigDxeFormSetGuid.h"

extern UINT8 ConfigDxeHiiBin[];
extern UINT8 ConfigDxeStrings[];

STATIC RASPBERRY_PI_FIRMWARE_PROTOCOL *mFwProtocol;

typedef struct {
  VENDOR_DEVICE_PATH VendorDevicePath;
  EFI_DEVICE_PATH_PROTOCOL End;
} HII_VENDOR_DEVICE_PATH;

STATIC HII_VENDOR_DEVICE_PATH mVendorDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8) (sizeof (VENDOR_DEVICE_PATH)),
        (UINT8) ((sizeof (VENDOR_DEVICE_PATH)) >> 8)
      }
    },
    CONFIGDXE_FORM_SET_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8) (END_DEVICE_PATH_LENGTH),
      (UINT8) ((END_DEVICE_PATH_LENGTH) >> 8)
    }
  }
};


STATIC EFI_STATUS
InstallHiiPages (
  VOID
  )
{
  EFI_STATUS     Status;
  EFI_HII_HANDLE HiiHandle;
  EFI_HANDLE     DriverHandle;

  DriverHandle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (&DriverHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mVendorDevicePath,
                  NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  HiiHandle = HiiAddPackages (&gConfigDxeFormSetGuid,
                              DriverHandle,
                              ConfigDxeStrings,
                              ConfigDxeHiiBin,
                              NULL);

  if (HiiHandle == NULL) {
    gBS->UninstallMultipleProtocolInterfaces (DriverHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mVendorDevicePath,
                  NULL);
    return EFI_OUT_OF_RESOURCES;
  }
  return EFI_SUCCESS;
}


STATIC EFI_STATUS
SetupVariables (
  VOID
  )
{
  UINTN Size;
  UINT32 Var32;
  EFI_STATUS Status;

  /*
   * Create the vars with default value.
   * If we don't, forms won't be able to update.
   */

  Size = sizeof (UINT32);
  Status = gRT->GetVariable(L"HypEnable",
                            &gConfigDxeFormSetGuid,
                            NULL,  &Size, &Var32);
  if (EFI_ERROR (Status)) {
    PcdSet32 (PcdHypEnable, PcdGet32 (PcdHypEnable));
  }

  Size = sizeof (UINT32);
  Status = gRT->GetVariable(L"HypLogMask",
                            &gConfigDxeFormSetGuid,
                            NULL,  &Size, &Var32);
  if (EFI_ERROR (Status)) {
    PcdSet32 (PcdHypLogMask, PcdGet32 (PcdHypLogMask));
  }

  Size = sizeof (UINT32);
  Status = gRT->GetVariable(L"HypWindowsDebugHook",
                            &gConfigDxeFormSetGuid,
                            NULL,  &Size, &Var32);
  if (EFI_ERROR (Status)) {
    PcdSet32 (PcdHypWindowsDebugHook,
              PcdGet32 (PcdHypWindowsDebugHook));
  }

  Size = sizeof (UINT32);
  Status = gRT->GetVariable(L"HypWin2000Mask",
                            &gConfigDxeFormSetGuid,
                            NULL,  &Size, &Var32);
  if (EFI_ERROR (Status)) {
    PcdSet32 (PcdHypWin2000Mask, PcdGet32 (PcdHypWin2000Mask));
  }

  Size = sizeof (UINT32);
  Status = gRT->GetVariable(L"CpuClock",
                            &gConfigDxeFormSetGuid,
                            NULL,  &Size, &Var32);
  if (EFI_ERROR (Status)) {
    /*
     * Create the var. If we don't, forms won't
     * be able to update.
     */
    PcdSet32 (PcdCpuClock, PcdGet32 (PcdCpuClock));
  }

  return EFI_SUCCESS;
}
  

STATIC VOID
ApplyVariables (
  VOID
  )
{
  EFI_STATUS Status;
  UINT32 CpuClock = PcdGet32 (PcdCpuClock);
  UINT32 Rate = 0;

  if (CpuClock != 0) {
    if (CpuClock == 2) {
      /*
       * Maximum: 1.2GHz on RPi 3, 1.4GHz on RPi 3B+, unless
       * overridden with arm_freq=xxx in config.txt.
       */
      Status = mFwProtocol->GetMaxClockRate(RPI_FW_CLOCK_RATE_ARM, &Rate);
      if (Status != EFI_SUCCESS) {
        DEBUG((EFI_D_ERROR, "Couldn't get the max CPU speed, leaving as is: %r\n",
               Status));
      }
    } else {
      Rate = 600 * 1000000;
    }
  }

  if (Rate != 0) {
    DEBUG((EFI_D_INFO, "Setting CPU speed to %uHz\n", Rate));
    Status = mFwProtocol->SetClockRate(RPI_FW_CLOCK_RATE_ARM, Rate);
    if (Status != EFI_SUCCESS) {
      DEBUG((EFI_D_ERROR, "Couldn't set the CPU speed: %r\n",
             Status));
    }
  }

  Status = mFwProtocol->GetClockRate(RPI_FW_CLOCK_RATE_ARM, &Rate);
  if (Status != EFI_SUCCESS) {
    DEBUG((EFI_D_ERROR, "Couldn't get the CPU speed: %r\n",
           Status));
  } else {
    DEBUG((EFI_D_INFO, "Current CPU speed is %uHz\n", Rate));
  }
}


EFI_STATUS
EFIAPI
ConfigInitialize(
  IN EFI_HANDLE ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable
  )
{
  EFI_STATUS Status;

  Status = gBS->LocateProtocol (&gRaspberryPiFirmwareProtocolGuid,
                                NULL, (VOID **)&mFwProtocol);
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = SetupVariables();
  if (Status != EFI_SUCCESS) {
    DEBUG((EFI_D_ERROR, "Couldn't not setup NV vars: %r\n",
           Status));
  }

  /*
   * FIXME: not the right place either.
   */
  ApplyVariables();

  Status = InstallHiiPages();
  if (Status != EFI_SUCCESS) {
    DEBUG((EFI_D_ERROR, "Couldn't install ConfigDxe configuration pages: %r\n",
           Status));
  }

  return EFI_SUCCESS;
}

