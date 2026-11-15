// ============================================================
//  WimSmith - extractwim.h
//  "Extract" / "Extract each" of the right-click menu of .wim / .esd /
//  .swm (WimSmith.exe --extract <one|each> <file>...): without the main
//  window and without administrator rights (in this process, no ACLs).
//    one : a new folder named after the first file, next to it; every
//          file is extracted into it
//    each: a new folder per file, named after it, next to it
//  A file with one image goes straight into its folder; with several, each
//  image into a subfolder "<index> <name>". Any part of a split WIM stands
//  for the whole set. Explorer starts one process per file: they are
//  gathered into one window (gather.h).
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    bool ExtractMode(const wstring& s);    // one | each
    // true: an open window took the files (this process ends)
    bool ExtractForward(const std::vector<wstring>& files, const wstring& mode);
    int RunExtract(const std::vector<wstring>& files, const wstring& mode);
}
