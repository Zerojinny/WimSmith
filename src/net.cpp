// ============================================================
//  WimSmith - net.cpp
//  The network link a file on the network is read through (see net.h).
// ============================================================
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include "net.h"
#include <winnetwk.h>
#include <cstdio>
#include <cwctype>

namespace wimsmith
{
    namespace net
    {
        // the server of a UNC path: \\server\share\..., \\?\UNC\server\share\...
        static wstring UncServer(const wstring& path)
        {
            wstring p = path;
            if (p.size() > 8 && _wcsnicmp(p.c_str(), L"\\\\?\\UNC\\", 8) == 0) p = L"\\\\" + p.substr(8);
            if (p.size() < 3 || p[0] != L'\\' || p[1] != L'\\' || p[2] == L'?' || p[2] == L'.') return wstring();
            size_t end = p.find(L'\\', 2);
            return p.substr(2, end == wstring::npos ? wstring::npos : end - 2);
        }

        // the server of a path: a UNC path, or a drive letter that is a network drive
        static wstring ServerOf(const wstring& path)
        {
            wstring server = UncServer(path);
            if (!server.empty()) return server;
            if (path.size() < 2 || path[1] != L':' || !iswalpha(path[0])) return wstring();
            wstring root = path.substr(0, 2) + L"\\";
            if (GetDriveTypeW(root.c_str()) != DRIVE_REMOTE) return wstring();
            wchar_t remote[1024] = {};
            DWORD len = 1024;
            if (WNetGetConnectionW(path.substr(0, 2).c_str(), remote, &len) != NO_ERROR) return wstring();
            return UncServer(remote);
        }

        typedef DWORD(WINAPI* BestInterfaceFn)(struct sockaddr*, PDWORD);
        typedef DWORD(WINAPI* IfEntry2Fn)(PMIB_IF_ROW2);

        static HMODULE IpHelper()
        {
            static HMODULE m = LoadLibraryExW(L"iphlpapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            return m;
        }

        bool OfPath(const wstring& path, PathLink& link)
        {
            link = PathLink();
            link.server = ServerOf(path);
            if (link.server.empty()) return false;
            HMODULE ip = IpHelper();
            BestInterfaceFn bestInterface = ip ? (BestInterfaceFn)GetProcAddress(ip, "GetBestInterfaceEx") : nullptr;
            IfEntry2Fn ifEntry2 = ip ? (IfEntry2Fn)GetProcAddress(ip, "GetIfEntry2") : nullptr;
            if (!bestInterface || !ifEntry2) return false;

            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;
            bool found = false;
            ADDRINFOW hints = {};
            hints.ai_family = AF_UNSPEC;
            hints.ai_socktype = SOCK_STREAM;
            ADDRINFOW* list = nullptr;
            if (GetAddrInfoW(link.server.c_str(), nullptr, &hints, &list) == 0)
            {
                for (ADDRINFOW* a = list; a && !found; a = a->ai_next)
                {
                    DWORD index = 0;
                    if (bestInterface(a->ai_addr, &index) != NO_ERROR) continue;
                    MIB_IF_ROW2 row = {};
                    row.InterfaceIndex = index;
                    if (ifEntry2(&row) != NO_ERROR || row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                    // the speed the file comes in at; ~0 = unknown
                    unsigned long long speed = row.ReceiveLinkSpeed;
                    if (speed == 0 || speed == ~0ULL) continue;
                    link.speed = speed;
                    // the connection's name as Windows shows it ("Ethernet 5"); the
                    // adapter's description only when it has none
                    link.adapter = row.Alias[0] ? row.Alias : row.Description;
                    found = true;
                }
                FreeAddrInfoW(list);
            }
            WSACleanup();
            return found;
        }

        wstring SpeedText(unsigned long long bps)
        {
            wchar_t buf[64];
            double v = bps >= 1000000000ULL ? bps / 1e9 : bps / 1e6;
            const wchar_t* unit = bps >= 1000000000ULL ? L"Gbit/s" : L"Mbit/s";
            if (v == (double)(long long)v) swprintf_s(buf, L"%lld %s", (long long)v, unit);
            else swprintf_s(buf, L"%.1f %s", v, unit);
            return buf;
        }
    }
}
