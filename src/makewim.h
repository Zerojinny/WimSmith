// ============================================================
//  WimSmith - makewim.h
//  The right-click menu of folders "WimSmith >" (assoc::RegisterFolderMenu):
//  WimSmith.exe --make-wim <mode> <folder>..., mode one (make one WIM
//  file, an image per folder), each (a WIM file per folder) or open
//  (only the window). Without the main window, a window with the folders, the WIM file
//  (or a folder for one WIM file per folder), compression and options;
//  [Start] runs the elevated helper (mode capture, source_count = n,
//  s<k> / w<k> / n<k>) and shows its progress; one / each start by
//  themselves, without asking, once no more folders come in.
//  Explorer starts one process per selected folder: the first one opens
//  the window (named mutex + a message-only window, one per mode), the
//  others give it their folder (WM_COPYDATA) and end.
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    // true: a "Make a WIM" window that is open took the folders (this
    // process ends); false: this process opens the window (RunMakeWim)
    // mode: L"one", L"each" or L"open" (MakeWimMode)
    bool MakeWimForward(const std::vector<wstring>& folders, const wstring& mode);
    int RunMakeWim(const std::vector<wstring>& folders, const wstring& mode);
    bool MakeWimMode(const wstring& s);   // one | each | open
}
