// ============================================================
//  WimSmith - gather.h
//  The items of one click in Explorer's right-click menu, gathered into
//  one process. Explorer starts the command once per selected file or
//  folder: the first process takes a named mutex ("Local\WimSmith.<name>")
//  and opens its window; the others send their path (WM_COPYDATA) to its
//  message-only window ("WimSmith.<name>.Receiver") and end. Used by
//  makewim.cpp (folders) and extractwim.cpp (WIM files).
// ============================================================
#pragma once
#include "common.h"
#include <functional>

namespace wimsmith
{
    namespace gather
    {
        // true: an open window of this name took the items (this process
        // ends); false: this process is the first one (Receive next)
        bool Forward(const wstring& name, const std::vector<wstring>& items);
        // the items of the other processes come to f (on the GUI thread);
        // f returns false when it takes none now (the sender tries again for ~10 s)
        void Receive(const wstring& name, std::function<bool(const std::vector<wstring>&)> f);
        // no more items: a later click opens a new window
        void Stop();
    }
}
