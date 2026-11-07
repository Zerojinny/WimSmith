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
        txtWim = Edit(page);
        btnBrowse = Button(page, L::T(L"qbt.btn_browse"));
        btnRead = Button(page, L::T(L"br.btn_read"));
        btnFm = Button(page, L::T(L"br.btn_7zfm"));
        grid.Add(L::T(L"bk.row_wim"), { Field(txtWim, 420), Btn(btnBrowse, 100), Btn(btnRead, 100), Btn(btnFm, 130) });
        cboImage = Combo(page);
        btnUp = Button(page, L::T(L"br.btn_up"));
        lblPath = Label(page, L"", FontUI, color::Dim, SS_LEFT | SS_ENDELLIPSIS);
        Item path = Txt(lblPath, 6, 0);
        path.fill = true;
        grid.Add(L::T(L"bk.row_image"), { Field(cboImage, 420), Btn(btnUp, 80), path });

        // the icons of the shell (the system image list, shared)
        SHFILEINFOW sfi = {};
        HIMAGELIST icons = (HIMAGELIST)SHGetFileInfoW(L"folder", FILE_ATTRIBUTE_DIRECTORY, &sfi, sizeof sfi,
            SHGFI_SYSICONINDEX | SHGFI_SMALLICON | SHGFI_USEFILEATTRIBUTES);
        tree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | TVS_HASBUTTONS | TVS_LINESATROOT
            | TVS_SHOWSELALWAYS, 0, 0, 10, 10, page, nullptr, Inst, nullptr);
        SendMessageW(tree, WM_SETFONT, (WPARAM)FontUI, FALSE);
        SetWindowTheme(tree, L"DarkMode_Explorer", nullptr);
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
                if (k->wVKey == VK_RETURN) OpenSelected();
                else if (k->wVKey == VK_BACK && current && current->Parent) ShowFolder(current->Parent, true);
                return true;
            }
            }
            return false;
        });
        OnChange(list, [this] { UpdateButtons(); });
        OnListDoubleClick(list, [this] { OpenSelected(); });
        OnClick(btnExtract, [this] { ExtractSelected(); });
        OnClick(btnOpen, [this] { OpenSelected(); });
        OnClick(btnCancel, [this] { Cancel(); });
        // Enter in the file field reads it
        SetWindowSubclass(txtWim, [](HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR self) -> LRESULT
        {
            if (m == WM_GETDLGCODE && wp == VK_RETURN) return DLGC_WANTALLKEYS;
            if (m == WM_KEYDOWN && wp == VK_RETURN) { ((BrowseTab*)self)->OpenWim(true); return 0; }
            if (m == WM_CHAR && wp == L'\r') return 0;
            return DefSubclassProc(h, m, wp, lp);
        }, 4, (DWORD_PTR)this);
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
        x += S(8);
        Move(bar, x, by + S(15), S(200), S(4)); x += S(200) + S(8);
        Move(btnCancel, x, by + S(2), S(80), S(28)); x += S(80) + S(8);
        Move(lblState, x + S(6), by + S(8), std::max(0, w - x - S(6)), TextHeight(FontUI) + S(2));
    }

    // ---------- state ----------

    void BrowseTab::Cancel()
    {
        if (!proc) return;
        cancelRequested = true;
        TerminateProcess(proc, 1);
    }

    void BrowseTab::UpdateButtons()
    {
        bool running = proc != nullptr;
        bool any = ListView_GetSelectedCount(list) > 0;
        Enable(btnRead, !running);
        Enable(txtWim, !running);
        Enable(cboImage, !running && ComboCount(cboImage) > 1);
        Enable(btnUp, !running && current && current->Parent);
        Enable(btnExtract, !running && top && any);
        Enable(btnOpen, !running && top && any);
        Enable(btnCancel, running);
        Enable(tree, !running || !listing);
        Enable(list, !running || !listing);
    }

    wstring BrowseTab::SevenZip() const { return Combine(app.ToolDir(), L"7z.exe"); }

    bool BrowseTab::Ensure7zip()
    {
        // lib\7z.exe: downloaded when missing
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
        if (proc) return;
        SetText(txtWim, wim);
        OpenWim(true);
        if (image > 0)
            for (size_t n = 0; n < prefixes.size(); n++)
                if (prefixes[n] == Num(image)) { ComboSelect(cboImage, (int)n); break; }
    }

    void BrowseTab::SetDefault(const wstring& wim)
    {
        if (Trim(GetText(txtWim)).empty() && FileExists(wim)) SetText(txtWim, wim);
    }

    // the images (wimlib), then the listing of the first one
    void BrowseTab::OpenWim(bool show)
    {
        if (proc) return;
        wstring wim = Trim(GetText(txtWim));
        if (wim.empty() || !FileExists(wim))
        {
            if (show) Info(L::F(L"msg.not_found", wim.empty() ? wstring(L"-") : wim), MB_ICONWARNING);
            return;
        }
        if (!Ensure7zip()) return;
        std::vector<wl::WimImage> images;
        bool known = app.Images(wim, false, images);
        listing = true;                       // no listing from the combo while it is filled
        prefixes.clear();
        ComboClear(cboImage);
        if (known && images.size() > 1)
        {
            for (const wl::WimImage& w : images)
            {
                prefixes.push_back(Num(w.Index));
                ComboAdd(cboImage, L::F(L"bk.image_line", w.Index, w.Name.empty() ? L::T(L"bk.no_name") : w.Name,
                    w.TotalBytes >= 0 ? App::Gb(w.TotalBytes) : wstring(L"?")));
            }
        }
        else
        {
            // one image (or wimlib could not tell): the whole file
            wstring name = known && images.size() == 1 && !images[0].Name.empty() ? images[0].Name : FileName(wim);
            prefixes.push_back(wstring());
            ComboAdd(cboImage, known && images.size() == 1 ? L"1: " + name : name);
        }
        ComboSelect(cboImage, 0);
        listing = false;
        ListImage();
    }

    // starts 7z.exe (no window); out: its stdout (stderr goes to the same pipe)
    bool BrowseTab::Start7z(const wstring& args, HANDLE& out)
    {
        SECURITY_ATTRIBUTES sa = { sizeof sa, nullptr, TRUE };
        HANDLE outR, outW;
        if (!CreatePipe(&outR, &outW, &sa, 0)) return false;
        SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
        HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
        STARTUPINFOW si = {};
        si.cb = sizeof si;
        si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
        si.hStdInput = nul;
        si.hStdOutput = outW;
        si.hStdError = outW;
        PROCESS_INFORMATION pi = {};
        wstring exe = SevenZip();
        wstring cmd = L"\"" + exe + L"\" " + args;
        std::vector<wchar_t> line(cmd.begin(), cmd.end());
        line.push_back(0);
        wstring dir = app.ToolDir();
        BOOL ok = CreateProcessW(exe.c_str(), line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, dir.c_str(), &si, &pi);
        DWORD e = GetLastError();
        CloseHandle(outW);
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        if (!ok) { CloseHandle(outR); SetLastError(e); return false; }
        CloseHandle(pi.hThread);
        proc = pi.hProcess;
        out = outR;
        return true;
    }

    // "2024-04-01 16:39:55 D.... <size 12> <packed 12>  name"
    static bool AddLine(BrowseTab::Node* root, const wstring& prefix, const wstring& line)
    {
        if (line.size() < 54 || line[19] != L' ' || line[25] != L' ' || line[38] != L' ' || line[51] != L' ' || line[52] != L' ') return false;
        wstring path = line.substr(53);
        // alternate data streams (file:stream) are not shown
        if (path.empty() || path.find(L':') != wstring::npos) return false;
        wstring rel = path;
        if (!prefix.empty())
        {
            if (!StartsWithI(path, prefix + L"\\")) return false;
            rel = path.substr(prefix.size() + 1);
        }
        if (rel.empty()) return false;
        wstring flags = line.substr(20, 5), attr;
        for (wchar_t c : flags) if (c != L'.') attr += c;
        bool dir = flags[0] == L'D';
        wstring modified = Trim(line.substr(0, 19));
        std::vector<wstring> parts = Split(rel, L'\\');
        BrowseTab::Node* n = root;
        for (size_t i = 0; i + 1 < parts.size(); i++) n = n->Child(parts[i]);
        const wstring& name = parts.back();
        if (dir)
        {
            BrowseTab::Node* d = n->Child(name);
            d->Modified = modified;
            d->Attr = attr;
        }
        else
        {
            BrowseTab::Row r;
            r.Name = name;
            r.Path = path;
            r.Size = ParseLong(line.substr(26, 12), -1);
            r.Modified = modified;
            r.Attr = attr;
            n->Files.push_back(r);
        }
        return true;
    }

    // 7z l of the chosen image, read in the background
    void BrowseTab::ListImage()
    {
        int sel = ComboIndex(cboImage);
        wstring wim = Trim(GetText(txtWim));
        if (sel < 0 || sel >= (int)prefixes.size() || proc || !FileExists(wim)) return;
        wstring prefix = prefixes[sel], rootName = ComboItem(cboImage, sel);
        wstring args = L"l -ba -sccUTF-8 \"" + wim + L"\"" + (prefix.empty() ? wstring() : L" \"" + prefix + L"\\*\"");
        listing = true;
        top.reset();
        current = nullptr;
        rows.clear();
        ListView_SetItemCountEx(list, 0, 0);
        TreeView_DeleteAllItems(tree);
        SetText(lblPath, L"");
        ProgressMarquee(bar, true);
        SetState(L::T(L"br.listing"));
        HANDLE out;
        if (!Start7z(args, out))
        {
            listing = false;
            ProgressMarquee(bar, false);
            SetState(ErrorMessage(GetLastError()));
            return;
        }
        UpdateButtons();
        HANDLE p = proc;
        HWND target = app.win.hwnd;
        ULONGLONG started = GetTickCount64();
        std::thread([this, p, out, prefix, rootName, wim, target, started]
        {
            std::shared_ptr<Node> root(new Node());
            root->Name = rootName;
            root->Path = prefix;
            int count = 0;
            std::string pending, errors;
            char buf[65536];
            DWORD got;
            auto take = [&](const std::string& raw)
            {
                std::string l = raw;
                if (!l.empty() && l.back() == '\r') l.pop_back();
                if (AddLine(root.get(), prefix, FromUtf8(l))) count++;
                else if (!l.empty() && errors.size() < 4000) errors += l + "\n";
            };
            while (ReadFile(out, buf, sizeof buf, &got, nullptr) && got > 0)
            {
                pending.append(buf, got);
                size_t nl;
                while ((nl = pending.find('\n')) != std::string::npos) { take(pending.substr(0, nl)); pending.erase(0, nl + 1); }
            }
            if (!pending.empty()) take(pending);
            CloseHandle(out);
            WaitForSingleObject(p, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(p, &code);
            wstring err = FromUtf8(errors);
            ULONGLONG took = GetTickCount64() - started;
            Post(target, [this, p, root, count, code, err, wim, took]
            {
                if (proc == p) proc = nullptr;
                CloseHandle(p);
                ListDone(root, count, (int)code, err, wim, took);
            });
        }).detach();
    }

    void BrowseTab::ListDone(std::shared_ptr<Node> root, int count, int code, const wstring& err, const wstring& wim, ULONGLONG took)
    {
        listing = false;
        ProgressMarquee(bar, false);
        ProgressValue(bar, 0);
        if (code != 0 && count == 0)
        {
            SetState(L::T(L"br.list_failed_short"));
            wstring msg = L::F(L"br.list_failed", wim, code, Trim(err).empty() ? wstring(L"-") : Trim(err)), one = msg;
            for (size_t q; (q = one.find(L"\r\n")) != wstring::npos;) one.replace(q, 2, L" ");
            app.Log(one);
            Info(msg, MB_ICONWARNING);
            UpdateButtons();
            return;
        }
        wimFile = wim;
        top = root;
        TreeView_DeleteAllItems(tree);
        HTREEITEM t = AddTreeItem(TVI_ROOT, top.get());
        TreeView_Expand(tree, t, TVE_EXPAND);
        ShowFolder(top.get(), true);
        SetState(L::F(L"br.listed", App::Group(count), Fixed(std::max(0.1, took / 1000.0), 1, false)));
        app.Log(L::F(L"br.log_listed", wim, count));
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
        int sel = ComboIndex(cboImage);
        wstring prefix = sel >= 0 && sel < (int)prefixes.size() ? prefixes[sel] : wstring();
        if (!prefix.empty()) rel = rel.size() > prefix.size() ? rel.substr(prefix.size() + 1) : wstring();
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

    // ---------- 7z x / e ----------

    // with a list file; progress (-bsp1) to the bar; done(code, error text) on the GUI thread
    void BrowseTab::RunExtract(const wstring& args, const std::vector<wstring>& paths, const wstring& state, std::function<void(int, const wstring&)> done)
    {
        wchar_t tmp[MAX_PATH + 1];
        GetTempPathW(MAX_PATH + 1, tmp);
        wstring listFile = Combine(tmp, L"7z_wimlib_7z_" + Num((long long)GetTickCount64()) + L".txt");
        wstring text;
        for (const wstring& p : paths) text += p + L"\r\n";
        if (!WriteBytes(listFile, ToUtf8(text))) { Info(ErrorMessage(GetLastError()), MB_ICONERROR); return; }
        cancelRequested = false;
        HANDLE out;
        if (!Start7z(args + L" -y -bso0 -bsp1 -bse1 -sccUTF-8 -scsUTF-8 \"@" + listFile + L"\"", out))
        {
            DWORD e = GetLastError();
            DeleteFileW(listFile.c_str());
            Info(ErrorMessage(e), MB_ICONERROR);
            return;
        }
        HANDLE p = proc;
        ProgressMarquee(bar, false);
        ProgressValue(bar, 0);
        SetState(state);
        UpdateButtons();
        HWND target = app.win.hwnd;
        std::thread([this, p, out, listFile, state, done, target]
        {
            std::string all;
            char buf[4096];
            DWORD got;
            int lastPct = -1;
            while (ReadFile(out, buf, sizeof buf, &got, nullptr) && got > 0)
            {
                std::string chunk(buf, got);
                all += chunk;
                // the last "nn%" of the chunk
                int pct = -1;
                for (size_t i = chunk.find('%'); i != std::string::npos; i = chunk.find('%', i + 1))
                {
                    size_t a = i;
                    while (a > 0 && i - a < 3 && isdigit((unsigned char)chunk[a - 1])) a--;
                    if (a < i) pct = atoi(chunk.substr(a, i - a).c_str());
                }
                if (pct >= 0 && pct <= 100 && pct != lastPct)
                {
                    lastPct = pct;
                    Post(target, [this, pct, state] { ProgressValue(bar, pct); SetState(state + L"  " + Num(pct) + L"%"); });
                }
            }
            CloseHandle(out);
            WaitForSingleObject(p, INFINITE);
            DWORD code = 1;
            GetExitCodeProcess(p, &code);
            DeleteFileW(listFile.c_str());
            // the error lines of 7-Zip (stderr is sent to stdout with -bse1)
            wstring errors;
            for (char& c : all) if (c == '\b' || c == '\r') c = '\n';
            for (const wstring& l : SplitLines(FromUtf8(all)))
            {
                wstring t = Trim(l);
                size_t d = 0;
                while (d < t.size() && d < 3 && iswdigit(t[d])) d++;
                bool progress = d > 0 && d < t.size() && t[d] == L'%';
                if (!t.empty() && !progress) errors += (errors.empty() ? L"" : L"\r\n") + t;
            }
            Post(target, [this, p, code, errors, done]
            {
                if (proc == p) proc = nullptr;
                CloseHandle(p);
                ProgressValue(bar, 0);
                UpdateButtons();
                done((int)code, errors);
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
        if (proc || !top || !FileExists(wimFile)) return;
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

        // 7z keeps the path inside the image: into a work folder in the target, then moved up
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
        RunExtract(L"x \"" + wimFile + L"\" -o\"" + stage + L"\"", paths, L::T(L"br.extracting"),
            [this, sel, stage, folderPath, dest, started](int code, const wstring& errors)
        {
            wstring moveErr;
            bool killed = cancelRequested;
            // cancelled: nothing half done is moved into the target
            if (!killed)
            {
                try
                {
                    wstring from = folderPath.empty() ? stage : Combine(stage, folderPath);
                    for (const Row& r : sel)
                    {
                        wstring src = Combine(from, r.Name);
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
            Info(msg, code == 1 ? MB_ICONWARNING : MB_ICONERROR);
        });
    }

    // ---------- open ----------

    static const wchar_t* const Programs[] = { L".exe", L".com", L".bat", L".cmd", L".ps1", L".vbs", L".vbe", L".js", L".jse", L".wsf",
        L".wsh", L".msi", L".msc", L".scr", L".cpl", L".hta", L".lnk", L".reg", L".inf", L".sys", L".dll" };

    // a text file opens in Notepad when no program is registered (Windows PE)
    static const wchar_t* const Texts[] = { L".txt", L".log", L".ini", L".inf", L".xml", L".cfg", L".conf", L".csv", L".json",
        L".md", L".htm", L".html", L".css", L".bat", L".cmd", L".ps1", L".reg", L".vbs", L".js", L".sif", L".unattend", L".manifest" };

    void BrowseTab::OpenSelected()
    {
        if (proc) return;
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
        RunExtract(L"e \"" + wimFile + L"\" -o\"" + dir + L"\"", { r.Path }, L::F(L"br.opening", r.Name),
            [this, file, r](int code, const wstring& errors)
        {
            if (cancelRequested) { SetState(L::T(L"bk.state_cancelled")); return; }
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
