// ============================================================
//  7z_wimlib - helper.cpp
//  The elevated helper: 7z_wimlib.exe --wim-helper <work folder>
//  (contract in helper.h). It reads job.ini and
//    capture: saves a volume or folder (threads = compressor threads,
//             0 = automatic) as a new WIM file (written to <file>.tmp
//             first) or as a new image of an existing one; with
//             snapshot = 1 it reads from a VSS snapshot;
//    export : copies one image (or all) of a WIM / ESD file into a new
//             WIM file (<file>.tmp first) or into an existing one;
//    split  : writes a WIM as parts (<name>.swm, <name>2.swm, ...) of at
//             most part_mb MiB;
//    join   : joins all parts of a split WIM into one WIM file;
//    verify : checks the integrity table and all the data of a WIM;
//    apply  : formats a volume (NTFS) and applies one image to it, or
//             (folder = ...) applies it into an empty folder; with
//             boot = UEFI|BIOS|ALL bcdboot then writes the boot files;
//    disk   : see disk.cpp.
//  wimlib's own messages go to wimlib_errors.txt.
// ============================================================
#include "helper.h"
#include "disk.h"
#include "storage.h"
#include "wim.h"
#include <algorithm>
#include <cstring>

namespace dvt
{
    using namespace wl;

    wstring statusFile, cancelFile, errorFile;


    // ---------- job.ini ----------

    static Job ReadJob(const wstring& file)
    {
        Job job;
        for (const wstring& line : SplitLines(ReadTextFile(file)))
        {
            size_t eq = line.find(L'=');
            if (eq != wstring::npos && eq > 0) job[Lower(Trim(line.substr(0, eq)))] = Trim(line.substr(eq + 1));
        }
        return job;
    }

    bool TryGet(const Job& job, const wstring& key, wstring& value)
    {
        Job::const_iterator it = job.find(Lower(key));
        if (it == job.end()) return false;
        value = it->second;
        return true;
    }

    wstring Get(const Job& job, const wstring& key)
    {
        wstring v;
        return TryGet(job, key, v) ? v : wstring();
    }

    // ---------- status.txt ----------

    static int lastPct = -2;
    static ULONGLONG lastLine, lastCancelCheck;
    static bool cancelled;

    void Status(const wstring& line)
    {
        AppendBytes(statusFile, ToUtf8(line + L"\r\n"));
    }

    // "P <percent> <phase> ..." at most about once a second, or when the percent changes
    void Progress(int pct, const wstring& text, bool force)
    {
        ULONGLONG now = GetTickCount64();
        if (!force && pct == lastPct && now - lastLine < 1000) return;
        lastPct = pct;
        lastLine = now;
        Status(L"P " + Num(pct) + L" " + text);
    }

    int Finish(int code, const wstring& text)
    {
        Status(L"EXIT " + Num(code) + L" " + text);
        return code;
    }

    // a wimlib error: its messages from wimlib_errors.txt, then EXIT
    static int Failed(int rc, const wstring& step)
    {
        if (FileExists(errorFile))
        {
            try
            {
                std::vector<wstring> lines = SplitLines(ReadTextFile(errorFile));
                while (!lines.empty() && lines.back().empty()) lines.pop_back();
                for (size_t n = lines.size() > 20 ? lines.size() - 20 : 0; n < lines.size(); n++)
                    if (!Trim(lines[n]).empty()) Status(L"  " + TrimEnd(lines[n]));
            }
            catch (const HelperError&) {}
        }
        if (rc == ErrAborted) return Finish(ErrAborted, L"Cancelled: " + step);
        return Finish(rc, step + L": " + ErrorText(rc));
    }

    // ---------- progress of wimlib ----------

    static bool splitting;
    static long long splitDoneBefore, splitTotal;
    static int splitPart, splitParts;

    template <class T> static T At(const void* p, size_t off)
    {
        T v;
        memcpy(&v, (const unsigned char*)p + off, sizeof v);
        return v;
    }

    static void Bytes(const wchar_t* phase, long long done, long long total)
    {
        int pct = total > 0 ? (int)std::min<long long>(100, done * 100 / total) : -1;
        Progress(pct, wstring(phase) + L" " + Num(done) + L" " + Num(total), false);
    }

    static void SplitProgress(long long done)
    {
        int pct = splitTotal > 0 ? (int)std::min<long long>(100, done * 100 / splitTotal) : -1;
        Progress(pct, L"split " + Num(done) + L" " + Num(splitTotal) + L" " + Num(splitPart) + L" " + Num(splitParts), false);
    }

    // wimlib progress -> "P ..." lines; also the cancel check
    static int __cdecl OnProgress(int msg, void* info, void*)
    {
        try
        {
            switch (msg)
            {
            case MsgScanDentry:
                // scan: num_dirs_scanned (32), num_nondirs_scanned (40), num_bytes_scanned (48)
                Progress(-1, L"scan " + Num(At<int64_t>(info, 32)) + L" " + Num(At<int64_t>(info, 40)) + L" " + Num(At<int64_t>(info, 48)), false);
                break;
            case MsgWriteStreams:
                // write_streams: total_bytes (0), total_streams (8), completed_bytes (16)
                if (splitting) SplitProgress(splitDoneBefore + At<int64_t>(info, 16));   // of the current part: add the parts before
                else Bytes(L"write", At<int64_t>(info, 16), At<int64_t>(info, 0));
                break;
            case MsgSplitBeginPart:
            case MsgSplitEndPart:
                // split: total_bytes (0), completed_bytes (8), cur_part_number (16), total_parts (20)
                splitTotal = At<int64_t>(info, 0);
                splitDoneBefore = At<int64_t>(info, 8);
                splitPart = At<int32_t>(info, 16);
                splitParts = At<int32_t>(info, 20);
                SplitProgress(splitDoneBefore);
                break;
            case MsgExtractStreams:
                // extract: total_bytes (40), completed_bytes (48)
                Bytes(L"apply", At<int64_t>(info, 48), At<int64_t>(info, 40));
                break;
            case MsgVerifyIntegrity:
                // integrity: total_bytes (0), completed_bytes (8)
                Bytes(L"integrity", At<int64_t>(info, 8), At<int64_t>(info, 0));
                break;
            case MsgBeginVerifyImage:
                // verify_image: total_images (8), current_image (12)
                Progress(-1, L"vimage " + Num(At<int32_t>(info, 12)) + L" " + Num(At<int32_t>(info, 8)), true);
                break;
            case MsgVerifyStreams:
                // verify_streams: total_bytes (16), completed_bytes (32)
                Bytes(L"verify", At<int64_t>(info, 32), At<int64_t>(info, 16));
                break;
            case MsgExtractFileStructure: Progress(-1, L"structure", false); break;
            case MsgExtractMetadata:
            case MsgWriteMetadataBegin: Progress(-1, L"metadata", false); break;
            }
            ULONGLONG now = GetTickCount64();
            if (!cancelled && now - lastCancelCheck > 300)
            {
                lastCancelCheck = now;
                cancelled = FileExists(cancelFile);
            }
        }
        catch (...) {}
        return cancelled ? StatusAbort : StatusContinue;
    }

    // ---------- opening a WIM ----------

    // opens a WIM file to read it. A part of a split WIM (.swm, any part)
    // is opened as its first part, the other parts are referenced; total /
    // found tell a missing part (rc = ErrPartsMissing).
    int wl::OpenRead(const wstring& file, int openFlags, ProgressFunc pf, Wim& wim, int& total, int& found)
    {
        total = 1;
        found = 1;
        wim.Reset();
        int rc = wimlib_open_wim_with_progress(file.c_str(), openFlags, &wim.p, pf, nullptr);
        if (rc != 0) { wim.p = nullptr; return rc; }
        WimInfo info;
        int part = 1;
        if (info.Read(wim.p)) { part = info.PartNumber(); total = std::max(1, info.TotalParts()); }
        if (total <= 1) return 0;
        std::vector<wstring> parts = SwmParts(file);
        found = (int)parts.size();
        if ((int)parts.size() != total) { wim.Reset(); return ErrPartsMissing; }
        if (part != 1)
        {
            wim.Reset();
            rc = wimlib_open_wim_with_progress(parts[0].c_str(), openFlags, &wim.p, pf, nullptr);
            if (rc != 0) { wim.p = nullptr; return rc; }
        }
        std::vector<const wchar_t*> rest;
        for (size_t n = 1; n < parts.size(); n++) rest.push_back(parts[n].c_str());
        rc = wimlib_reference_resource_files(wim.p, rest.data(), (unsigned)rest.size(), 0, openFlags);
        if (rc != 0) wim.Reset();
        return rc;
    }

    static wstring OpenText(int rc, const wstring& file, int total, int found)
    {
        if (rc == ErrPartsMissing)
            return file + L" belongs to " + Num(total) + L" parts, but " + Num(found) + L" were found in its folder (all parts must keep their names).";
        return ErrorText(rc);
    }

    static int ImageCount(WIMStruct* wim)
    {
        WimInfo i;
        return i.Read(wim) ? i.ImageCount() : 0;
    }

    static int BootIndex(WIMStruct* wim)
    {
        WimInfo i;
        return i.Read(wim) ? i.BootIndex() : 0;
    }

    static unsigned Threads(const Job& job)
    {
        long long t = ParseLong(Get(job, L"threads"), 0);
        return (unsigned)std::max(0LL, std::min(64LL, t));
    }

    // ---------- checks ----------

    static wstring SafeFull(const wstring& p) { return FullPath(p); }

    static bool Inside(const wstring& path, const wstring& dir)
    {
        return StartsWithI(WithSlash(path), WithSlash(dir));
    }

    bool IsWindowsDrive(wchar_t letter)
    {
        wstring r = wstring(1, UpperChar(letter)) + L":\\";
        return r == Root(Env(L"SystemDrive") + L"\\") || r == Root(WindowsDir());
    }

    // a locale for bcdboot /l, e.g. ko-kr, en-us, zh-tw, sr-latn-rs
    bool IsLocale(const wstring& s)
    {
        std::vector<wstring> parts = Split(s, L'-');
        if (parts.size() < 2 || parts.size() > 3) return false;
        for (size_t i = 0; i < parts.size(); i++)
        {
            size_t min = 2, max = i == 0 ? 3 : 4;
            if (parts[i].size() < min || parts[i].size() > max) return false;
            for (wchar_t c : parts[i]) if (c < L'a' || c > L'z') return false;
        }
        return true;
    }

    // NTFS volume label: letters, digits, space, - and _, at most 32
    wstring CleanLabel(const wstring& label)
    {
        wstring r;
        for (wchar_t c : Trim(label))
            if ((IsCharAlphaNumericW(c) || c == L' ' || c == L'-' || c == L'_') && r.size() < 32) r += c;
        return r.empty() ? wstring(L"Windows") : r;
    }

    // snapshot: the source is read from a VSS snapshot, so the running
    // Windows (its drive or its Windows folder) may be saved
    wstring CaptureProblem(const wstring& source, const wstring& wimFile, bool snapshot)
    {
        if (wimFile.empty() || !EndsWithI(wimFile, L".wim")) return L"bk.err_wim_name";
        if (source.empty() || !DirExists(source)) return L"bk.err_source_missing";
        wstring src = WithSlash(FullPath(source));
        wstring root = Root(source);
        if (!snapshot && root.size() == 3 && IsWindowsDrive(root[0]) && EqualsI(src, root)) return L"bk.err_source_system";
        if (!snapshot && StartsWithI(src, WithSlash(WindowsDir()))) return L"bk.err_source_system";
        // with a snapshot as well: the WIM file must not be inside what is saved
        if (StartsWithI(FullPath(wimFile), src)) return L"bk.err_wim_in_source";
        return wstring();
    }

    // the parts of a split WIM: <dir>\<stem>.swm, <stem>2.swm, <stem>3.swm, ...
    // (the names wimlib_split gives); a chosen later part leads to the first
    std::vector<wstring> SwmParts(const wstring& anyPart)
    {
        std::vector<wstring> parts;
        wstring full = SafeFull(anyPart);
        if (full.empty()) return parts;
        wstring dir = DirName(full), ext = FileExt(full), stem = FileStem(full);
        size_t b = stem.size();
        while (b > 0 && stem[b - 1] >= L'0' && stem[b - 1] <= L'9') b--;
        wstring bare = stem.substr(0, b);
        if (!bare.empty() && bare.size() < stem.size() && FileExists(Combine(dir, bare + ext))) stem = bare;
        wstring first = Combine(dir, stem + ext);
        if (!FileExists(first)) return parts;
        parts.push_back(first);
        for (int n = 2; n < 10000; n++)
        {
            wstring f = Combine(dir, stem + Num(n) + ext);
            if (!FileExists(f)) break;
            parts.push_back(f);
        }
        return parts;
    }

    wstring SplitProblem(const wstring& sourceWim, const wstring& swmFile)
    {
        if (sourceWim.empty() || !FileExists(sourceWim)) return L"ex.err_source_missing";
        if (swmFile.empty() || !EndsWithI(swmFile, L".swm") || SafeFull(swmFile).empty()) return L"sj.err_swm_name";
        return wstring();
    }

    wstring JoinProblem(const std::vector<wstring>& parts, const wstring& wimFile)
    {
        if (parts.empty()) return L"sj.err_no_parts";
        if (wimFile.empty() || !EndsWithI(wimFile, L".wim")) return L"sj.err_wim_name";
        wstring b = SafeFull(wimFile);
        if (b.empty()) return L"sj.err_wim_name";
        for (const wstring& p : parts)
            if (EqualsI(SafeFull(p), b)) return L"ex.err_same_file";
        return wstring();
    }

    wstring ExportProblem(const wstring& sourceWim, const wstring& wimFile)
    {
        if (sourceWim.empty() || !FileExists(sourceWim)) return L"ex.err_source_missing";
        if (wimFile.empty() || !(EndsWithI(wimFile, L".wim") || EndsWithI(wimFile, L".esd"))) return L"ex.err_wim_name";
        wstring a = SafeFull(sourceWim), b = SafeFull(wimFile);
        if (b.empty()) return L"ex.err_wim_name";
        if (EqualsI(a, b)) return L"ex.err_same_file";
        return wstring();
    }

    wstring TargetProblem(wchar_t letter, const wstring& wimFile, const wstring& buildRoot)
    {
        letter = UpperChar(letter);
        if (letter < L'A' || letter > L'Z') return L"bk.err_target_type";
        wstring r = wstring(1, letter) + L":\\";
        UINT t = DriveTypeOf(letter);
        if (t != DRIVE_FIXED && t != DRIVE_REMOVABLE) return L"bk.err_target_type";
        if (IsWindowsDrive(letter)) return L"bk.err_target_system";
        if (Root(wimFile) == r) return L"bk.err_target_wim";
        if (Root(buildRoot) == r) return L"bk.err_target_build";
        return wstring();
    }

    // a folder chosen as the restore target (no format, the image goes into it)
    wstring FolderTargetProblem(const wstring& folder, const wstring& wimFile, const wstring& buildRoot)
    {
        wstring f = FullPath(folder);
        if (f.empty()) return L"bk.err_folder_invalid";
        wstring r = Root(f);
        if (r.size() != 3) return L"bk.err_folder_invalid";
        if (EqualsI(WithSlash(f), r)) return L"bk.err_folder_root";
        UINT t = DriveTypeOf(r[0]);
        if (t != DRIVE_FIXED && t != DRIVE_REMOVABLE) return L"bk.err_folder_type";
        wstring windows = WindowsDir();
        if (!windows.empty() && (Inside(f, windows) || Inside(windows, f))) return L"bk.err_folder_system";
        if (!wimFile.empty() && Inside(SafeFull(wimFile), f)) return L"bk.err_folder_wim";
        if (!buildRoot.empty())
        {
            wstring b = SafeFull(buildRoot);
            if (!b.empty() && (Inside(f, b) || Inside(b, f))) return L"bk.err_folder_build";
        }
        if (FileExists(f)) return L"bk.err_folder_invalid";
        if (DirExists(f) && DirHasEntries(f)) return L"bk.err_folder_not_empty";
        return wstring();
    }

    // ---------- tools of Windows ----------

    // a program of Windows; its output goes to status.txt
    static int RunTool(const wstring& exe, const wstring& args)
    {
        std::string out, err;
        int rc = RunProcess(exe, args, out, err);
        for (const wstring& l : SplitLines(FromCodePage(out + err, CP_OEMCP)))
            if (!Trim(l).empty()) Status(L"  " + TrimEnd(l));
        return rc;
    }

    wchar_t FreeLetter()
    {
        for (const wchar_t* c = L"STUVWXYZRQPONMLKJIHGFED"; *c; c++)
            if (!DriveExists(*c)) return *c;
        return 0;
    }

    // the volume is mounted with the given file system (up to 20 seconds)
    bool WaitReady(wchar_t letter, const wstring& fileSystem)
    {
        for (int n = 0; n < 40; n++)
        {
            if (EqualsI(DriveFileSystem(letter), fileSystem)) return true;
            Sleep(500);
        }
        return false;
    }

    static bool WaitFolder(const wstring& dir)
    {
        for (int n = 0; n < 20; n++)
        {
            if (DirExists(dir)) return true;
            Sleep(500);
        }
        return false;
    }

    // ---------- capture ----------

    static int VerifyCore(const wstring& wimFile, wstring& step);

    static int Capture(const Job& job)
    {
        wstring source = Get(job, L"source"), wimFile = Get(job, L"wim");
        wstring name = Get(job, L"name"), desc = Get(job, L"description");
        int ctype = (int)ParseLong(Get(job, L"compression"), CompressionLzx);
        unsigned threads = Threads(job);   // 0 = wimlib's default (one per logical processor)
        bool append = Get(job, L"append") == L"1" && FileExists(wimFile);
        bool snapshot = Get(job, L"snapshot") == L"1";
        wstring why = CaptureProblem(source, wimFile, snapshot);
        if (!why.empty()) return Finish(87, L"Refused: " + why + L" (" + source + L" -> " + wimFile + L")");
        CreateDirs(DirName(FullPath(wimFile)));

        // a new file is written next to the old one first: a failed or
        // cancelled backup leaves the old file as it was
        wstring tmp = append ? wstring() : wimFile + L".tmp";
        wstring step;
        int rc, image = 0;
        {
            Wim wim;
            if (append)
            {
                Status(L"Opening " + wimFile);
                rc = wimlib_open_wim_with_progress(wimFile.c_str(), OpenWriteAccess, &wim.p, OnProgress, nullptr);
                if (rc != 0) { wim.p = nullptr; return Failed(rc, L"Open " + wimFile); }
            }
            else
            {
                rc = wimlib_create_new_wim(ctype, &wim.p);
                if (rc != 0) { wim.p = nullptr; return Failed(rc, L"Create WIM"); }
                wimlib_register_progress_function(wim.p, OnProgress, nullptr);
            }

            step = L"Capture " + source;
            int addFlags = AddWinConfig | AddVerbose;
            if (snapshot)
            {
                // wimlib creates the VSS snapshot, reads from it and deletes it again
                addFlags |= AddSnapshot;
                step = L"Capture " + source + L" (VSS snapshot)";
                Status(L"Creating a VSS snapshot of " + Root(source));
                Progress(-1, L"snapshot", true);
            }
            else Progress(-1, L"scan 0 0 0", true);
            Status(L"Scanning " + source + L" (Windows capture configuration)");
            rc = wimlib_add_image(wim.p, source.c_str(), name.empty() ? nullptr : name.c_str(), nullptr, addFlags);
            if (rc == 0)
            {
                image = ImageCount(wim.p);
                if (!desc.empty()) wimlib_set_image_property(wim.p, image, L"DESCRIPTION", desc.c_str());
                if (append)
                {
                    step = L"Write image " + Num(image) + L" to " + wimFile;
                    Status(L"Adding image " + Num(image) + L" to " + wimFile);
                    rc = wimlib_overwrite(wim.p, 0, threads);
                }
                else
                {
                    step = L"Write " + tmp;
                    DeleteFileOrThrow(tmp);
                    Status(L"Writing " + tmp);
                    rc = wimlib_write(wim.p, tmp.c_str(), AllImages, ctype == CompressionLzms ? WriteSolid : 0, threads);
                }
            }
        }
        if (rc != 0)
        {
            if (!tmp.empty()) DeleteFileW(tmp.c_str());
            return Failed(rc, step);
        }
        if (!tmp.empty())
        {
            DeleteFileOrThrow(wimFile);
            MoveFileOrThrow(tmp, wimFile);
        }
        if (Get(job, L"verify") != L"1") return Finish(0, L"OK image " + Num(image));
        // the backup is written: a cancelled verification does not undo it
        wstring vstep;
        rc = VerifyCore(wimFile, vstep);
        if (rc == ErrAborted) return Finish(0, L"OK image " + Num(image) + L" (verification cancelled)");
        if (rc == ErrPartsMissing) return Finish(2, L"The backup was written, but its verification failed: " + vstep);
        if (rc != 0) return Failed(rc, L"The backup was written, but its verification failed: " + vstep);
        return Finish(0, L"OK image " + Num(image) + L", verified");
    }

    // ---------- verify ----------

    // the integrity table (when the file has one) and then all the data:
    // wimlib_verify_wim reads every blob and checks its SHA-1 hash, and the
    // metadata of every image. A split WIM needs all of its parts.
    static int VerifyCore(const wstring& wimFile, wstring& step)
    {
        step = L"Open " + wimFile;
        if (!FileExists(wimFile)) { step = L"Not found: " + wimFile; return ErrPartsMissing; }
        Wim wim;
        int total, found;
        Status(L"Opening " + wimFile + L" (with the integrity check)");
        Progress(-1, L"vopen", true);
        int rc = OpenRead(wimFile, OpenCheckIntegrity, OnProgress, wim, total, found);
        if (rc == ErrPartsMissing) { step = OpenText(rc, wimFile, total, found); return rc; }
        if (rc != 0) { step = L"Open " + wimFile + L" (integrity table)"; return rc; }
        if (total > 1) Status(L"Split WIM: " + Num(total) + L" parts");
        WimInfo info;
        bool integrity = info.Read(wim.p) && info.Integrity();
        Status(integrity ? L"The integrity table is correct." : L"The file has no integrity table (the data is checked by its SHA-1 hashes).");
        step = L"Verify " + wimFile;
        Status(L"Verifying the metadata of the images and all the data");
        Progress(-1, L"verify 0 0", true);
        return wimlib_verify_wim(wim.p, 0);
    }

    static int Verify(const Job& job)
    {
        wstring wimFile = Get(job, L"wim"), step;
        int rc = VerifyCore(wimFile, step);
        if (rc == ErrPartsMissing) return Finish(2, step);
        if (rc != 0) return Failed(rc, step);
        return Finish(0, L"OK verified " + wimFile);
    }

    // ---------- export ----------

    // index = 0: all images (with their own names); else one image, with
    // the given name / description (empty = those of the source image)
    static int Export(const Job& job)
    {
        wstring source = Get(job, L"source"), wimFile = Get(job, L"wim");
        wstring name = Get(job, L"name"), desc = Get(job, L"description");
        int index = (int)ParseLong(Get(job, L"index"), 0);
        int ctype = (int)ParseLong(Get(job, L"compression"), CompressionLzx);
        unsigned threads = Threads(job);
        bool append = Get(job, L"append") == L"1" && FileExists(wimFile);
        wstring why = ExportProblem(source, wimFile);
        if (!why.empty()) return Finish(87, L"Refused: " + why + L" (" + source + L" -> " + wimFile + L")");
        CreateDirs(DirName(FullPath(wimFile)));

        Wim src;
        Status(L"Opening " + source);
        int total, found;
        int rc = OpenRead(source, 0, OnProgress, src, total, found);
        if (rc == ErrPartsMissing) return Finish(2, OpenText(rc, source, total, found));
        if (rc != 0) return Failed(rc, L"Open " + source);
        if (total > 1) Status(L"Split WIM: " + Num(total) + L" parts");
        wstring tmp = append ? wstring() : wimFile + L".tmp";
        wstring step;
        int first = 0, last = 0;
        int count = ImageCount(src.p);
        if (index < 0 || index > count)
            return Finish(87, L"Image " + Num(index) + L" is not in " + source + L" (" + Num(count) + L" images).");
        {
            // the destination is written and freed before the source is freed
            Wim dest;
            if (append)
            {
                Status(L"Opening " + wimFile);
                rc = wimlib_open_wim_with_progress(wimFile.c_str(), OpenWriteAccess, &dest.p, OnProgress, nullptr);
                if (rc != 0) { dest.p = nullptr; return Failed(rc, L"Open " + wimFile); }
            }
            else
            {
                rc = wimlib_create_new_wim(ctype, &dest.p);
                if (rc != 0) { dest.p = nullptr; return Failed(rc, L"Create WIM"); }
                wimlib_register_progress_function(dest.p, OnProgress, nullptr);
            }
            first = ImageCount(dest.p) + 1;
            // the boot image of the source stays bootable (all images: the one
            // marked in the source), unless the target has its own
            int srcBoot = BootIndex(src.p);
            int flags = srcBoot > 0 && (index == 0 || index == srcBoot) && BootIndex(dest.p) == 0 ? ExportBoot : 0;
            step = L"Export " + (index == 0 ? wstring(L"all images") : L"image " + Num(index)) + L" of " + source;
            Status(step);
            Progress(-1, L"export", true);
            rc = wimlib_export_image(src.p, index == 0 ? AllImages : index, dest.p,
                index != 0 && !name.empty() ? name.c_str() : nullptr,
                index != 0 && !desc.empty() ? desc.c_str() : nullptr, flags);
            if (rc == 0)
            {
                last = ImageCount(dest.p);
                if (append)
                {
                    step = L"Write images " + Num(first) + L"-" + Num(last) + L" to " + wimFile;
                    Status(L"Adding image(s) " + Num(first) + L"-" + Num(last) + L" to " + wimFile);
                    rc = wimlib_overwrite(dest.p, 0, threads);
                }
                else
                {
                    step = L"Write " + tmp;
                    DeleteFileOrThrow(tmp);
                    Status(L"Writing " + tmp);
                    rc = wimlib_write(dest.p, tmp.c_str(), AllImages, ctype == CompressionLzms ? WriteSolid : 0, threads);
                }
            }
        }
        src.Reset();
        if (rc != 0)
        {
            if (!tmp.empty()) DeleteFileW(tmp.c_str());
            return Failed(rc, step);
        }
        if (!tmp.empty())
        {
            DeleteFileOrThrow(wimFile);
            MoveFileOrThrow(tmp, wimFile);
        }
        return Finish(0, L"OK images " + Num(first) + L"-" + Num(last));
    }

    // ---------- split / join ----------

    static int Split(const Job& job)
    {
        wstring source = Get(job, L"source"), swm = Get(job, L"swm");
        long long partMb = ParseLong(Get(job, L"part_mb"), 4000);
        wstring why = SplitProblem(source, swm);
        if (!why.empty()) return Finish(87, L"Refused: " + why + L" (" + source + L" -> " + swm + L")");
        if (partMb < 1) return Finish(87, L"Part size " + Num(partMb) + L" MB");
        CreateDirs(DirName(FullPath(swm)));
        // old parts of the same name (the GUI asked) would mix with the new ones
        for (const wstring& old : SwmParts(swm)) { Status(L"Deleting the old part " + old); DeleteFileOrThrow(old); }

        int rc;
        {
            Wim wim;
            Status(L"Opening " + source);
            rc = wimlib_open_wim_with_progress(source.c_str(), 0, &wim.p, OnProgress, nullptr);
            if (rc != 0) { wim.p = nullptr; return Failed(rc, L"Open " + source); }
            Status(L"Splitting into parts of " + Num(partMb) + L" MB: " + swm);
            splitting = true;
            Progress(-1, L"split 0 0 0 0", true);
            rc = wimlib_split(wim.p, swm.c_str(), (uint64_t)partMb * MiB, 0);
            splitting = false;
        }
        if (rc != 0)
        {
            // a half written set of parts is of no use
            for (const wstring& part : SwmParts(swm)) DeleteFileW(part.c_str());
            return Failed(rc, L"Split " + source);
        }
        std::vector<wstring> made = SwmParts(swm);
        for (const wstring& part : made) Status(L"  " + part + L"  (" + Num(FileSize(part) / MiB) + L" MB)");
        return Finish(0, L"OK " + Num((long long)made.size()) + L" parts");
    }

    static int Join(const Job& job)
    {
        wstring wimFile = Get(job, L"wim");
        std::vector<wstring> parts;
        for (const wstring& part : dvt::Split(Get(job, L"parts"), L'|'))
            if (!Trim(part).empty()) parts.push_back(Trim(part));
        wstring why = JoinProblem(parts, wimFile);
        if (!why.empty()) return Finish(87, L"Refused: " + why + L" (" + wimFile + L")");
        for (const wstring& part : parts)
            if (!FileExists(part)) return Finish(2, L"Not found: " + part);
        CreateDirs(DirName(FullPath(wimFile)));
        // written next to an old file first: a failed join leaves it as it was
        wstring tmp = wimFile + L".tmp";
        DeleteFileOrThrow(tmp);
        Status(L"Joining " + Num((long long)parts.size()) + L" parts into " + tmp);
        Progress(-1, L"join", true);
        std::vector<const wchar_t*> list;
        for (const wstring& p : parts) list.push_back(p.c_str());
        int rc = wimlib_join_with_progress(list.data(), (unsigned)list.size(), tmp.c_str(), 0, 0, OnProgress, nullptr);
        if (rc != 0)
        {
            DeleteFileW(tmp.c_str());
            return Failed(rc, L"Join " + parts[0]);
        }
        DeleteFileOrThrow(wimFile);
        MoveFileOrThrow(tmp, wimFile);
        return Finish(0, L"OK " + wimFile);
    }

    // ---------- apply ----------

    // boot = UEFI | BIOS | ALL; sys_disk, sys_part, sys_offset name the
    // system partition. A partition without a drive letter gets one for
    // bcdboot; that letter is removed again afterwards.
    static int WriteBootFiles(const Job& job, wchar_t letter, const wstring& fw)
    {
        if (fw != L"UEFI" && fw != L"BIOS" && fw != L"ALL") return Finish(87, L"Unknown firmware: " + fw);
        Progress(-1, L"boot", true);
        wstring windows = wstring(1, letter) + L":\\Windows";
        if (!DirExists(Combine(windows, L"System32"))) { Status(L"No Windows in the image: " + windows + L" is missing."); return 3; }

        int disk = (int)ParseLong(Get(job, L"sys_disk"), -1), part = (int)ParseLong(Get(job, L"sys_part"), -1);
        long long offset = ParseLong(Get(job, L"sys_offset"), -1);
        std::vector<DiskPartition> parts;
        wstring err;
        if (!QueryPartitions(parts, err)) { Status(L"Partitions not read: " + err); return 1; }
        const DiskPartition* sys = nullptr;
        for (const DiskPartition& p : parts)
            if (p.Disk == disk && p.Number == part && p.Offset == offset) sys = &p;
        wstring where = L"disk " + Num(disk) + L", partition " + Num(part);
        if (!sys) { Status(L"The system partition (" + where + L") was not found; the disks have changed."); return 1168; }
        wstring why = SystemPartitionProblem(*sys, fw);
        if (!why.empty()) { Status(L"Refused: " + why + L" (" + where + L")"); return 87; }

        wchar_t sysLetter = sys->Letter;
        bool assigned = false;
        int rc = 0;
        if (sysLetter == 0)
        {
            sysLetter = FreeLetter();
            if (sysLetter == 0) { Status(L"No free drive letter for the system partition."); return 1; }
            Status(L"Drive letter " + wstring(1, sysLetter) + L": for " + where);
            DWORD e = storage::AddLetter(disk, offset, sysLetter);
            if (e != 0) { Status(L"The drive letter was not assigned: " + ErrorMessage(e)); return (int)e; }
            assigned = true;
        }
        // the letter given above is removed again, whatever happens
        struct Unassign
        {
            bool& on; wchar_t letter;
            ~Unassign()
            {
                if (!on) return;
                try
                {
                    DWORD e = storage::RemoveLetter(letter);
                    Status(e == 0 ? L"Drive letter " + wstring(1, letter) + L": removed again."
                        : L"Drive letter " + wstring(1, letter) + L": was not removed: " + ErrorMessage(e));
                }
                catch (...) {}
            }
        } unassign{ assigned, sysLetter };

        if (assigned && !WaitFolder(wstring(1, sysLetter) + L":\\")) { Status(wstring(1, sysLetter) + L": is not ready."); return 21; }
        // BIOS starts the active partition of an MBR disk
        if (fw != L"UEFI" && !sys->Gpt() && !sys->Active)
        {
            Status(L"Disk " + Num(disk) + L": partition " + Num(part) + L" is made the active partition.");
            DWORD e = storage::SetActive(disk, part);
            if (e != 0) { Status(L"The partition was not made active: " + ErrorMessage(e)); return (int)e; }
        }
        wstring args = windows + L" /s " + wstring(1, sysLetter) + L": /f " + fw;
        wstring bootLang = Lower(Get(job, L"boot_lang"));
        if (IsLocale(bootLang)) args += L" /l " + bootLang;
        Status(L"bcdboot " + args);
        rc = RunTool(Combine(SystemDir(), L"bcdboot.exe"), args);
        if (rc != 0) Status(L"bcdboot ended with code " + Num(rc) + L".");
        return rc;
    }

    // apply one image into an empty (or new) folder; nothing is formatted
    static int ApplyToFolder(const wstring& wimFile, int index, const wstring& folder, const wstring& buildRoot)
    {
        wstring why = FolderTargetProblem(folder, wimFile, buildRoot);
        if (!why.empty()) return Finish(87, L"Refused: " + why + L" (" + folder + L")");
        if (!FileExists(wimFile)) return Finish(2, L"Not found: " + wimFile);
        wstring dir = FullPath(folder);

        Wim wim;
        int total, found;
        int rc = OpenRead(wimFile, 0, OnProgress, wim, total, found);
        if (rc == ErrPartsMissing) return Finish(2, OpenText(rc, wimFile, total, found));
        if (rc != 0) return Failed(rc, L"Open " + wimFile);
        if (total > 1) Status(L"Split WIM: " + Num(total) + L" parts");
        int count = ImageCount(wim.p);
        if (index < 1 || index > count)
            return Finish(87, L"Image " + Num(index) + L" is not in " + wimFile + L" (" + Num(count) + L" images).");
        long long need = ParseLong(Property(wim.p, index, L"TOTALBYTES"), 0);
        CreateDirs(dir);
        wstring root = Root(dir);
        long long free = DriveFreeBytes(root);
        if (need > 0 && free >= 0 && free < need)
            return Finish(112, root + L" has " + Num(free / MiB) + L" MB free, image " + Num(index) + L" needs " + Num(need / MiB) + L" MB.");

        Status(L"Applying image " + Num(index) + L" (" + Property(wim.p, index, L"NAME") + L") to " + dir);
        rc = wimlib_extract_image(wim.p, index, dir.c_str(), 0);
        if (rc != 0) return Failed(rc, L"Apply image " + Num(index) + L" to " + dir);
        return Finish(0, L"OK " + dir);
    }

    static int Apply(const Job& job)
    {
        wstring wimFile = Get(job, L"wim"), target = Get(job, L"target");
        wstring label = CleanLabel(Get(job, L"label"));
        int index = (int)ParseLong(Get(job, L"index"), 1);
        wstring folder = Get(job, L"folder");
        if (!folder.empty()) return ApplyToFolder(wimFile, index, folder, Get(job, L"root"));
        if (target.empty()) return Finish(87, L"No target volume.");
        wchar_t letter = UpperChar(target[0]);
        wstring l = wstring(1, letter);
        wstring why = TargetProblem(letter, wimFile, Get(job, L"root"));
        if (!why.empty()) return Finish(87, L"Refused: " + why + L" (" + l + L":)");
        if (!FileExists(wimFile)) return Finish(2, L"Not found: " + wimFile);

        {
            Wim wim;
            int total, found;
            int rc = OpenRead(wimFile, 0, OnProgress, wim, total, found);
            if (rc == ErrPartsMissing) return Finish(2, OpenText(rc, wimFile, total, found));
            if (rc != 0) return Failed(rc, L"Open " + wimFile);
            if (total > 1) Status(L"Split WIM: " + Num(total) + L" parts");
            int count = ImageCount(wim.p);
            if (index < 1 || index > count)
                return Finish(87, L"Image " + Num(index) + L" is not in " + wimFile + L" (" + Num(count) + L" images).");
            long long need = ParseLong(Property(wim.p, index, L"TOTALBYTES"), 0);

            Progress(-1, L"format", true);
            Status(L"Formatting " + l + L": (NTFS, label \"" + label + L"\")");
            wstring reason;
            DWORD frc = storage::Format(letter, L"NTFS", label, reason);
            if (frc != 0) return Finish((int)frc, L"Format " + l + L": failed: " + reason + L" (code " + Num(frc) + L").");
            if (!WaitReady(letter, L"NTFS")) return Finish(21, l + L": is not ready after the format.");
            long long free = DriveFreeBytes(l + L":\\");
            if (need > 0 && free >= 0 && free < need)
                return Finish(112, l + L": has " + Num(free / MiB) + L" MB free, image " + Num(index) + L" needs " + Num(need / MiB) + L" MB.");

            Status(L"Applying image " + Num(index) + L" (" + Property(wim.p, index, L"NAME") + L") to " + l + L":\\");
            rc = wimlib_extract_image(wim.p, index, (l + L":\\").c_str(), 0);
            if (rc != 0) return Failed(rc, L"Apply image " + Num(index) + L" to " + l + L":");
        }
        wstring fw = Get(job, L"boot");
        if (fw.empty()) return Finish(0, L"OK " + l + L":");
        int brc = WriteBootFiles(job, letter, fw);
        if (brc != 0) return Finish(brc, L"The image was applied to " + l + L":, but the boot files were not written (code " + Num(brc) + L").");
        return Finish(0, L"OK " + l + L": + boot files (" + fw + L")");
    }

    // ---------- entry ----------

    int RunHelper(const std::vector<wstring>& args)
    {
        if (args.size() < 2) return 87;
        wstring work = args[1];
        statusFile = Combine(work, L"status.txt");
        cancelFile = Combine(work, L"cancel");
        errorFile = Combine(work, L"wimlib_errors.txt");
        if (!WriteBytes(statusFile, std::string())) return 5;
        try
        {
            Job job = ReadJob(Combine(work, L"job.ini"));
            wstring mode = Get(job, L"mode");
            if (mode == L"disk") return Disk(job);     // diskpart / Storage module, no wimlib
            // export, split, join and verify run without administrator rights: no privileges to acquire
            wstring err = Load(Get(job, L"tools"), mode == L"capture" || mode == L"apply");
            if (!err.empty()) return Finish(2, err);
            wimlib_set_error_file_by_name(errorFile.c_str());
            if (mode == L"capture") return Capture(job);
            if (mode == L"export") return Export(job);
            if (mode == L"split") return Split(job);
            if (mode == L"join") return Join(job);
            if (mode == L"verify") return Verify(job);
            if (mode == L"apply") return Apply(job);
            return Finish(87, L"Unknown mode: " + mode);
        }
        catch (const HelperError& e) { return Finish(1, e.msg); }
        catch (const std::exception& e) { return Finish(1, FromUtf8(e.what())); }
    }
}
