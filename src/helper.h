// ============================================================
//  WimSmith - helper.h
//  The helper: WimSmith.exe --wim-helper <work folder> (helper.cpp,
//  disk.cpp). The same contract as the C# helper:
//    <work>\job.ini      the job ("key = value", UTF-8)
//    <work>\status.txt   progress "P <percent> <phase> ...", log lines,
//                        "LETTER <x>", at the end "EXIT <code> <text>"
//    <work>\cancel       stops the job at the next progress message
//  The checks (…Problem) return "" or the key of the reason in
//  lang\*.ini; the GUI makes them too.
// ============================================================
#pragma once
#include "common.h"

namespace wimsmith
{
    const long long MiB = 1048576;

    int RunHelper(const std::vector<wstring>& args);

    // ---- status.txt (helper.cpp) ----
    extern wstring statusFile, cancelFile, errorFile;
    void Status(const wstring& line);
    void Progress(int pct, const wstring& text, bool force);
    int Finish(int code, const wstring& text);

    // ---- tools of Windows (helper.cpp) ----
    wchar_t FreeLetter();
    bool WaitReady(wchar_t letter, const wstring& fileSystem);

    // ---- job.ini ----
    wstring Get(const Job& job, const wstring& key);
    bool TryGet(const Job& job, const wstring& key, wstring& value);

    // ---- checks (helper.cpp) ----
    bool IsWindowsDrive(wchar_t letter);
    bool IsLocale(const wstring& s);
    wstring CleanLabel(const wstring& label);
    wstring CaptureProblem(const wstring& source, const wstring& wimFile, bool snapshot);
    wstring SplitProblem(const wstring& sourceWim, const wstring& swmFile);
    wstring JoinProblem(const std::vector<wstring>& parts, const wstring& wimFile);
    wstring ExportProblem(const wstring& sourceWim, const wstring& wimFile);
    wstring TargetProblem(wchar_t letter, const wstring& wimFile, const wstring& buildRoot);
    wstring FolderTargetProblem(const wstring& folder, const wstring& wimFile, const wstring& buildRoot);
    std::vector<wstring> SwmParts(const wstring& anyPart);

    // ---- mode = disk (disk.cpp) ----
    int Disk(const Job& job);
}
