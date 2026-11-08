// ============================================================
//  7z_wimlib - unzip.h
//  Reads a zip file without another program (the wimlib download:
//  wimlib-<ver>-windows-x86_64-bin.zip). Stored and Deflate entries,
//  checked with their CRC-32; no ZIP64, no encryption.
// ============================================================
#pragma once
#include "common.h"
#include <functional>

namespace sevenwim
{
    namespace unzip
    {
        // the files of the top folder of zip whose name want() accepts, into
        // outDir (which must exist); returns how many; throws HelperError
        int ExtractTop(const wstring& zip, const wstring& outDir, const std::function<bool(const wstring& name)>& want);
    }
}
