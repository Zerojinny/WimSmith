// ============================================================
//  WimSmith - assoc.h
//  .wim files opened with WimSmith (settings dialog): a ProgID
//  "WimSmith.wim" for the current user only (HKCU\Software\Classes, no
//  administrator rights), with "<exe>" "%1" as its open command, set as
//  the default of .wim and listed under "Open with". When the user has
//  chosen another program for .wim in Windows (UserChoice, which a
//  program cannot change), Windows keeps that one: Default() tells.
//  WimSmith.exe <file> opens the file in the tab of "open_tab"
//  (App::OpenFile).
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    namespace assoc
    {
        bool Registered();                    // the ProgID opens this exe
        bool Default();                       // Windows opens .wim with this exe
        wstring Register(const wstring& typeName);   // "" = done, else the error
        void Unregister();                    // the previous default of .wim comes back
    }
}
