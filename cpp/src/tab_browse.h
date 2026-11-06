// ============================================================
//  7z_wimlib (C++) - tab_browse.h
//  The browse tab: the files of a WIM / ESD file, read with 7-Zip
//  (lib\7z.exe).
//   - 7z l -ba lists one image (a WIM with several images: "<n>\*",
//     the image chosen in the list; one image: the whole file). The
//     folders are shown as a tree, the content of a folder as a list.
//   - Extract: the selected files / folders go into a chosen folder
//     (7z x into a hidden work folder there, then moved up, so that the
//     path inside the image is not repeated).
//   - Open: a file is extracted to %TEMP%\7z_wimlib_view and opened with
//     its program (a program file: its folder is shown instead).
//   - 7-Zip: the WIM file opened in 7-Zip's own file manager.
//  Nothing is written to the WIM file.
// ============================================================
#pragma once
#include "app.h"
#include <memory>

namespace dvt
{
    struct BrowseTab
    {
        BrowseTab(App& app, HWND page);
        void SetDefault(const wstring& wim);
        void ShowWim(const wstring& wim, int image);  // from the info tab
        bool Extracting() const { return proc != nullptr && !listing; }
        void Cancel();

        App& app;
        HWND page;

        struct Node;
        struct Row
        {
            wstring Name, Path, Modified, Attr;
            long long Size = -1;
            Node* Dir = nullptr;              // a folder
        };
        struct Node
        {
            wstring Name, Path, Modified, Attr;   // Path: inside the archive ("" = root, "2\Windows")
            Node* Parent = nullptr;
            std::vector<std::unique_ptr<Node>> Dirs;
            std::vector<Row> Files;
            Node* Child(const wstring& name);
        };

    private:
        HWND txtWim, btnBrowse, btnRead, btnFm, cboImage, btnUp, lblPath;
        HWND tree, splitter, list, btnExtract, btnOpen, bar, btnCancel, lblState;
        ui::Grid grid;
        std::vector<wstring> prefixes;        // the items of cboImage: "" (whole file) or "<n>"
        wstring wimFile;                      // the file of the tree shown
        std::shared_ptr<Node> top;
        Node* current = nullptr;
        std::vector<Row> rows;
        int sortColumn = 0, splitX = -1;
        bool sortDown = false, listing = false, cancelRequested = false, dragging = false;
        HANDLE proc = nullptr;                // 7z.exe that runs (listing or extracting)

        void Layout(int w, int h);
        void UpdateButtons();
        void SetState(const wstring& text) { ui::SetText(lblState, text); ui::Relayout(page); }
        bool Ensure7zip();
        wstring SevenZip() const;
        void ChooseWim();
        void OpenWim(bool show);
        void ListImage();
        void ListDone(std::shared_ptr<Node> root, int count, int code, const wstring& err, const wstring& wim, ULONGLONG took);
        HTREEITEM AddTreeItem(HTREEITEM parent, Node* n);
        void FillTreeItem(HTREEITEM item);
        HTREEITEM FindTreeItem(Node* n);
        void ShowFolder(Node* n, bool syncTree);
        std::vector<Row> SelectedRows() const;
        int IconFor(const wstring& name, bool dir);
        bool Start7z(const wstring& args, HANDLE& out);
        void RunExtract(const wstring& args, const std::vector<wstring>& paths, const wstring& state, std::function<void(int, const wstring&)> done);
        void ExtractSelected();
        void OpenSelected();
        void OpenIn7zFm();
    };
}
