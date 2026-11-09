// ============================================================
//  WimSmith - mainwin.h
//  The main window (the C# MainForm + the frame of BackupPanel): the
//  tab strip and its pages, a thin progress line and the footer
//  (state | last log line | [Show log] | after done | [Cancel] |
//  [Settings] | language). The log has a window of its own (LogWindow),
//  hidden until [Show log]. The tabs are added by their own files.
//  [Settings]: a dialog with the theme, .wim / .esd / .swm opened with
//  WimSmith (assoc.cpp), the tab such a file opens in (setting
//  "open_tab") and the right-click menu "WimSmith >" of Explorer.
// ============================================================
#pragma once
#include "ui.h"

namespace wimsmith
{
    class LogWindow
    {
    public:
        void Create(HWND owner);
        void Append(const wstring& line);
        void ShowNear(HWND main);
    private:
        HWND win = nullptr, box = nullptr;
        bool placed = false;
    };

    class MainWindow
    {
    public:
        HWND hwnd = nullptr;
        HWND tabs = nullptr, progress = nullptr, footer = nullptr;
        HWND lblState = nullptr, lblLast = nullptr, lnkLog = nullptr, lblAfter = nullptr;
        HWND cboAfter = nullptr, btnCancel = nullptr, cboLang = nullptr, btnSettings = nullptr;
        bool restart = false;                 // another language or theme was chosen
        bool maximize = false;                // the screen is smaller than the window
        LogWindow log;

        void Create();
        // a new tab; returns its page (a panel with 8 px padding around it)
        HWND AddPage(const wstring& title);
        void SelectPage(HWND page);
        bool PageShown(HWND page) const;
        void Log(const wstring& text);        // "HH:mm:ss  text" to the log and the footer
        void SetStatus(const wstring& text);  // the state at the left of the footer
        void UpdateMenuLanguage();            // the right-click menu of Explorer in the language shown
        std::function<bool()> busy;           // a job runs (set by App)
        bool Busy() const { return busy && busy(); }
        std::function<bool()> extracting;     // the browse tab extracts files
        std::function<void()> stopExtracting;

    private:
        std::vector<HWND> pages, hosts;       // the pages of the tabs, their scrolling hosts
        std::vector<wstring> langCodes;
        void Layout(int w, int h);
        void LayoutFooter(int w, int h);
        void ShowPage();
        void ChangeLanguage();
        void ShowSettings();
        bool CanRestart();                    // no job, no extraction (else a message)
    };
}
