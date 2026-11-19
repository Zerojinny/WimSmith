// ============================================================
//  WimSmith - net.h
//  The network link a file on the network is read through (net.cpp):
//  the server of a UNC path (\\server\share\...) or of a network drive
//  (WNetGetConnection), its address (GetAddrInfoW), the interface Windows
//  takes to reach it (GetBestInterfaceEx) and that interface's receive
//  link speed (GetIfEntry2). No administrator rights; iphlpapi.dll is
//  loaded at run time (a WinPE without networking may lack it).
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    namespace net
    {
        struct PathLink
        {
            wstring server;                 // e.g. "nas" or "192.168.0.10"
            unsigned long long speed = 0;   // the receive link speed in bit/s (0 = unknown)
            wstring adapter;                // the connection's name, e.g. "Ethernet 5" (else the adapter's description)
        };

        // a path on the network and the link it is read through; false: a
        // local path, or the server / interface / speed could not be found
        bool OfPath(const wstring& path, PathLink& link);

        // "100 Mbit/s", "866.7 Mbit/s", "1 Gbit/s", "2.5 Gbit/s"
        wstring SpeedText(unsigned long long bitsPerSecond);
    }
}
