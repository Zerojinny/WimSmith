// ============================================================
//  WimSmith - assoc.h
//  .wim / .esd / .swm files opened with WimSmith (settings dialog), each
//  chosen on its own: a ProgID "WimSmith<.ext>" for the current user
//  only (HKCU\Software\Classes, no administrator rights), with
//  "<exe>" "%1" as its open command, set as the default of the extension
//  and listed under "Open with". When the user has chosen another
//  program for the extension in Windows (UserChoice, which a program
//  cannot change), Windows keeps that one: Default() tells.
//  WimSmith.exe <file> opens the file in the tab of "open_tab"
//  (App::OpenFile).
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    namespace assoc
    {
        // the extensions that can be set: ".wim", ".esd", ".swm"
        extern const wchar_t* const Extensions[3];
        bool Registered(const wstring& ext);   // the ProgID opens this exe
        bool Default(const wstring& ext);      // Windows opens the extension with this exe
        wstring Register(const wstring& ext, const wstring& typeName);   // "" = done, else the error
        void Unregister(const wstring& ext);   // the previous default of the extension comes back
        void Changed();                        // tells Explorer (once after the changes)
    }
}
