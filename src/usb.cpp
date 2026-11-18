// ============================================================
//  WimSmith - usb.cpp
//  How a drive is connected over USB (see usb.h).
// ============================================================
#include "usb.h"
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <usbioctl.h>
#include <cwctype>

namespace wimsmith
{
    namespace usb
    {
        // GUID_DEVINTERFACE_DISK and GUID_DEVINTERFACE_USB_HUB (written out: no
        // initguid.h, which would define every GUID of the headers above here)
        static const GUID DiskInterface = { 0x53f56307, 0xb6bf, 0x11d0, { 0x94, 0xf2, 0x00, 0xa0, 0xc9, 0x1e, 0xfb, 0x8b } };
        static const GUID HubInterface = { 0xf18a0e88, 0xc30c, 0x11d0, { 0x88, 0x15, 0x00, 0xa0, 0xc9, 0x06, 0xbe, 0xd8 } };

        struct Handle
        {
            HANDLE h = INVALID_HANDLE_VALUE;
            explicit Handle(HANDLE x) : h(x) {}
            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;
            ~Handle() { if (ok()) CloseHandle(h); }
            bool ok() const { return h != INVALID_HANDLE_VALUE && h != nullptr; }
        };

        static HANDLE Open(const wstring& path, DWORD access)
        {
            return CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        }

        static bool DeviceNumber(const wstring& path, STORAGE_DEVICE_NUMBER& n)
        {
            Handle h(Open(path, 0));
            DWORD got = 0;
            return h.ok() && DeviceIoControl(h.h, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &n, sizeof n, &got, nullptr);
        }

        static wstring InstanceId(DEVINST d)
        {
            wchar_t buf[MAX_DEVICE_ID_LEN + 1] = {};
            return CM_Get_Device_IDW(d, buf, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS ? wstring(buf) : wstring();
        }

        static wstring Upper(wstring s)
        {
            for (wchar_t& c : s) c = (wchar_t)towupper(c);
            return s;
        }

        // the path of the hub interface of a hub device (root hubs have one too)
        static wstring HubPath(DEVINST hub)
        {
            wstring id = InstanceId(hub);
            ULONG len = 0;
            if (id.empty() || CM_Get_Device_Interface_List_SizeW(&len, (LPGUID)&HubInterface, (DEVINSTID_W)id.c_str(),
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || len < 2) return wstring();
            std::vector<wchar_t> list(len, 0);
            if (CM_Get_Device_Interface_ListW((LPGUID)&HubInterface, (DEVINSTID_W)id.c_str(), list.data(), len,
                CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS) return wstring();
            return wstring(list.data());   // the first one
        }

        static HANDLE OpenHub(const wstring& path)
        {
            HANDLE h = Open(path, GENERIC_WRITE);   // as USBView; without rights the query below still works
            return h != INVALID_HANDLE_VALUE ? h : Open(path, 0);
        }

        // port: 1-based. The protocols of the port and what the device on it runs at.
        static bool AskPort(HANDLE hub, ULONG port, int& speed, bool& portUsb3, bool& deviceUsb3)
        {
            DWORD got = 0;
            bool any = false;
            std::vector<BYTE> buf(sizeof(USB_NODE_CONNECTION_INFORMATION_EX) + 32 * sizeof(USB_PIPE_INFO), 0);
            USB_NODE_CONNECTION_INFORMATION_EX* ex = (USB_NODE_CONNECTION_INFORMATION_EX*)buf.data();
            ex->ConnectionIndex = port;
            if (DeviceIoControl(hub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX, buf.data(), (DWORD)buf.size(),
                buf.data(), (DWORD)buf.size(), &got, nullptr) && ex->ConnectionStatus == DeviceConnected)
            {
                speed = ex->Speed <= UsbSuperSpeed ? (int)ex->Speed : SpeedSuper;
                any = true;
            }
            // Windows 8+: the protocols of the port, and SuperSpeed (the field above
            // reports a SuperSpeed device as high speed on some versions)
            USB_NODE_CONNECTION_INFORMATION_EX_V2 v2 = {};
            v2.ConnectionIndex = port;
            v2.Length = sizeof v2;
            v2.SupportedUsbProtocols.Usb110 = 1;
            v2.SupportedUsbProtocols.Usb200 = 1;
            v2.SupportedUsbProtocols.Usb300 = 1;
            if (DeviceIoControl(hub, IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2, &v2, sizeof v2, &v2, sizeof v2, &got, nullptr))
            {
                portUsb3 = v2.SupportedUsbProtocols.Usb300 != 0;
                deviceUsb3 = v2.Flags.DeviceIsSuperSpeedCapableOrHigher != 0 || v2.Flags.DeviceIsOperatingAtSuperSpeedOrHigher != 0;
                if (v2.Flags.DeviceIsOperatingAtSuperSpeedOrHigher) speed = SpeedSuper;
                any = true;
            }
            return any;
        }

        bool OfDrive(wchar_t letter, DriveInfo& info)
        {
            info = DriveInfo();
            STORAGE_DEVICE_NUMBER vol = {};
            if (!DeviceNumber(wstring(L"\\\\.\\") + letter + L":", vol) || vol.DeviceType != FILE_DEVICE_DISK) return false;

            // the disk device of that number
            DEVINST disk = 0;
            HDEVINFO set = SetupDiGetClassDevsW(&DiskInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (set == INVALID_HANDLE_VALUE) return false;
            for (DWORD i = 0; !disk; i++)
            {
                SP_DEVICE_INTERFACE_DATA ifd = { sizeof ifd };
                if (!SetupDiEnumDeviceInterfaces(set, nullptr, &DiskInterface, i, &ifd)) break;
                DWORD size = 0;
                SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &size, nullptr);
                if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
                std::vector<BYTE> buf(size, 0);
                SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf.data();
                detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
                SP_DEVINFO_DATA dd = { sizeof dd };
                if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, size, nullptr, &dd)) continue;
                STORAGE_DEVICE_NUMBER n = {};
                if (!DeviceNumber(detail->DevicePath, n) || n.DeviceType != vol.DeviceType || n.DeviceNumber != vol.DeviceNumber) continue;
                disk = dd.DevInst;
                wchar_t name[256] = {};
                if (SetupDiGetDeviceRegistryPropertyW(set, &dd, SPDRP_FRIENDLYNAME, nullptr, (PBYTE)name, sizeof name - sizeof(wchar_t), nullptr)
                    || SetupDiGetDeviceRegistryPropertyW(set, &dd, SPDRP_DEVICEDESC, nullptr, (PBYTE)name, sizeof name - sizeof(wchar_t), nullptr))
                    info.name = name;
            }
            SetupDiDestroyDeviceInfoList(set);
            if (!disk) return false;

            // up the tree to the USB device (USBSTOR / UASP disk -> ... -> USB\VID_..)
            DEVINST device = 0, cur = disk;
            for (int level = 0; level < 10 && !device; level++)
            {
                DEVINST parent = 0;
                if (CM_Get_Parent(&parent, cur, 0) != CR_SUCCESS) break;
                wstring id = Upper(InstanceId(parent));
                if (id.rfind(L"USB\\", 0) == 0 && id.find(L"&MI_") == wstring::npos && id.rfind(L"USB\\ROOT_HUB", 0) != 0) device = parent;
                cur = parent;
            }
            if (!device) return false;

            // its port on the hub above it
            ULONG port = 0, size = sizeof port, type = 0;
            DEVINST hub = 0;
            if (CM_Get_DevNode_Registry_PropertyW(device, CM_DRP_ADDRESS, &type, &port, &size, 0) != CR_SUCCESS || port == 0
                || CM_Get_Parent(&hub, device, 0) != CR_SUCCESS) return true;   // on USB, speed unknown
            wstring path = HubPath(hub);
            if (path.empty()) return true;
            Handle h(OpenHub(path));
            if (h.ok()) AskPort(h.h, port, info.speed, info.portUsb3, info.deviceUsb3);
            return true;
        }

        bool SystemHasUsb3Port()
        {
            HDEVINFO set = SetupDiGetClassDevsW(&HubInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
            if (set == INVALID_HANDLE_VALUE) return false;
            bool found = false;
            for (DWORD i = 0; !found; i++)
            {
                SP_DEVICE_INTERFACE_DATA ifd = { sizeof ifd };
                if (!SetupDiEnumDeviceInterfaces(set, nullptr, &HubInterface, i, &ifd)) break;
                DWORD size = 0;
                SetupDiGetDeviceInterfaceDetailW(set, &ifd, nullptr, 0, &size, nullptr);
                if (size < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)) continue;
                std::vector<BYTE> buf(size, 0);
                SP_DEVICE_INTERFACE_DETAIL_DATA_W* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)buf.data();
                detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
                if (!SetupDiGetDeviceInterfaceDetailW(set, &ifd, detail, size, nullptr, nullptr)) continue;
                Handle h(OpenHub(detail->DevicePath));
                if (!h.ok()) continue;
                USB_NODE_INFORMATION node = {};
                DWORD got = 0;
                if (!DeviceIoControl(h.h, IOCTL_USB_GET_NODE_INFORMATION, &node, sizeof node, &node, sizeof node, &got, nullptr)) continue;
                int ports = node.u.HubInformation.HubDescriptor.bNumberOfPorts;
                for (int p = 1; p <= ports && !found; p++)
                {
                    int speed = SpeedUnknown;
                    bool portUsb3 = false, deviceUsb3 = false;
                    if (AskPort(h.h, (ULONG)p, speed, portUsb3, deviceUsb3) && portUsb3) found = true;
                }
            }
            SetupDiDestroyDeviceInfoList(set);
            return found;
        }
    }
}
