// ============================================================
//  WimSmith - app.h
//  What the tabs share (the C# BackupPanel apart from its tabs): the
//  main window, the volumes, wimlib in the GUI process, the
//  confirmations, and the job: the helper (this exe --wim-helper <work
//  folder>, elevated for capture / apply / disk) with its work folder
//  %TEMP%\WimSmith_job\<time> (job.ini, status.txt), its progress in the
//  footer, [Cancel], and after a successful job: nothing / shut down /
//  restart. The work folder is deleted after a successful job and kept
//  after a failed or cancelled one.
// ============================================================
#pragma once
#include "disk.h"
#include "mainwin.h"
#include "wim.h"
#include <functional>

namespace wimsmith
{
    struct RestoreTab;
    struct DiskTab;
    struct BackupTab;
    struct ExportTab;
    struct SplitJoinTab;
    struct InfoTab;
    struct BrowseTab;
    struct AboutTab;

    // a volume (or a chosen folder: Letter 0) of the source / target lists
    struct Volume
    {
        wchar_t Letter = 0;
        wstring Path, Text;
        long long Size = -1;
    };

    class App
    {
    public:
        MainWindow win;
        wstring root;                         // the folder of the exe
        RestoreTab* restore = nullptr;
        DiskTab* disk = nullptr;
        BackupTab* backup = nullptr;
        ExportTab* exportTab = nullptr;
        SplitJoinTab* splitJoin = nullptr;
        InfoTab* infoTab = nullptr;
        BrowseTab* browse = nullptr;
        AboutTab* about = nullptr;

        void Create();
        void RefreshStatus();                 // the default files and the lists, once shown
        // a WIM file given to the exe (opened from Explorer): in the tab of
        // the setting "open_tab" (browse | info | restore | export, default browse)
        void OpenFile(const wstring& file);
        bool Busy() const { return helper != nullptr; }

        // ---- shared ----
        wstring ToolDir() const { return Combine(root, L"lib"); }
        bool LoadWimlib();                    // false (with a message) when libwim-15.dll cannot be used
        wstring DefaultWim() const;           // <folder of the last backup>\backup.wim ("" before)
        wstring ShortPath(const wstring& path) const;
        void Log(const wstring& text) { win.Log(text); }
        void Status(const wstring& text) { win.SetStatus(text); }
        // the images of a WIM file (false with a log line / message)
        bool Images(const wstring& wim, bool show, std::vector<wl::WimImage>& list);
        // a WIM file was written: the tabs that show it read it again; an
        // empty restore tab (backup) or an empty info tab (export, join) takes it
        void WimWritten(const wstring& wim, bool toRestore = true);
        static int Processors();
        // the browse tab with an image of a WIM file (0 = the file)
        void Browse(const wstring& wim, int image);
        static wstring Gb(long long bytes);
        static wstring Group(long long n);    // 1,234,567
        static std::vector<Volume> Volumes(bool readyOnly);
        // the second confirmation: the drive letter (or disk number) has to be typed
        bool ConfirmTyped(const wstring& text, const wstring& expected, const wstring& titleKey = L"bk.confirm_type_title");

        // ---- the job ----
        struct JobInfo
        {
            wstring mode, wim, target, boot;  // target: a letter, or a folder (restore into a folder)
            int image = 0;
            bool verify = false, integrity = false;
            wchar_t newLetter = 0;            // "LETTER x" of the disk job
            bool ToFolder() const { return target.size() > 1; }
        };
        JobInfo job;
        // after code 0: the done message ("" = none, and no "after done")
        std::function<wstring(const wstring& elapsed)> onSuccess;
        std::function<void()> onFailure;      // after a failed or cancelled job
        std::vector<std::function<void(bool busy)>> busyHandlers;
        void StartHelper(Job values);
        void CancelJob();

    private:
        HANDLE helper = nullptr;
        wstring workDir, statusFile, phaseText;
        size_t linesShown = 0;
        ULONGLONG started = 0;
        void SetBusy(bool busy);
        void Poll();
        bool ReadLines(wstring& exit);
        void OnProgressLine(const wstring& line);
        void ShowProgress();
        void DeleteWorkDir();
        void AfterDone(const wstring& msg);
        bool Countdown(const wstring& msg, const wstring& countdownKey);
        static wstring Duration(ULONGLONG ms);
    };
}
