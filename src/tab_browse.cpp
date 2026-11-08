// ============================================================
//  7z_wimlib - tab_browse.cpp
//  The browse tab (see tab_browse.h).
// ============================================================
#include "tab_browse.h"
#include "lang.h"
#include "libs.h"
#include <algorithm>
#include <commctrl.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <map>
#include <thread>

namespace sevenwim
{
    using namespace ui;

    BrowseTab::Node* BrowseTab::Node::Child(const wstring& name)
    {
        for (auto& d : Dirs) if (EqualsI(d->Name, name)) return d.get();
        std::unique_ptr<Node> c(new Node());
        c->Name = name;
        c->Path = Path.empty() ? name : Path + L"\\" + name;
        c->Parent = this;
        Dirs.push_back(std::move(c));
        return Dirs.back().get();
    }

    static wstring ViewRoot()
    {
        wchar_t tmp[MAX_PATH + 1];
        GetTempPathW(MAX_PATH + 1, tmp);
        return Combine(tmp, L"7z_wimlib_view");
    }

    static wstring Fixed(double v, int decimals, bool trim)
    {
        wchar_t b[64];
        swprintf_s(b, L"%.*f", decimals, v);
        wstring s = b;
        if (trim && s.find(L'.') != wstring::npos)
        {
            while (s.back() == L'0') s.pop_back();
            if (s.back() == L'.') s.pop_back();
        }
        return s;
    }

    static wstring SizeText(long long b)
    {
        if (b < 0) return wstring();
        if (b < 1024) return Num(b) + L" B";
        if (b < 1048576) return Fixed(b / 1024.0, 1, true) + L" KB";
        if (b < 1073741824LL) return Fixed(b / 1048576.0, 1, true) + L" MB";
        return Fixed(b / 1073741824.0, 2, true) + L" GB";
    }

    // the splitter between the tree and the list: dragged with the mouse
    static LRESULT CALLBACK SplitterSub(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR self)
    {
        BrowseTab* b = (BrowseTab*)self;
        switch (m)
        {
        case WM_SETCURSOR: SetCursor(LoadCursor(nullptr, IDC_SIZEWE)); return TRUE;
        case WM_LBUTTONDOWN: SetCapture(h); return 0;
        case WM_LBUTTONUP: ReleaseCapture(); return 0;
        case WM_MOUSEMOVE:
            if (GetCapture() == h)
            {
                // the page keeps the position (the tree and the list at least 120 px)
                POINT p;
                GetCursorPos(&p);
                ScreenToClient(b->page, &p);
                SendMessageW(b->page, WM_APP + 2, (WPARAM)p.x, 0);
            }
            return 0;
        }
        return DefSubclassProc(h, m, wp, lp);
    }

    BrowseTab::BrowseTab(App& a, HWND p) : app(a), page(p), grid(p)
    {
        // short labels: the first row (field and three buttons) fits the smallest window
        grid.labelWidth = 110;
        // the tree and the list take what is left: a low page is still usable
        SetScrollMin(page, 800, 280);
        txtWim = Edit(page);
        btnBrowse = Button(page, L::T(L"qbt.btn_browse"));
        btnRead = Button(page, L::T(L"br.btn_read"));
        btnFm = Button(page, L::T(L"br.btn_7zfm"));
        grid.Add(L::T(L"bk.row_wim"), { Field(txtWim, 360), Btn(btnBrowse, 100), Btn(btnRead, 80), Btn(btnFm, 120) });
        cboImage = Combo(page);
        btnUp = Button(page, L::T(L"br.btn_up"));
        lblPath = Label(page, L"", FontUI, color::Dim, SS_LEFT | SS_ENDELLIPSIS);
        Item path = Txt(lblPath, 6, 0);
        path.fill = true;
        grid.Add(L::T(L"bk.row_image"), { Field(cboImage, 360), Btn(btnUp, 80), path });

        // the icons of the shell (the system image list, shared)
        SHFILEINFOW sfi = {};
        HIMAGELIST icons = (HIMAGELIST)SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof sfi,
            SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
        tree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | TVS_HASBUTTONS | TVS_LINESATROOT
            | TVS_SHOWSELALWAYS, 0, 0, 10, 10, page, nullptr, Inst, nullptr);
        SendMessageW(tree, WM_SETFONT, (WPARAM)FontUI, FALSE);
        SetWindowTheme(tree, ThemeApp(L"DarkMode_Explorer"), nullptr);
        TreeView_SetBkColor(tree, color::Panel);
        TreeView_SetTextColor(tree, color::Text);
        TreeView_SetLineColor(tree, color::Dim);
        if (icons) TreeView_SetImageList(tree, icons, TVSIL_NORMAL);
        splitter = Panel(page, color::Back);
        SetWindowSubclass(splitter, SplitterSub, 1, (DWORD_PTR)this);
        list = List(page, { { L::T(L"br.col_name"), 300 }, { L::T(L"br.col_size"), -100 }, { L::T(L"br.col_modified"), 140 }, { L::T(L"br.col_attr"), 60 } },
            LVS_OWNERDATA);
        if (icons) ListView_SetImageList(list, icons, LVSIL_SMALL);

        btnExtract = Button(page, L::T(L"br.btn_extract"));
        Primary(btnExtract, false);
        btnOpen = Button(page, L::T(L"br.btn_open"));
        btnEdit = Button(page, L::T(L"br.btn_edit"));
        bar = Progress(page);
        btnCancel = Button(page, L::T(L"bk.btn_cancel"));
        lblState = Label(page, L::T(L"br.none"), FontUI, color::Text, SS_LEFT | SS_ENDELLIPSIS);
        SetLayout(page, [this](int w, int h) { Layout(w, h); });
        // the splitter reports where it is dragged to
        SetWindowSubclass(page, [](HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR self) -> LRESULT
        {
            if (m == WM_APP + 2)
            {
                BrowseTab* b = (BrowseTab*)self;
                RECT rc;
                GetClientRect(h, &rc);
                b->splitX = std::max(S(120), std::min((int)wp, (int)rc.right - S(120)));
                Relayout(h);
                return 0;
            }
            return DefSubclassProc(h, m, wp, lp);
        }, 3, (DWORD_PTR)this);

        OnClick(btnBrowse, [this] { ChooseWim(); });
        OnClick(btnRead, [this] { OpenWim(true); });
        OnClick(btnFm, [this] { OpenIn7zFm(); });
        OnChange(cboImage, [this] { if (!listing) ListImage(); });
        OnClick(btnUp, [this] { if (current && current->Parent) ShowFolder(current->Parent, true); });
        OnNotify(tree, [this](NMHDR* n) -> LRESULT
        {
            if (n->code == NM_RCLICK)
            {
                // the folder under the mouse: selected, then its menu
                POINT pt;
                GetCursorPos(&pt);
                TVHITTESTINFO hit = {};
                hit.pt = pt;
                ScreenToClient(tree, &hit.pt);
                HTREEITEM item = TreeView_HitTest(tree, &hit);
                if (!item) return 0;
                TreeView_SelectItem(tree, item);
                TVITEMW it = {};
                it.mask = TVIF_PARAM;
                it.hItem = item;
                TreeView_GetItem(tree, &it);
                if (it.lParam) ShowMenu(pt, (Node*)it.lParam);
                return 1;
            }
            if (n->code == TVN_KEYDOWN)
            {
                NMTVKEYDOWN* k = (NMTVKEYDOWN*)n;
                Node* node = current;
                if (!node) return 0;
                Row r;
                r.Name = node->Name;
                r.Path = node->Path;
                r.Dir = node;
                if (k->wVKey == VK_F2 && node->Parent) Rename(r, node->Parent);
                else if (k->wVKey == VK_DELETE && node->Parent) Delete({ r });
                else if (k->wVKey == VK_F5 && !running && top) { reopenPath = node->Path; ListImage(); }
                else if (k->wVKey == VK_APPS)
                {
                    RECT rc;
                    HTREEITEM sel = TreeView_GetSelection(tree);
                    if (sel && TreeView_GetItemRect(tree, sel, &rc, TRUE))
                    {
                        POINT pt = { rc.left, rc.bottom };
                        ClientToScreen(tree, &pt);
                        ShowMenu(pt, node);
                    }
                }
                return 0;
            }
            if (n->code == TVN_ITEMEXPANDINGW)
            {
                NMTREEVIEWW* t = (NMTREEVIEWW*)n;
                if (t->action == TVE_EXPAND) FillTreeItem(t->itemNew.hItem);
            }
            else if (n->code == TVN_SELCHANGEDW)
            {
                NMTREEVIEWW* t = (NMTREEVIEWW*)n;
                Node* node = (Node*)t->itemNew.lParam;
                if (node && node != current) ShowFolder(node, false);
            }
            return 0;
        });
        OnListNotify(list, [this](NMHDR* n, LRESULT& r) -> bool
        {
            switch (n->code)
            {
            case LVN_GETDISPINFOW:
            {
                NMLVDISPINFOW* d = (NMLVDISPINFOW*)n;
                int i = d->item.iItem;
                if (i < 0 || i >= (int)rows.size()) return true;
                const Row& row = rows[i];
                if (d->item.mask & LVIF_TEXT)
                {
                    wstring t = d->item.iSubItem == 0 ? row.Name : d->item.iSubItem == 1 ? (row.Dir ? wstring() : SizeText(row.Size))
                        : d->item.iSubItem == 2 ? row.Modified : row.Attr;
                    wcsncpy_s(d->item.pszText, d->item.cchTextMax, t.c_str(), _TRUNCATE);
                }
                if (d->item.mask & LVIF_IMAGE) d->item.iImage = IconFor(row.Name, row.Dir != nullptr);
                r = 0;
                return true;
            }
            case LVN_COLUMNCLICK:
            {
                NMLISTVIEW* c = (NMLISTVIEW*)n;
                sortDown = c->iSubItem == sortColumn ? !sortDown : false;
                sortColumn = c->iSubItem;
                if (current) ShowFolder(current, false);
                return true;
            }
            case LVN_KEYDOWN:
            {
                NMLVKEYDOWN* k = (NMLVKEYDOWN*)n;
                std::vector<Row> sel = SelectedRows();
                if (k->wVKey == VK_RETURN) OpenSelected();
                else if (k->wVKey == VK_BACK && current && current->Parent) ShowFolder(current->Parent, true);
                else if (k->wVKey == VK_DELETE && !sel.empty()) Delete(sel);
                else if (k->wVKey == VK_F2 && sel.size() == 1) Rename(sel[0], current);
                else if (k->wVKey == VK_F5 && !running && top) { reopenPath = current ? current->Path : wstring(); ListImage(); }
                else if (k->wVKey == VK_APPS)
                {
                    // the menu at the focused row (or the top of the list)
                    POINT pt = { S(20), S(30) };
                    int f = ListView_GetNextItem(list, -1, LVNI_FOCUSED);
                    RECT rc;
                    if (f >= 0 && ListView_GetItemRect(list, f, &rc, LVIR_LABEL)) pt = { rc.left, rc.bottom };
                    ClientToScreen(list, &pt);
                    ShowMenu(pt, nullptr);
                }
                return true;
            }
            case NM_RCLICK:
            {
                POINT pt;
                GetCursorPos(&pt);
                ShowMenu(pt, nullptr);
                return true;
            }
            }
            return false;
        });
        OnChange(list, [this] { UpdateButtons(); });
        OnListDoubleClick(list, [this] { OpenSelected(); });
        OnClick(btnExtract, [this] { ExtractSelected(); });
        OnClick(btnOpen, [this] { OpenSelected(); });
        OnClick(btnEdit, [this]
        {
            RECT rc;
            GetWindowRect(btnEdit, &rc);
            ShowMenu(POINT{ rc.left, rc.bottom }, nullptr);
        });
        OnClick(btnCancel, [this] { Cancel(); });
        // Enter in the file field reads it
        SetWindowSubclass(txtWim, [](HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR self) -> LRESULT
        {
            if (m == WM_GETDLGCODE && wp == VK_RETURN) return DLGC_WANTALLKEYS;
            if (m == WM_KEYDOWN && wp == VK_RETURN) { ((BrowseTab*)self)->OpenWim(true); return 0; }
            if (m == WM_CHAR && wp == L'\r') return 0;
            return DefSubclassProc(h, m, wp, lp);
        }, 4, (DWORD_PTR)this);
        app.busyHandlers.push_back([this](bool b) { appBusy = b; UpdateButtons(); });
        app.win.extracting = [this] { return Extracting(); };
        app.win.stopExtracting = [this] { Cancel(); };
        UpdateButtons();
        // files opened before (best effort: an open file stays)
        try { if (DirExists(ViewRoot())) DeleteTree(ViewRoot()); }
        catch (const HelperError&) {}
    }

    void BrowseTab::Layout(int w, int h)
    {
        int y = grid.Place(0, 0, w);
        int bottom = h - S(38);
        if (splitX < 0 && w >= S(500)) splitX = std::max(S(200), w / 3);   // the real width is known when first shown
        int sx = std::max(S(120), std::min(splitX < 0 ? S(200) : splitX, w - S(120)));
        Move(tree, 0, y, sx, std::max(0, bottom - y));
        Move(splitter, sx, y, S(4), std::max(0, bottom - y));
        Move(list, sx + S(4), y, std::max(0, w - sx - S(4)), std::max(0, bottom - y));
        // the row below: [Extract] [Open] (bar) [Cancel] state
        int by = bottom + S(4);
        int x = 0;
        Move(btnExtract, x, by + S(2), S(150), S(28)); x += S(150) + S(8);
        Move(btnOpen, x, by + S(2), S(110), S(28)); x += S(110) + S(8);
        Move(btnEdit, x, by + S(2), S(100), S(28)); x += S(100) + S(8);
        x += S(8);
        Move(bar, x, by + S(15), S(140), S(4)); x += S(140) + S(8);
        Move(btnCancel, x, by + S(2), S(80), S(28)); x += S(80) + S(8);
        Move(lblState, x + S(6), by + S(8), std::max(0, w - x - S(6)), TextHeight(FontUI) + S(2));
    }

    // ---------- state ----------

    void BrowseTab::Cancel()
    {
        if (running && abort) abort->store(true);
    }

    void BrowseTab::UpdateButtons()
    {
        bool any = ListView_GetSelectedCount(list) > 0;
        bool busy = running || appBusy;
        Enable(btnRead, !busy);
        Enable(txtWim, !busy);
        Enable(cboImage, !busy && ComboCount(cboImage) > 1);
        Enable(btnUp, !busy && current && current->Parent);
        Enable(btnExtract, !busy && top && any);
        Enable(btnOpen, !busy && top && any);
        Enable(btnEdit, !busy && top);
        Enable(btnCancel, running);
        Enable(tree, !running || !listing);
        Enable(list, !running || !listing);
    }

    // 7-Zip: only for its file manager (lib\7zFM.exe), downloaded when missing
    bool BrowseTab::Ensure7zip()
    {
        wstring err = libs::Ensure7zip(app.root);
        if (err.empty()) return true;
        wstring msg = L::F(L"br.no_7zip", err);
        wstring one = msg;
        for (size_t q; (q = one.find(L"\r\n")) != wstring::npos;) one.replace(q, 2, L" ");
        app.Log(one);
        Info(msg, MB_ICONWARNING);
        return false;
    }

    // ---------- open a WIM file ----------

    void BrowseTab::ChooseWim()
    {
        wstring cur = Trim(GetText(txtWim)), file;
        if (!OpenFile({ { L"WIM (*.wim;*.esd;*.swm)", L"*.wim;*.esd;*.swm" }, { L"*.*", L"*.*" } }, DirName(FullPath(cur)), file)) return;
        SetText(txtWim, file);
        OpenWim(true);
    }

    void BrowseTab::ShowWim(const wstring& wim, int image)
    {
        if (running) return;
        SetText(txtWim, wim);
        OpenWim(true);
        if (image > 0)
            for (size_t n = 0; n < images.size(); n++)
                if (images[n] == image) { ComboSelect(cboImage, (int)n); break; }
    }

    void BrowseTab::SetDefault(const wstring& wim)
    {
        if (Trim(GetText(txtWim)).empty() && FileExists(wim)) SetText(txtWim, wim);
    }

    // the images (wimlib), then the listing of the first one
    void BrowseTab::OpenWim(bool show)
    {
        if (running) return;
        wstring wim = Trim(GetText(txtWim));
        if (wim.empty() || !FileExists(wim))
        {
            if (show) Info(L::F(L"msg.not_found", wim.empty() ? wstring(L"-") : wim), MB_ICONWARNING);
            return;
        }
        std::vector<wl::WimImage> imgs;
        if (!app.Images(wim, show, imgs) || imgs.empty()) return;
        listing = true;                       // no listing from the combo while it is filled
        images.clear();
        ComboClear(cboImage);
        for (const wl::WimImage& w : imgs)
        {
            images.push_back(w.Index);
            ComboAdd(cboImage, L::F(L"bk.image_line", w.Index, w.Name.empty() ? L::T(L"bk.no_name") : w.Name,
                w.TotalBytes >= 0 ? App::Gb(w.TotalBytes) : wstring(L"?")));
        }
        ComboSelect(cboImage, 0);
        listing = false;
        ListImage();
    }

    // ---------- listing (wimlib_iterate_dir_tree) ----------

    // "yyyy-MM-dd HH:mm:ss" in local time; seconds since 1970 (UTC)
    static wstring TimeText(long long unixSeconds)
    {
        if (unixSeconds <= 0) return wstring();
        ULONGLONG ft = (ULONGLONG)unixSeconds * 10000000ULL + 116444736000000000ULL;
        FILETIME f = { (DWORD)ft, (DWORD)(ft >> 32) };
        SYSTEMTIME u, t;
        if (!FileTimeToSystemTime(&f, &u) || !SystemTimeToTzSpecificLocalTime(nullptr, &u, &t)) return wstring();
        wchar_t b[32];
        swprintf_s(b, L"%04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        return b;
    }

    // the attribute letters (as 7-Zip shows them)
    static wstring AttrText(DWORD a)
    {
        wstring s;
        if (a & FILE_ATTRIBUTE_DIRECTORY) s += L'D';
        if (a & FILE_ATTRIBUTE_READONLY) s += L'R';
        if (a & FILE_ATTRIBUTE_HIDDEN) s += L'H';
        if (a & FILE_ATTRIBUTE_SYSTEM) s += L'S';
        if (a & FILE_ATTRIBUTE_ARCHIVE) s += L'A';
        return s;
    }

    // the file cannot be opened for writing for want of rights (a WIM in the
    // root of C:, in Program Files, ...): its edits go to the elevated helper.
    // A file another program holds (sharing violation) is not such a case.
    static bool NeedsAdmin(const wstring& file)
    {
        HANDLE h = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); return false; }
        return GetLastError() == ERROR_ACCESS_DENIED && !(GetFileAttributesW(file.c_str()) & FILE_ATTRIBUTE_READONLY);
    }

    struct ListContext
    {
        BrowseTab::Node* root;
        std::atomic<bool>* abort;
        int count = 0;
    };

    // one file or folder of the image (struct wimlib_dir_entry); the parents come first
    static int __cdecl OnEntry(const void* d, void* ctx)
    {
        ListContext* c = (ListContext*)ctx;
        if (c->abort->load()) return 1;       // stops the iteration (its return value)
        const char* b = (const char*)d;
        const wchar_t* full = *(const wchar_t* const*)(b + wl::DirEntryFullPath);
        DWORD attr = *(const uint32_t*)(b + wl::DirEntryAttributes);
        long long seconds = *(const int64_t*)(b + wl::DirEntryWriteTime);
        long long size = (long long)*(const uint64_t*)(b + wl::DirEntrySize);
        if (!full) return 0;
        wstring rel = full;
        while (!rel.empty() && rel[0] == L'\\') rel.erase(0, 1);
        if (rel.empty()) return 0;
        std::vector<wstring> parts = Split(rel, L'\\');
        BrowseTab::Node* n = c->root;
        for (size_t i = 0; i + 1 < parts.size(); i++) n = n->Child(parts[i]);
        const wstring& name = parts.back();
        if (attr & FILE_ATTRIBUTE_DIRECTORY)
        {
            BrowseTab::Node* dir = n->Child(name);
            dir->Modified = TimeText(seconds);
            dir->Attr = AttrText(attr);
        }
        else
        {
            BrowseTab::Row r;
            r.Name = name;
            r.Path = rel;
            r.Size = size;
            r.Modified = TimeText(seconds);
            r.Attr = AttrText(attr);
            n->Files.push_back(r);
        }
        c->count++;
        return 0;
    }

    static wstring PartsText(const wstring& file, int total, int found)
    {
        return file + L": " + Num(total) + L" parts, " + Num(found) + L" found in its folder (all parts must keep their names).";
    }

    // the chosen image, read in the background (a split WIM: all its parts)
    void BrowseTab::ListImage()
    {
        int sel = ComboIndex(cboImage);
        wstring wim = Trim(GetText(txtWim));
        if (sel < 0 || sel >= (int)images.size() || running || !FileExists(wim)) return;
        int image = images[sel];
        wstring rootName = ComboItem(cboImage, sel);
        listing = true;
        running = true;
        abort = std::make_shared<std::atomic<bool>>(false);
        top.reset();
        current = nullptr;
        rows.clear();
        ListView_SetItemCountEx(list, 0, 0);
        TreeView_DeleteAllItems(tree);
        SetText(lblPath, L"");
        ProgressMarquee(bar, true);
        SetState(L::T(L"br.listing"));
        UpdateButtons();
        HWND target = app.win.hwnd;
        ULONGLONG started = GetTickCount64();
        std::shared_ptr<std::atomic<bool>> stop = abort;
        std::thread([this, wim, image, rootName, target, started, stop]
        {
            std::shared_ptr<Node> root(new Node());
            root->Name = rootName;
            int count = 0, total = 1, found = 1;
            wstring err, why;
            bool admin = false;
            int rc;
            {
                wl::Wim w;
                rc = wl::OpenRead(wim, 0, nullptr, w, total, found);
                if (rc == 0)
                {
                    // the edit commands: not for a split WIM or a read-only file
                    if (total > 1) why = L::T(L"br.why_split");
                    else if (GetFileAttributesW(wim.c_str()) & FILE_ATTRIBUTE_READONLY) why = L::T(L"br.why_readonly");
                    else admin = NeedsAdmin(wim);
                    ListContext c = { root.get(), stop.get() };
                    rc = wl::wimlib_iterate_dir_tree(w.p, image, L"", wl::IterateRecursive | wl::IterateChildren, OnEntry, &c);
                    count = c.count;
                    if (stop->load()) rc = wl::ErrAborted;
                }
            }
            if (rc == wl::ErrPartsMissing) err = PartsText(wim, total, found);
            else if (rc != 0) err = wl::ErrorText(rc);
            ULONGLONG took = GetTickCount64() - started;
            Post(target, [this, root, count, rc, err, wim, image, took, why, admin]
            {
                running = false;
                ListDone(root, count, rc, err, wim, image, took, why, admin);
            });
        }).detach();
    }

    void BrowseTab::ListDone(std::shared_ptr<Node> root, int count, int rc, const wstring& err, const wstring& wim, int image,
        ULONGLONG took, const wstring& why, bool admin)
    {
        wstring again = reopenPath;
        reopenPath.clear();
        if (rc != 0) doneText.clear();
        listing = false;
        ProgressMarquee(bar, false);
        ProgressValue(bar, 0);
        if (rc == wl::ErrAborted)
        {
            SetState(L::T(L"bk.state_cancelled"));
            UpdateButtons();
            return;
        }
        if (rc != 0)
        {
            SetState(L::T(L"br.list_failed_short"));
            wstring msg = L::F(L"br.list_failed", wim, rc, Trim(err).empty() ? wstring(L"-") : Trim(err)), one = msg;
            for (size_t q; (q = one.find(L"\r\n")) != wstring::npos;) one.replace(q, 2, L" ");
            app.Log(one);
            Info(msg, MB_ICONWARNING);
            UpdateButtons();
            return;
        }
        wimFile = wim;
        wimImage = image;
        notWritable = why;
        needsAdmin = admin;
        top = root;
        TreeView_DeleteAllItems(tree);
        HTREEITEM t = AddTreeItem(TVI_ROOT, top.get());
        TreeView_Expand(tree, t, TVE_EXPAND);
        // after an edit or F5: the folder shown before (as far as it is still there)
        ShowFolder(again.empty() ? top.get() : FindNode(again), true);
        // after an edit: what was done (the listing that follows is not news)
        if (!doneText.empty()) SetState(doneText);
        else
        {
            SetState(L::F(L"br.listed", App::Group(count), Fixed(std::max(0.1, took / 1000.0), 1, false)));
            app.Log(L::F(L"br.log_listed", wim, count));
        }
        doneText.clear();
        UpdateButtons();
    }

    // ---------- tree ----------

    HTREEITEM BrowseTab::AddTreeItem(HTREEITEM parent, Node* n)
    {
        TVINSERTSTRUCTW ins = {};
        ins.hParent = parent;
        ins.hInsertAfter = TVI_LAST;
        ins.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_CHILDREN;
        ins.item.pszText = (LPWSTR)n->Name.c_str();
        ins.item.lParam = (LPARAM)n;
        ins.item.iImage = ins.item.iSelectedImage = IconFor(L"", true);
        ins.item.cChildren = n->Dirs.empty() ? 0 : 1;   // filled when expanded
        return TreeView_InsertItem(tree, &ins);
    }

    void BrowseTab::FillTreeItem(HTREEITEM item)
    {
        if (TreeView_GetChild(tree, item)) return;
        TVITEMW it = {};
        it.mask = TVIF_PARAM;
        it.hItem = item;
        TreeView_GetItem(tree, &it);
        Node* n = (Node*)it.lParam;
        if (!n) return;
        std::vector<Node*> dirs;
        for (auto& d : n->Dirs) dirs.push_back(d.get());
        std::sort(dirs.begin(), dirs.end(), [](Node* a, Node* b) { return CompareStringOrdinal(a->Name.c_str(), -1, b->Name.c_str(), -1, TRUE) == CSTR_LESS_THAN; });
        SendMessageW(tree, WM_SETREDRAW, FALSE, 0);
        for (Node* d : dirs) AddTreeItem(item, d);
        SendMessageW(tree, WM_SETREDRAW, TRUE, 0);
    }

    // the tree item of a folder (the path to it is expanded)
    HTREEITEM BrowseTab::FindTreeItem(Node* n)
    {
        std::vector<Node*> chain;
        for (Node* x = n; x; x = x->Parent) chain.insert(chain.begin(), x);
        HTREEITEM t = TreeView_GetRoot(tree);
        if (!t) return nullptr;
        for (size_t i = 1; i < chain.size(); i++)
        {
            FillTreeItem(t);
            TreeView_Expand(tree, t, TVE_EXPAND);
            HTREEITEM next = nullptr;
            for (HTREEITEM c = TreeView_GetChild(tree, t); c; c = TreeView_GetNextSibling(tree, c))
            {
                TVITEMW it = {};
                it.mask = TVIF_PARAM;
                it.hItem = c;
                TreeView_GetItem(tree, &it);
                if ((Node*)it.lParam == chain[i]) { next = c; break; }
            }
            if (!next) return t;
            t = next;
        }
        return t;
    }

    // ---------- list ----------

    void BrowseTab::ShowFolder(Node* n, bool syncTree)
    {
        current = n;
        std::vector<Row> dirs, files(n->Files);
        for (auto& d : n->Dirs)
        {
            Row r;
            r.Name = d->Name;
            r.Path = d->Path;
            r.Modified = d->Modified;
            r.Attr = d->Attr;
            r.Dir = d.get();
            dirs.push_back(r);
        }
        int col = sortColumn;
        bool down = sortDown;
        auto cmp = [col, down](const Row& a, const Row& b)
        {
            int c = 0;
            switch (col)
            {
            case 1: c = a.Size < b.Size ? -1 : a.Size > b.Size ? 1 : 0; break;
            case 2: c = a.Modified.compare(b.Modified); break;
            case 3: c = a.Attr.compare(b.Attr); break;
            }
            if (c == 0) c = CompareStringOrdinal(a.Name.c_str(), -1, b.Name.c_str(), -1, TRUE) - CSTR_EQUAL;
            return down ? c > 0 : c < 0;
        };
        std::stable_sort(dirs.begin(), dirs.end(), cmp);
        std::stable_sort(files.begin(), files.end(), cmp);
        rows = dirs;
        rows.insert(rows.end(), files.begin(), files.end());
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
        ListView_SetItemCountEx(list, (int)rows.size(), 0);
        InvalidateRect(list, nullptr, TRUE);
        if (!rows.empty()) ListView_EnsureVisible(list, 0, FALSE);
        wstring rel = n->Path;
        SetText(lblPath, L"\\" + rel + L"   " + L::F(L"br.folder_count", (long long)dirs.size(), (long long)files.size()));
        if (syncTree)
        {
            HTREEITEM t = FindTreeItem(n);
            if (t && TreeView_GetSelection(tree) != t) TreeView_SelectItem(tree, t);
        }
        UpdateButtons();
    }

    std::vector<BrowseTab::Row> BrowseTab::SelectedRows() const
    {
        std::vector<Row> sel;
        for (int i = ListView_GetNextItem(list, -1, LVNI_SELECTED); i >= 0; i = ListView_GetNextItem(list, i, LVNI_SELECTED))
            if (i < (int)rows.size()) sel.push_back(rows[i]);
        return sel;
    }

    // ---------- icons (of the shell, by extension) ----------

    int BrowseTab::IconFor(const wstring& name, bool dir)
    {
        static std::map<wstring, int> cache;
        wstring ext = dir ? L"<dir>" : Lower(FileExt(name));
        // programs and icons have their own icon in the file: one generic icon for them
        if (!dir && (ext.empty() || ext == L".exe" || ext == L".ico" || ext == L".lnk")) ext = L"<file>";
        auto it = cache.find(ext);
        if (it != cache.end()) return it->second;
        SHFILEINFOW sfi = {};
        wstring probe = dir ? L"folder" : L"file" + (ext[0] == L'<' ? wstring() : ext);
        int idx = -1;
        if (SHGetFileInfoW(probe.c_str(), dir ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &sfi, sizeof sfi,
                SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES))
            idx = sfi.iIcon;
        cache[ext] = idx;
        return idx;
    }

    // ---------- extracting (wimlib_extract_paths) ----------

    struct ExtractContext
    {
        std::atomic<bool>* abort;
        BrowseTab* tab;
        HWND target;
        wstring state;
        int lastPct = -1;
    };

    // wimlib's progress: the percentage to the bar; a cancel stops wimlib
    static int __cdecl OnExtractProgress(int msg, void* info, void* ctx)
    {
        ExtractContext* c = (ExtractContext*)ctx;
        if (c->abort->load()) return wl::StatusAbort;
        if (msg == wl::MsgExtractStreams && info)
        {
            // struct wimlib_progress_info_extract: total_bytes (40), completed_bytes (48)
            uint64_t total = *(const uint64_t*)((const char*)info + 40), done = *(const uint64_t*)((const char*)info + 48);
            int pct = total > 0 ? (int)(done * 100 / total) : 0;
            if (pct != c->lastPct)
            {
                c->lastPct = pct;
                c->tab->ShowProgress(c->target, pct, c->state);
            }
        }
        return wl::StatusContinue;
    }

    void BrowseTab::ShowProgress(HWND target, int pct, const wstring& state)
    {
        Post(target, [this, pct, state] { ProgressMarquee(bar, false); ProgressValue(bar, pct); SetState(state + L"  " + Num(pct) + L"%"); });
    }

    // the paths (inside the image) straight into target (no folders of the
    // image around them), in the background; done(rc, error text) on the GUI thread
    void BrowseTab::RunExtract(const std::vector<wstring>& paths, const wstring& target, const wstring& state,
        std::function<void(int, const wstring&)> done)
    {
        // the target with all folders above it (wimlib makes only the last one;
        // %TEMP%\7z_wimlib_view is deleted at start)
        try { CreateDirs(target); }
        catch (const HelperError& e) { SetState(L::T(L"bk.state_failed")); Info(e.msg, MB_ICONERROR); return; }
        running = true;
        abort = std::make_shared<std::atomic<bool>>(false);
        ProgressMarquee(bar, false);
        ProgressValue(bar, 0);
        SetState(state);
        UpdateButtons();
        HWND win = app.win.hwnd;
        wstring wim = wimFile;
        int image = wimImage;
        std::shared_ptr<std::atomic<bool>> stop = abort;
        std::thread([this, paths, target, state, done, win, wim, image, stop]
        {
            int rc, total = 1, found = 1;
            wstring err, messages = wl::BeginMessages();
            {
                wl::Wim w;
                rc = wl::OpenRead(wim, 0, nullptr, w, total, found);
                if (rc == 0)
                {
                    ExtractContext c = { stop.get(), this, win, state };
                    wl::wimlib_register_progress_function(w.p, OnExtractProgress, &c);
                    std::vector<wstring> wimPaths;
                    for (const wstring& p : paths) wimPaths.push_back(L"\\" + p);
                    std::vector<const wchar_t*> ptrs;
                    for (const wstring& p : wimPaths) ptrs.push_back(p.c_str());
                    // no ACLs: no administrator rights needed; names Windows does not allow are changed
                    rc = wl::wimlib_extract_paths(w.p, image, target.c_str(), ptrs.data(), ptrs.size(),
                        wl::ExtractNoAcls | wl::ExtractNoPreserveDirs | wl::ExtractReplaceInvalidNames);
                    if (stop->load()) rc = wl::ErrAborted;
                }
            }
            if (rc == wl::ErrPartsMissing) err = PartsText(wim, total, found);
            else if (rc != 0 && rc != wl::ErrAborted)
            {
                wstring more = wl::Messages(messages);
                err = (more.empty() ? wstring() : more + L"\r\n\r\n") + wl::ErrorText(rc);
            }
            Post(win, [this, rc, err, done]
            {
                running = false;
                ProgressValue(bar, 0);
                UpdateButtons();
                done(rc, err);
            });
        }).detach();
    }

    // a file or a folder to dest; an existing folder is merged, existing files replaced
    static void MoveMerge(const wstring& src, const wstring& dest)
    {
        if (FileExists(src))
        {
            DeleteFileOrThrow(dest);
            MoveFileOrThrow(src, dest);
            return;
        }
        if (!DirExists(dest)) { MoveFileOrThrow(src, dest); return; }
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(Combine(src, L"*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        std::vector<wstring> names;
        do { wstring n = fd.cFileName; if (n != L"." && n != L"..") names.push_back(n); } while (FindNextFileW(h, &fd));
        FindClose(h);
        for (const wstring& n : names) MoveMerge(Combine(src, n), Combine(dest, n));
    }

    // Explorer is there (not in Windows PE)
    static bool HasExplorer() { return FileExists(Combine(WindowsDir(), L"explorer.exe")); }

    void BrowseTab::ExtractSelected()
    {
        if (running || !top || !FileExists(wimFile)) return;
        std::vector<Row> sel = SelectedRows();
        if (sel.empty()) return;
        wstring dest;
        if (!PickFolder(L::F(L"br.select_dest", (long long)sel.size()), wstring(), dest)) return;
        std::vector<wstring> exist;
        for (const Row& r : sel)
            if (FileExists(Combine(dest, r.Name)) || DirExists(Combine(dest, r.Name))) exist.push_back(r.Name);
        if (!exist.empty())
        {
            wstring names;
            for (size_t i = 0; i < exist.size() && i < 10; i++) names += (i ? L"\r\n" : L"") + exist[i];
            if (exist.size() > 10) names += L"\r\n...";
            if (!YesNo(L::F(L"br.confirm_overwrite", dest, names), MB_ICONWARNING)) return;
        }

        // into a work folder in the target first, then moved up: a cancelled or
        // failed extract leaves nothing half done in the target
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t stamp[32];
        swprintf_s(stamp, L".7z_wimlib_%04d%02d%02d%02d%02d%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        wstring stage = Combine(dest, stamp);
        wstring folderPath = current->Path;
        std::vector<wstring> paths;
        for (const Row& r : sel) paths.push_back(r.Path);
        app.Log(L::F(L"br.log_extract", (long long)sel.size(), folderPath.empty() ? wstring(L"\\") : folderPath, dest));
        ULONGLONG started = GetTickCount64();
        RunExtract(paths, stage, L::T(L"br.extracting"),
            [this, sel, stage, dest, started](int code, const wstring& errors)
        {
            wstring moveErr;
            bool killed = code == wl::ErrAborted;
            // cancelled: nothing half done is moved into the target
            if (!killed)
            {
                try
                {
                    for (const Row& r : sel)
                    {
                        wstring src = Combine(stage, r.Name);
                        if (DirExists(src) || FileExists(src)) MoveMerge(src, Combine(dest, r.Name));
                    }
                }
                catch (const HelperError& e) { moveErr = e.msg; }
            }
            try { if (DirExists(stage)) DeleteTree(stage); }
            catch (const HelperError&) {}
            wstring took = Fixed((GetTickCount64() - started) / 1000.0, 1, false);
            if (code == 0 && moveErr.empty())
            {
                SetState(L::F(L"br.extracted", (long long)sel.size(), took));
                app.Log(L::F(L"br.extracted", (long long)sel.size(), took) + L"  -> " + dest);
                // Windows PE has no Explorer to show the folder: the path only
                if (!HasExplorer()) Info(L::F(L"br.done_path", dest));
                else if (YesNo(L::F(L"br.done_open", dest), MB_ICONINFORMATION))
                    ShellExecuteW(app.win.hwnd, L"open", L"explorer.exe", (L"\"" + dest + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
                return;
            }
            SetState(L::T(killed ? L"bk.state_cancelled" : L"bk.state_failed"));
            wstring msg = !moveErr.empty() ? moveErr : killed ? L::T(L"br.cancelled") : L::F(L"br.extract_failed", code, errors.empty() ? wstring(L"-") : errors);
            wstring one = msg;
            for (size_t q; (q = one.find(L"\r\n")) != wstring::npos;) one.replace(q, 2, L" ");
            app.Log(one);
            Info(msg, killed ? MB_ICONWARNING : MB_ICONERROR);
        });
    }

    // ---------- edit (wimlib_update_image) ----------

    BrowseTab::Node* BrowseTab::FindNode(const wstring& path) const
    {
        Node* n = top.get();
        if (!n || path.empty()) return n;
        for (const wstring& part : Split(path, L'\\'))
        {
            Node* next = nullptr;
            for (auto& d : n->Dirs) if (EqualsI(d->Name, part)) { next = d.get(); break; }
            if (!next) break;
            n = next;
        }
        return n;
    }

    static wstring InImage(const BrowseTab::Node* folder, const wstring& name)
    {
        return folder->Path.empty() ? name : folder->Path + L"\\" + name;
    }

    static bool Has(const BrowseTab::Node* folder, const wstring& name)
    {
        for (auto& d : folder->Dirs) if (EqualsI(d->Name, name)) return true;
        for (auto& f : folder->Files) if (EqualsI(f.Name, name)) return true;
        return false;
    }

    // a file name Windows accepts
    static bool ValidName(const wstring& name)
    {
        if (name.empty() || name == L"." || name == L"..") return false;
        return name.find_first_of(L"\\/:*?\"<>|") == wstring::npos;
    }

    static wstring Names(const std::vector<wstring>& names, size_t max)
    {
        wstring s;
        for (size_t i = 0; i < names.size() && i < max; i++) s += (i ? L"\r\n" : L"") + names[i];
        if (names.size() > max) s += L"\r\n...";
        return s;
    }

    bool BrowseTab::CanEdit()
    {
        if (running || appBusy || !top || !FileExists(wimFile)) return false;
        if (notWritable.empty()) return true;
        Info(L::F(L"br.not_writable", wimFile, notWritable), MB_ICONWARNING);
        return false;
    }

    enum { MenuOpen = 1, MenuExtract, MenuNewFolder, MenuAddFiles, MenuAddFolder, MenuRename, MenuDelete, MenuRefresh };

    // folder: a folder of the tree (its menu acts on it); nullptr: the list
    // (rename / delete act on the selected rows, new things go into the folder shown)
    void BrowseTab::ShowMenu(POINT pt, Node* folder)
    {
        if (!top) return;
        std::vector<Row> sel;
        if (folder)
        {
            if (folder->Parent)
            {
                Row r;
                r.Name = folder->Name;
                r.Path = folder->Path;
                r.Dir = folder;
                sel.push_back(r);
            }
        }
        else sel = SelectedRows();
        Node* into = folder ? folder : current;
        bool edit = !running && !appBusy && notWritable.empty();
        std::vector<MenuItem> m;
        auto add = [&m](int id, const wchar_t* key, bool on, const wchar_t* shortcut = L"")
        {
            MenuItem it;
            it.id = id;
            it.text = L::T(key);
            it.key = shortcut;
            it.enabled = on;
            m.push_back(it);
        };
        auto line = [&m] { m.push_back(MenuItem()); };
        auto note = [&m](const wstring& text) { MenuItem it; it.text = text; it.enabled = false; m.push_back(it); };
        if (!folder)
        {
            add(MenuOpen, L"br.menu_open", !running && sel.size() == 1, L"Enter");
            add(MenuExtract, L"br.menu_extract", !running && !sel.empty());
            line();
        }
        add(MenuNewFolder, L"br.menu_new_folder", edit && into);
        add(MenuAddFiles, L"br.menu_add_files", edit && into);
        add(MenuAddFolder, L"br.menu_add_folder", edit && into);
        line();
        add(MenuRename, L"br.menu_rename", edit && sel.size() == 1, L"F2");
        add(MenuDelete, L"br.menu_delete", edit && !sel.empty(), L"Del");
        line();
        add(MenuRefresh, L"br.menu_refresh", !running && !appBusy, L"F5");
        if (notWritable.empty() && needsAdmin)
        {
            // the edits will ask for administrator rights
            line();
            note(L::T(L"br.menu_admin"));
        }
        if (!notWritable.empty())
        {
            // why the edit commands are grey
            line();
            note(L::F(L"br.not_writable_short", notWritable));
        }
        int cmd = PopupMenu(app.win.hwnd, pt, m);
        switch (cmd)
        {
        case MenuOpen: OpenSelected(); break;
        case MenuExtract: ExtractSelected(); break;
        case MenuNewFolder: NewFolder(into); break;
        case MenuAddFiles: AddFiles(into); break;
        case MenuAddFolder: AddFolder(into); break;
        case MenuRename: if (sel.size() == 1) Rename(sel[0], folder ? folder->Parent : current); break;
        case MenuDelete: Delete(sel); break;
        case MenuRefresh: reopenPath = into ? into->Path : wstring(); ListImage(); break;
        }
    }

    void BrowseTab::NewFolder(Node* folder)
    {
        if (!folder || !CanEdit()) return;
        wstring name = L::T(L"br.new_folder_name");
        if (!InputText(L::T(L"br.menu_new_folder"), L::T(L"br.new_folder_prompt"), name)) return;
        if (!ValidName(name)) { Info(L::F(L"br.bad_name", name), MB_ICONWARNING); return; }
        if (Has(folder, name)) { Info(L::F(L"br.name_exists", name), MB_ICONWARNING); return; }
        // wimlib adds folders from the disk: an empty one in %TEMP%
        wchar_t tmp[MAX_PATH + 1];
        GetTempPathW(MAX_PATH + 1, tmp);
        wstring empty = Combine(tmp, L"7z_wimlib_empty_" + Num((long long)GetTickCount64()));
        if (!CreateDirectoryW(empty.c_str(), nullptr)) { Info(ErrorMessage(GetLastError()), MB_ICONERROR); return; }
        wstring target = InImage(folder, name);
        RunUpdate({ { wl::UpdateAdd, empty, L"\\" + target, wl::AddNoAcls } }, L::F(L"br.op_new_folder", L"\\" + target), empty);
    }

    void BrowseTab::AddFiles(Node* folder)
    {
        if (!folder || !CanEdit()) return;
        std::vector<wstring> files;
        if (!OpenFiles({ { L::T(L"fd.all_files"), L"*.*" } }, wstring(), files) || files.empty()) return;
        std::vector<wstring> exist;
        for (const wstring& f : files) if (Has(folder, FileName(f))) exist.push_back(FileName(f));
        if (!exist.empty() && !YesNo(L::F(L"br.confirm_replace", Names(exist, 10)), MB_ICONWARNING)) return;
        std::vector<UpdateCmd> cmds;
        for (const wstring& f : files) cmds.push_back({ wl::UpdateAdd, f, L"\\" + InImage(folder, FileName(f)), wl::AddNoAcls });
        wstring what = L::F(L"br.op_add", files.size() == 1 ? FileName(files[0]) : L::F(L"br.items", (long long)files.size()),
            L"\\" + folder->Path);
        RunUpdate(cmds, what, wstring());
    }

    void BrowseTab::AddFolder(Node* folder)
    {
        if (!folder || !CanEdit()) return;
        wstring dir;
        if (!PickFolder(L::T(L"br.select_add_folder"), wstring(), dir)) return;
        wstring full = FullPath(dir);
        while (full.size() > 3 && (full.back() == L'\\' || full.back() == L'/')) full.pop_back();
        wstring name = FileName(full);
        // a drive root has no name to give the folder
        if (!ValidName(name)) { Info(L::F(L"br.bad_name", full), MB_ICONWARNING); return; }
        if (Has(folder, name) && !YesNo(L::F(L"br.confirm_merge", name), MB_ICONWARNING)) return;
        RunUpdate({ { wl::UpdateAdd, full, L"\\" + InImage(folder, name), wl::AddNoAcls } },
            L::F(L"br.op_add", name, L"\\" + folder->Path), wstring());
    }

    void BrowseTab::Rename(const Row& r, Node* parent)
    {
        if (!parent || !CanEdit()) return;
        wstring name = r.Name;
        if (!InputText(L::T(L"br.rename_title"), L::T(L"br.rename_prompt"), name) || name == r.Name) return;
        if (!ValidName(name)) { Info(L::F(L"br.bad_name", name), MB_ICONWARNING); return; }
        // another item of that name (the same item in other letters is fine)
        if (!EqualsI(name, r.Name) && Has(parent, name)) { Info(L::F(L"br.name_exists", name), MB_ICONWARNING); return; }
        wstring target = InImage(parent, name);
        RunUpdate({ { wl::UpdateRename, L"\\" + r.Path, L"\\" + target, 0 } }, L::F(L"br.op_rename", L"\\" + r.Path, name), wstring());
    }

    void BrowseTab::Delete(const std::vector<Row>& items)
    {
        if (items.empty() || !CanEdit()) return;
        std::vector<wstring> names;
        for (const Row& r : items) names.push_back(r.Dir ? r.Name + L"\\" : r.Name);
        if (!YesNo(L::F(L"br.confirm_delete", (long long)items.size(), wimImage, Names(names, 10), wimFile), MB_ICONWARNING)) return;
        std::vector<UpdateCmd> cmds;
        for (const Row& r : items) cmds.push_back({ wl::UpdateDelete, L"\\" + r.Path, wstring(), wl::DeleteRecursive });
        RunUpdate(cmds, L::F(L"br.op_delete", items.size() == 1 ? L"\\" + items[0].Path : L::F(L"br.items", (long long)items.size())), wstring());
    }

    // wimlib's progress while the file is written: the percentage; a cancel stops wimlib
    static int __cdecl OnUpdateProgress(int msg, void* info, void* ctx)
    {
        ExtractContext* c = (ExtractContext*)ctx;
        if (c->abort->load()) return wl::StatusAbort;
        if (msg == wl::MsgWriteStreams && info)
        {
            // struct wimlib_progress_info_write_streams: total_bytes (0), completed_bytes (16)
            uint64_t total = *(const uint64_t*)info, done = *(const uint64_t*)((const char*)info + 16);
            int pct = total > 0 ? (int)(done * 100 / total) : 0;
            if (pct != c->lastPct)
            {
                c->lastPct = pct;
                c->tab->ShowProgress(c->target, pct, c->state);
            }
        }
        return wl::StatusContinue;
    }

    // the commands on the image shown, then the file is written in place
    // (wimlib_overwrite appends; the old content stays valid until the end)
    void BrowseTab::RunUpdate(const std::vector<UpdateCmd>& cmds, const wstring& what, const wstring& cleanup)
    {
        if (running || appBusy || !top || cmds.empty()) { if (!cleanup.empty()) RemoveDirectoryW(cleanup.c_str()); return; }
        // checked again now (the file may have moved, or its rights changed)
        if (NeedsAdmin(wimFile)) { RunUpdateElevated(cmds, what, cleanup); return; }
        running = true;
        abort = std::make_shared<std::atomic<bool>>(false);
        reopenPath = current ? current->Path : wstring();
        ProgressMarquee(bar, true);
        SetState(L::T(L"br.updating"));
        UpdateButtons();
        HWND win = app.win.hwnd;
        wstring wim = wimFile, state = L::T(L"br.updating");
        int image = wimImage;
        ULONGLONG started = GetTickCount64();
        std::shared_ptr<std::atomic<bool>> stop = abort;
        std::thread([this, cmds, what, cleanup, win, wim, image, state, started, stop]
        {
            int rc;
            wstring messages = wl::BeginMessages();
            {
                wl::Wim w;
                ExtractContext c = { stop.get(), this, win, state };
                rc = wl::wimlib_open_wim_with_progress(wim.c_str(), wl::OpenWriteAccess, &w.p, OnUpdateProgress, &c);
                if (rc != 0) w.p = nullptr;
                else
                {
                    rc = wl::UpdateImage(w.p, image, cmds);
                    if (rc == 0 && !stop->load()) rc = wl::wimlib_overwrite(w.p, 0, 0);
                    if (stop->load()) rc = wl::ErrAborted;
                }
            }
            if (!cleanup.empty()) RemoveDirectoryW(cleanup.c_str());
            wstring err;
            if (rc != 0 && rc != wl::ErrAborted)
            {
                // wimlib's own lines first (the file and the Windows error), then its code
                wstring more = wl::Messages(messages);
                err = (more.empty() ? wstring() : more + L"\r\n\r\n") + wl::ErrorText(rc);
                if (rc == wl::ErrOpen) err += L"\r\n\r\n" + L::T(L"br.hint_open");
            }
            ULONGLONG took = GetTickCount64() - started;
            Post(win, [this, rc, err, what, took]
            {
                running = false;
                ProgressMarquee(bar, false);
                ProgressValue(bar, 0);
                UpdateDone(rc, err, what, took);
            });
        }).detach();
    }

    // the same commands through the helper with administrator rights (Windows
    // asks); progress, cancel and a failure message as for a backup
    void BrowseTab::RunUpdateElevated(const std::vector<UpdateCmd>& cmds, const wstring& what, const wstring& cleanup)
    {
        Job job;
        job[L"mode"] = L"update";
        job[L"wim"] = wimFile;
        job[L"index"] = Num(wimImage);
        job[L"count"] = Num((long long)cmds.size());
        for (size_t i = 0; i < cmds.size(); i++)
        {
            wstring k = L"c" + Num((long long)i + 1) + L"_";
            const UpdateCmd& u = cmds[i];
            job[k + L"op"] = Num(u.op);
            // a file to add: a path the elevated helper can see (network / SUBST drives)
            job[k + L"a"] = u.op == wl::UpdateAdd ? SessionFreePath(u.a) : u.a;
            job[k + L"b"] = u.b;
            job[k + L"flags"] = Num(u.flags);
        }
        reopenPath = current ? current->Path : wstring();
        app.job = App::JobInfo();
        app.job.mode = L"update";
        app.job.wim = wimFile;
        app.job.image = wimImage;
        ULONGLONG started = GetTickCount64();
        app.onSuccess = [this, what, cleanup, started](const wstring&) -> wstring
        {
            if (!cleanup.empty()) RemoveDirectoryW(cleanup.c_str());
            UpdateDone(0, wstring(), what, GetTickCount64() - started);
            return wstring();                 // no message box, no "after done"
        };
        app.onFailure = [this, cleanup]
        {
            if (!cleanup.empty()) RemoveDirectoryW(cleanup.c_str());
            reopenPath.clear();
            SetState(L::T(L"bk.state_failed"));
            UpdateButtons();
        };
        app.Log(L::F(L"br.log_admin", wimFile));
        SetState(L::T(L"br.updating_admin"));
        app.StartHelper(job);
        if (!app.Busy())
        {
            // not started (administrator rights refused)
            if (!cleanup.empty()) RemoveDirectoryW(cleanup.c_str());
            reopenPath.clear();
            SetState(L::T(L"bk.state_cancelled"));
        }
        UpdateButtons();
    }

    void BrowseTab::UpdateDone(int rc, const wstring& err, const wstring& what, ULONGLONG took)
    {
        if (rc == 0)
        {
            app.Log(L::F(L"br.log_update", wimFile, wimImage, what));
            // the other tabs that show this file read it again
            app.WimWritten(wimFile, false);
            doneText = L::F(L"br.updated", what, Fixed(std::max(0.1, took / 1000.0), 1, false));
            ListImage();
            return;
        }
        wstring msg = rc == wl::ErrAborted ? L::T(L"br.update_cancelled") : L::F(L"br.update_failed", rc, err.empty() ? wstring(L"-") : err);
        wstring one = msg;
        for (size_t q; (q = one.find(L"\r\n")) != wstring::npos;) one.replace(q, 2, L" ");
        app.Log(one);
        SetState(L::T(rc == wl::ErrAborted ? L"bk.state_cancelled" : L"bk.state_failed"));
        reopenPath.clear();
        UpdateButtons();
        Info(msg, rc == wl::ErrAborted ? MB_ICONWARNING : MB_ICONERROR);
    }

    // ---------- open ----------

    static const wchar_t* const Programs[] = { L".exe", L".com", L".bat", L".cmd", L".ps1", L".vbs", L".vbe", L".js", L".jse", L".wsf",
        L".wsh", L".msi", L".msc", L".scr", L".cpl", L".hta", L".lnk", L".reg", L".inf", L".sys", L".dll" };

    // a text file opens in Notepad when no program is registered (Windows PE)
    static const wchar_t* const Texts[] = { L".txt", L".log", L".ini", L".inf", L".xml", L".cfg", L".conf", L".csv", L".json",
        L".md", L".htm", L".html", L".css", L".bat", L".cmd", L".ps1", L".reg", L".vbs", L".js", L".sif", L".unattend", L".manifest" };

    void BrowseTab::OpenSelected()
    {
        if (running) return;
        std::vector<Row> sel = SelectedRows();
        if (sel.size() != 1) return;
        Row r = sel[0];
        if (r.Dir) { ShowFolder(r.Dir, true); return; }
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t stamp[32];
        swprintf_s(stamp, L"%02d%02d%02d-%03d", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
        wstring dir = Combine(ViewRoot(), stamp);
        wstring file = Combine(dir, r.Name);
        RunExtract({ r.Path }, dir, L::F(L"br.opening", r.Name),
            [this, file, r](int code, const wstring& errors)
        {
            if (code == wl::ErrAborted) { SetState(L::T(L"bk.state_cancelled")); return; }
            if (code != 0 || !FileExists(file))
            {
                SetState(L::T(L"bk.state_failed"));
                Info(L::F(L"br.extract_failed", code, errors.empty() ? wstring(L"-") : errors), MB_ICONERROR);
                return;
            }
            SetState(r.Name);
            // a program is not started from here: its folder is shown
            wstring ext = Lower(FileExt(file));
            bool explorer = HasExplorer();
            for (const wchar_t* p : Programs)
                if (ext == p)
                {
                    if (explorer) ShellExecuteW(app.win.hwnd, L"open", L"explorer.exe", (L"/select,\"" + file + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
                    else Info(L::F(L"br.program_path", file));
                    return;
                }
            for (const wchar_t* p : Texts)
                if (ext == p && !explorer)
                {
                    // Windows PE: no associations to rely on
                    if ((INT_PTR)ShellExecuteW(app.win.hwnd, L"open", Combine(SystemDir(), L"notepad.exe").c_str(),
                        (L"\"" + file + L"\"").c_str(), nullptr, SW_SHOWNORMAL) <= 32)
                        Info(L::F(L"br.no_program", file), MB_ICONWARNING);
                    return;
                }
            if ((INT_PTR)ShellExecuteW(app.win.hwnd, nullptr, file.c_str(), nullptr, nullptr, SW_SHOWNORMAL) > 32) return;
            bool text = false;
            for (const wchar_t* p : Texts) if (ext == p) text = true;
            if (text && (INT_PTR)ShellExecuteW(app.win.hwnd, L"open", Combine(SystemDir(), L"notepad.exe").c_str(),
                (L"\"" + file + L"\"").c_str(), nullptr, SW_SHOWNORMAL) > 32) return;
            // no program for this type: Windows asks for one; without Explorer the path is shown
            if (explorer) ShellExecuteW(app.win.hwnd, L"open", L"rundll32.exe", (L"shell32.dll,OpenAs_RunDLL " + file).c_str(), nullptr, SW_SHOWNORMAL);
            else Info(L::F(L"br.no_program", file), MB_ICONWARNING);
        });
    }

    void BrowseTab::OpenIn7zFm()
    {
        wstring wim = Trim(GetText(txtWim));
        if (wim.empty() || !FileExists(wim)) { Info(L::F(L"msg.not_found", wim.empty() ? wstring(L"-") : wim), MB_ICONWARNING); return; }
        if (!Ensure7zip()) return;
        wstring fm = Combine(app.ToolDir(), L"7zFM.exe");
        if (!FileExists(fm)) { Info(L::F(L"msg.not_found", fm), MB_ICONWARNING); return; }
        if ((INT_PTR)ShellExecuteW(app.win.hwnd, L"open", fm.c_str(), (L"\"" + wim + L"\"").c_str(), nullptr, SW_SHOWNORMAL) <= 32)
            Info(ErrorMessage(GetLastError()), MB_ICONWARNING);
    }
}
