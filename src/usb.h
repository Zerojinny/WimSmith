// ============================================================
//  WimSmith - usb.h
//  How a drive is connected over USB (usb.cpp), without administrator
//  rights: the drive letter's disk is found among the disk interfaces
//  (IOCTL_STORAGE_GET_DEVICE_NUMBER), its USB device up the device tree
//  (CM_Get_Parent, an instance id "USB\..." that is not an interface
//  "&MI_"), and the hub port it sits on (CM_DRP_ADDRESS) is asked with
//  IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX(_V2): the speed it runs
//  at and whether the port supports USB 3.0.
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    namespace usb
    {
        // USB_DEVICE_SPEED, and SuperSpeed or faster
        enum { SpeedUnknown = -1, SpeedLow = 0, SpeedFull = 1, SpeedHigh = 2, SpeedSuper = 3 };

        struct DriveInfo
        {
            int speed = SpeedUnknown;   // what the device runs at now
            bool portUsb3 = false;      // its port supports USB 3.0 or faster
            bool deviceUsb3 = false;    // the device says it can do SuperSpeed (Windows 8+)
            wstring name;               // the disk's name, e.g. "SanDisk Ultra USB Device"
        };

        // the USB connection of the device that holds a drive letter;
        // false: not on a USB device (or not found)
        bool OfDrive(wchar_t letter, DriveInfo& info);

        // some USB port of this PC (on a root hub or any hub) supports USB 3.0 or faster
        bool SystemHasUsb3Port();
    }
}
