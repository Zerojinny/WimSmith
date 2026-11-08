// ============================================================
//  7z_wimlib - tab_browse.h
//  The browse tab: the files of a WIM / ESD / SWM file, read with
//  wimlib (lib\libwim-15.dll, the same as the other tabs).
//   - wimlib_iterate_dir_tree lists the chosen image in the background;
//     the folders are shown as a tree, the content of a folder as a list.
//   - Extract: wimlib_extract_paths puts the selected files / folders
//     into a hidden work folder in the chosen folder (without ACLs, so no
//     administrator rights), then they are moved up; a cancelled extract
//     leaves nothing in the target.
//   - Open: a file is extracted to %TEMP%\7z_wimlib_view and opened with
//     its program (a program file: its folder is shown instead).
//   - Edit (the [Edit] button, the right mouse button, Del / F2 / F5): new
//     folder, add files / a folder, rename, delete in the image shown.
//     wimlib_update_image, then wimlib_overwrite (in place: the new data is
//     appended, the file stays usable when this is cancelled or fails);
//     added files get no ACLs (no administrator rights needed). Not for a
//     split WIM or a read-only file. The listing is read again afterwards.
//  Only the edit commands write to the WIM file.
// ============================================================
#pragma once
#include "app.h"
#include <atomic>
#include <memory>

namespace sevenwim
{
    struct BrowseTab
    {
        BrowseTab(App& app, HWND page);
        void SetDefault(const wstring& wim);
        void ShowWim(const wstring& wim, int image);  // from the info tab
        void WimChanged(const wstring& wim);          // the info tab changed it: images read again
        bool Extracting() const { return running && !listing; }
        void Cancel();
        void ShowProgress(HWND target, int pct, const wstring& state);   // from the extracting thread

        App& app;
        HWND page;

        struct Node;
        struct Row
        {
            wstring Name, Path, Modified, Attr;   // Path: inside the image ("Windows\notepad.exe")
            long long Size = -1;
            Node* Dir = nullptr;              // a folder
        };
        struct Node
        {
            wstring Name, Path, Modified, Attr;   // Path: inside the image ("" = root, "Windows\System32")
            Node* Parent = nullptr;
            std::vector<std::unique_ptr<Node>> Dirs;
            std::vector<Row> Files;
            Node* Child(const wstring& name);
        };

    private:
        HWND txtWim, btnBrowse, btnRead, cboImage, btnUp, lblPath;
        HWND tree, splitter, list, btnExtract, btnOpen, btnEdit, bar, btnCancel, lblState;
        ui::Grid grid;
        std::vector<int> images;              // the items of cboImage: the image numbers
        wstring wimFile;                      // the file of the tree shown
        int wimImage = 1;                     // its image
        std::shared_ptr<Node> top;
        Node* current = nullptr;
        std::vector<Row> rows;
        int sortColumn = 0, splitX = -1;
        bool sortDown = false, listing = false, running = false, dragging = false;
        std::shared_ptr<std::atomic<bool>> abort;   // the running listing / extraction stops
        wstring notWritable;                  // why the WIM file shown cannot be changed ("" = it can)
        wstring reopenPath;                   // the folder to show again after an edit
        wstring doneText;                     // the state after the listing that follows an edit
        typedef wl::UpdateCommand UpdateCmd;
        bool needsAdmin = false;              // the file shown can be changed only with administrator rights
        bool appBusy = false;                 // a job of the helper runs (here: an elevated edit)

        void Layout(int w, int h);
        void UpdateButtons();
        void SetState(const wstring& text) { ui::SetText(lblState, text); ui::Relayout(page); }
        void ChooseWim();
        void OpenWim(bool show);
        void ListImage();
        void ListDone(std::shared_ptr<Node> root, int count, int rc, const wstring& err, const wstring& wim, int image,
            ULONGLONG took, const wstring& why, bool admin);
        Node* FindNode(const wstring& path) const;
        // ---- edit ----
        void ShowMenu(POINT pt, Node* folder);        // folder: from the tree (else the list selection)
        bool CanEdit();
        void NewFolder(Node* folder);
        void AddFiles(Node* folder);
        void AddFolder(Node* folder);
        void Rename(const Row& r, Node* parent);
        void Delete(const std::vector<Row>& items);
        void RunUpdate(const std::vector<UpdateCmd>& cmds, const wstring& what, const wstring& cleanup);
        void RunUpdateElevated(const std::vector<UpdateCmd>& cmds, const wstring& what, const wstring& cleanup);
        void UpdateDone(int rc, const wstring& err, const wstring& what, ULONGLONG took);
        HTREEITEM AddTreeItem(HTREEITEM parent, Node* n);
        void FillTreeItem(HTREEITEM item);
        HTREEITEM FindTreeItem(Node* n);
        void ShowFolder(Node* n, bool syncTree);
        std::vector<Row> SelectedRows() const;
        int IconFor(const wstring& name, bool dir);
        void RunExtract(const std::vector<wstring>& paths, const wstring& target, const wstring& state,
            std::function<void(int, const wstring&)> done);
        void ExtractSelected();
        void OpenSelected();
    };
}
