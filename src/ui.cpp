// ============================================================
//  7z_wimlib - ui.cpp
//  The window toolkit (see ui.h).
// ============================================================
#include "ui.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <shobjidl.h>
#include <algorithm>
#include <map>

namespace ui
{
    float Scale = 1.0f;
    HINSTANCE Inst;
    HWND Main;
    HFONT FontUI, FontBold, FontMono;

    static const wchar_t* const PanelClass = L"SevenWimPanel";
    static const wchar_t* const TopClass = L"SevenWimWindow";
    static const wchar_t* const CheckClass = L"SevenWimCheck";
    static const wchar_t* const TabsClass = L"SevenWimTabs";
    static const wchar_t* const ProgressClass = L"SevenWimProgress";
    static const UINT WM_APP_POST = WM_APP + 1;

    // ---------- state of the windows ----------

    struct Handlers
    {
        std::vector<Fn> click, change;
        std::function<LRESULT(NMHDR*)> notify;
    };
    static std::map<HWND, Handlers> handlers;
    static std::map<HWND, COLORREF> foreColors, backColors;
    static std::map<HWND, std::function<void(int, int)>> layouts;
    static std::map<HWND, std::function<void(HDC, const RECT&)>> painters;
    static std::map<HWND, SIZE> minSizes;
    static std::map<HWND, std::function<bool()>> closers;
    static std::map<HWND, Fn> destroyers;
    static std::map<std::pair<HWND, UINT_PTR>, Fn> timers;
    static std::map<HWND, int> buttonKinds;   // 1 = accent, 2 = danger
    static std::map<HWND, HWND> upDowns;      // number edit -> its arrows
    static std::map<COLORREF, HBRUSH> brushes;
    static HWND hovered;
    static std::map<HWND, std::vector<bool>> boldRows;      // list -> rows in bold
    static std::map<HWND, Fn> listDouble;
    static std::map<HWND, std::function<bool(NMHDR*, LRESULT&)>> listExtra;
    static std::map<HWND, std::pair<Fn, Fn>> keyHandlers;   // Enter / Esc of a window
    static std::map<HWND, int> modalResults;

    HBRUSH Brush(COLORREF c)
    {
        auto it = brushes.find(c);
        if (it != brushes.end()) return it->second;
        HBRUSH b = CreateSolidBrush(c);
        brushes[c] = b;
        return b;
    }

    COLORREF Lighter(COLORREF c, float a)
    {
        auto up = [a](int v) { return (int)(v + (255 - v) * a); };
        return RGB(up(GetRValue(c)), up(GetGValue(c)), up(GetBValue(c)));
    }

    static void Fire(HWND h, bool click)
    {
        auto it = handlers.find(h);
        if (it == handlers.end()) return;
        std::vector<Fn> list = click ? it->second.click : it->second.change;   // a copy: a callback may add others
        for (Fn& f : list) f();
    }

    void OnClick(HWND h, Fn f) { handlers[h].click.push_back(f); }
    void OnChange(HWND h, Fn f) { handlers[h].change.push_back(f); }
    void OnNotify(HWND h, std::function<LRESULT(NMHDR*)> f) { handlers[h].notify = f; }
    void FireChange(HWND h) { Fire(h, false); }

    void Reset()
    {
        handlers.clear(); foreColors.clear(); backColors.clear(); layouts.clear(); painters.clear();
        minSizes.clear(); closers.clear(); destroyers.clear(); timers.clear(); buttonKinds.clear();
        upDowns.clear(); boldRows.clear(); listDouble.clear(); listExtra.clear(); keyHandlers.clear(); modalResults.clear(); hovered = nullptr;
    }

    COLORREF BackOf(HWND h)
    {
        for (HWND w = h; w; w = GetParent(w))
        {
            auto it = backColors.find(w);
            if (it != backColors.end()) return it->second;
        }
        return color::Back;
    }

    static wstring ClassOf(HWND h)
    {
        wchar_t c[64] = { 0 };
        GetClassNameW(h, c, 64);
        return c;
    }

    static HFONT FontOf(HWND h)
    {
        HFONT f = (HFONT)SendMessageW(h, WM_GETFONT, 0, 0);
        return f ? f : FontUI;
    }

    // ---------- owner-drawn buttons ----------

    static void DrawButton(const DRAWITEMSTRUCT* d)
    {
        HWND h = d->hwndItem;
        auto k = buttonKinds.find(h);
        int kind = k == buttonKinds.end() ? 0 : k->second;
        bool enabled = !(d->itemState & ODS_DISABLED), down = (d->itemState & ODS_SELECTED) != 0, hover = hovered == h;
        COLORREF fill, border, fore;
        if (!enabled) { fill = kind ? color::Button : color::Panel; border = color::Line; fore = color::Dim; }
        else if (kind)
        {
            COLORREF base = kind == 2 ? color::Danger : color::Accent;
            fill = down ? Lighter(base, 0.3f) : hover ? Lighter(base, 0.15f) : base;
            border = base;
            fore = kind == 2 ? color::DangerText : color::AccentText;
        }
        else
        {
            fill = down ? color::Border : hover ? color::ButtonHover : color::Button;
            border = (d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT) ? color::Dim : color::Border;
            fore = color::Text;
        }
        RECT r = d->rcItem;
        FillRect(d->hDC, &r, Brush(fill));
        FrameRect(d->hDC, &r, Brush(border));
        wchar_t text[256] = { 0 };
        GetWindowTextW(h, text, 256);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, fore);
        HGDIOBJ old = SelectObject(d->hDC, FontOf(h));
        InflateRect(&r, -S(4), 0);
        DrawTextW(d->hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(d->hDC, old);
    }

    static void TrackHover(HWND h, UINT m)
    {
        if (m == WM_MOUSEMOVE && hovered != h)
        {
            HWND before = hovered;
            hovered = h;
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
            InvalidateRect(h, nullptr, FALSE);
            if (before && IsWindow(before)) InvalidateRect(before, nullptr, FALSE);
        }
        else if (m == WM_MOUSELEAVE && hovered == h)
        {
            hovered = nullptr;
            InvalidateRect(h, nullptr, FALSE);
        }
    }

    static LRESULT CALLBACK ButtonSub(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
    {
        if (m == WM_MOUSEMOVE || m == WM_MOUSELEAVE) TrackHover(h, m);
        return DefSubclassProc(h, m, wp, lp);
    }

    // ---------- edits: a 1 px frame in the border color ----------

    static LRESULT CALLBACK EditSub(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
    {
        if (m == WM_NCPAINT)
        {
            LRESULT r = DefSubclassProc(h, m, wp, lp);
            HDC dc = GetWindowDC(h);
            RECT rc;
            GetWindowRect(h, &rc);
            OffsetRect(&rc, -rc.left, -rc.top);
            FrameRect(dc, &rc, Brush(GetFocus() == h ? color::Dim : color::Border));
            ReleaseDC(h, dc);
            return r;
        }
        if (m == WM_SETFOCUS || m == WM_KILLFOCUS)
        {
            LRESULT r = DefSubclassProc(h, m, wp, lp);
            SetWindowPos(h, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
            return r;
        }
        return DefSubclassProc(h, m, wp, lp);
    }

    // a combo box in which text can be typed: its edit part reports its colors
    // to the combo box. Disabled, Windows would draw it white with the text
    // still selected; here: the dark field, the text dim, nothing selected.
    static LRESULT CALLBACK EditComboSub(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
    {
        switch (m)
        {
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        {
            HDC dc = (HDC)wp;
            SetTextColor(dc, m == WM_CTLCOLOREDIT && IsWindowEnabled(h) ? color::Text : color::Dim);
            SetBkColor(dc, color::Panel);
            return (LRESULT)Brush(color::Panel);
        }
        case WM_ENABLE:
        case WM_SETTEXT:
        {
            LRESULT r = DefSubclassProc(h, m, wp, lp);
            if (!IsWindowEnabled(h)) SendMessageW(h, CB_SETEDITSEL, 0, MAKELPARAM(0xFFFF, 0));
            return r;
        }
        }
        return DefSubclassProc(h, m, wp, lp);
    }

    // links and clickable labels: the hand cursor
    static LRESULT CALLBACK LinkSub(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
    {
        if (m == WM_SETCURSOR) { SetCursor(LoadCursor(nullptr, IDC_HAND)); return TRUE; }
        return DefSubclassProc(h, m, wp, lp);
    }

    // ---------- routing of the container windows ----------

    static bool Route(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT& r)
    {
        switch (msg)
        {
        case WM_COMMAND:
        {
            HWND c = (HWND)lp;
            if (!c)
            {
                // Enter / Esc (IsDialogMessage): IDOK / IDCANCEL to the window
                auto k = keyHandlers.find(hwnd);
                int id = LOWORD(wp);
                if (k == keyHandlers.end() || (id != IDOK && id != IDCANCEL)) return false;
                Fn f = id == IDOK ? k->second.first : k->second.second;
                if (f) f();
                r = 0;
                return true;
            }
            int code = HIWORD(wp);
            if (code == BN_CLICKED || code == BN_DOUBLECLICKED || code == STN_CLICKED) Fire(c, true);
            else if (code == EN_CHANGE || code == CBN_SELCHANGE || code == CBN_EDITCHANGE)
            {
                // a number edit reports for itself; a combo's edit for the combo
                Fire(c, false);
            }
            r = 0;
            return true;
        }
        case WM_NOTIFY:
        {
            NMHDR* n = (NMHDR*)lp;
            auto it = handlers.find(n->hwndFrom);
            if (it != handlers.end() && it->second.notify) { r = it->second.notify(n); return true; }
            return false;
        }
        case WM_DRAWITEM:
        {
            const DRAWITEMSTRUCT* d = (const DRAWITEMSTRUCT*)lp;
            if (d->CtlType != ODT_BUTTON) return false;
            DrawButton(d);
            r = TRUE;
            return true;
        }
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
        {
            HDC dc = (HDC)wp;
            SetTextColor(dc, color::Text);
            SetBkColor(dc, color::Panel);
            r = (LRESULT)Brush(color::Panel);
            return true;
        }
        case WM_CTLCOLORSTATIC:
        {
            HDC dc = (HDC)wp;
            HWND c = (HWND)lp;
            wstring cls = ClassOf(c);
            bool field = _wcsicmp(cls.c_str(), L"Edit") == 0;
            COLORREF back = field ? color::Panel : BackOf(GetParent(c));
            COLORREF fore = color::Text;
            if (!IsWindowEnabled(c)) fore = color::Dim;
            else if (!field)
            {
                auto it = foreColors.find(c);
                if (it != foreColors.end()) fore = it->second;
            }
            SetTextColor(dc, fore);
            SetBkColor(dc, back);
            r = (LRESULT)Brush(back);
            return true;
        }
        case WM_CTLCOLORBTN:
            r = (LRESULT)Brush(BackOf(hwnd));
            return true;
        case WM_ERASEBKGND:
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, Brush(BackOf(hwnd)));
            r = 1;
            return true;
        }
        }
        return false;
    }

    static LRESULT CALLBACK WindowProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
    {
        LRESULT r = 0;
        if (Route(h, m, wp, lp, r)) return r;
        switch (m)
        {
        case WM_SIZE:
        {
            auto it = layouts.find(h);
            if (it != layouts.end() && wp != SIZE_MINIMIZED)
            {
                auto f = it->second;
                f(LOWORD(lp), HIWORD(lp));
            }
            return 0;
        }
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            auto it = painters.find(h);
            if (it != painters.end())
            {
                RECT rc;
                GetClientRect(h, &rc);
                auto f = it->second;
                f(dc, rc);
            }
            EndPaint(h, &ps);
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            auto it = minSizes.find(h);
            if (it != minSizes.end())
            {
                RECT rc = { 0, 0, S(it->second.cx), S(it->second.cy) };
                AdjustWindowRectEx(&rc, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
                MINMAXINFO* mm = (MINMAXINFO*)lp;
                mm->ptMinTrackSize.x = rc.right - rc.left;
                mm->ptMinTrackSize.y = rc.bottom - rc.top;
            }
            return 0;
        }
        case WM_CLOSE:
        {
            auto it = closers.find(h);
            if (it != closers.end())
            {
                auto f = it->second;
                if (!f()) return 0;
            }
            DestroyWindow(h);
            return 0;
        }
        case WM_DESTROY:
        {
            auto it = destroyers.find(h);
            if (it != destroyers.end())
            {
                Fn f = it->second;
                destroyers.erase(it);
                f();
            }
            return 0;
        }
        case WM_TIMER:
        {
            auto it = timers.find(std::make_pair(h, (UINT_PTR)wp));
            if (it != timers.end())
            {
                Fn f = it->second;
                f();
            }
            return 0;
        }
        case WM_APP_POST:
        {
            Fn* f = (Fn*)lp;
            if (f) { (*f)(); delete f; }
            return 0;
        }
        }
        return DefWindowProcW(h, m, wp, lp);
    }

    // ---------- check boxes and radio buttons ----------

    struct CheckState
    {
        bool checked = false, radio = false, down = false;
        int group = 0;
        HFONT font = nullptr;
    };

    static CheckState* StateOf(HWND h) { return (CheckState*)GetWindowLongPtrW(h, GWLP_USERDATA); }

    static void SetCheckedCore(HWND h, bool on, bool fire);

    static void UserToggle(HWND h)
    {
        CheckState* s = StateOf(h);
        if (!s || !IsWindowEnabled(h)) return;
        SetCheckedCore(h, s->radio ? true : !s->checked, true);
        Fire(h, true);
    }

    static void PaintCheck(HWND h, HDC target)
    {
        CheckState* s = StateOf(h);
        RECT rc;
        GetClientRect(h, &rc);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bmp = CreateCompatibleBitmap(target, std::max(1L, rc.right), std::max(1L, rc.bottom));
        HGDIOBJ oldBmp = SelectObject(dc, bmp);
        FillRect(dc, &rc, Brush(BackOf(GetParent(h))));
        bool en = IsWindowEnabled(h) != 0, hover = hovered == h;
        int box = S(14), y = (rc.bottom - box) / 2;
        RECT b = { 0, y, box, y + box };
        COLORREF edge = !en ? color::Line : (s->checked ? color::Accent : hover ? color::Dim : color::Border);
        if (s->radio)
        {
            HPEN pen = CreatePen(PS_SOLID, 1, edge);
            HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, Brush(color::Panel));
            Ellipse(dc, b.left, b.top, b.right, b.bottom);
            if (s->checked)
            {
                SelectObject(dc, Brush(en ? color::Accent : color::Dim));
                SelectObject(dc, GetStockObject(NULL_PEN));
                int i = S(4);
                Ellipse(dc, b.left + i, b.top + i, b.right - i + 1, b.bottom - i + 1);
            }
            SelectObject(dc, op);
            SelectObject(dc, ob);
            DeleteObject(pen);
        }
        else
        {
            FillRect(dc, &b, Brush(s->checked ? (en ? color::Accent : color::Dim) : color::Panel));
            FrameRect(dc, &b, Brush(edge));
            if (s->checked)
            {
                HPEN pen = CreatePen(PS_SOLID, std::max(2, S(2)), color::AccentText);
                HGDIOBJ op = SelectObject(dc, pen);
                POINT p[3] = { { b.left + S(3), b.top + box / 2 }, { b.left + S(6), b.bottom - S(4) }, { b.right - S(3), b.top + S(4) } };
                Polyline(dc, p, 3);
                SelectObject(dc, op);
                DeleteObject(pen);
            }
        }
        wchar_t text[512] = { 0 };
        GetWindowTextW(h, text, 512);
        RECT tr = { box + S(6), 0, rc.right, rc.bottom };
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, en ? color::Text : color::Dim);
        HGDIOBJ of = SelectObject(dc, s->font ? s->font : FontUI);
        DrawTextW(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        if (GetFocus() == h)
        {
            RECT fr = tr;
            DrawTextW(dc, text, -1, &fr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
            fr.top = (rc.bottom - (fr.bottom - fr.top)) / 2 - 1;
            fr.bottom = rc.bottom - fr.top;
            fr.left -= 2;
            fr.right = std::min(fr.right + 2, rc.right);
            FrameRect(dc, &fr, Brush(color::Border));
        }
        SelectObject(dc, of);
        BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
        DeleteDC(dc);
    }

    static LRESULT CALLBACK CheckProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
    {
        CheckState* s = StateOf(h);
        switch (m)
        {
        case WM_NCCREATE:
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)new CheckState());
            break;
        case WM_NCDESTROY:
            delete s;
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            break;
        case WM_SETFONT:
            if (s) s->font = (HFONT)wp;
            if (lp) InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_GETFONT:
            return (LRESULT)(s ? s->font : nullptr);
        case WM_GETDLGCODE:
            return DLGC_BUTTON | (s && s->radio ? DLGC_RADIOBUTTON : 0);
        case WM_KEYUP:
            if (wp == VK_SPACE) UserToggle(h);
            return 0;
        case WM_LBUTTONDOWN:
            SetFocus(h);
            SetCapture(h);
            if (s) s->down = true;
            return 0;
        case WM_LBUTTONUP:
            if (s && s->down)
            {
                s->down = false;
                ReleaseCapture();
                POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                RECT rc;
                GetClientRect(h, &rc);
                if (PtInRect(&rc, p)) UserToggle(h);
            }
            return 0;
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
            TrackHover(h, m);
            return 0;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE:
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_SETTEXT:
        {
            LRESULT r = DefWindowProcW(h, m, wp, lp);
            InvalidateRect(h, nullptr, FALSE);
            return r;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            PaintCheck(h, dc);
            EndPaint(h, &ps);
            return 0;
        }
        }
        return DefWindowProcW(h, m, wp, lp);
    }

    static BOOL CALLBACK UncheckSibling(HWND w, LPARAM lp)
    {
        HWND self = (HWND)lp;
        if (w == self || GetParent(w) != GetParent(self) || ClassOf(w) != CheckClass) return TRUE;
        CheckState* a = StateOf(self);
        CheckState* b = StateOf(w);
        if (b && b->radio && b->group == a->group && b->checked) SetCheckedCore(w, false, true);
        return TRUE;
    }

    static void SetCheckedCore(HWND h, bool on, bool fire)
    {
        CheckState* s = StateOf(h);
        if (!s || s->checked == on) return;
        s->checked = on;
        InvalidateRect(h, nullptr, FALSE);
        if (s->radio && on) EnumChildWindows(GetParent(h), UncheckSibling, (LPARAM)h);
        if (fire) Fire(h, false);
    }

    bool Checked(HWND h)
    {
        CheckState* s = StateOf(h);
        return s && s->checked;
    }

    void SetChecked(HWND h, bool on) { SetCheckedCore(h, on, true); }

    // ---------- tab strip ----------

    struct TabsState
    {
        std::vector<wstring> names;
        int sel = -1, hover = -1;
    };

    static TabsState* TabsOf(HWND h) { return (TabsState*)GetWindowLongPtrW(h, GWLP_USERDATA); }

    int TabHeight(HWND) { return TextHeight(FontBold) + 2 * S(9); }

    static std::vector<RECT> TabRects(HWND h)
    {
        std::vector<RECT> r;
        TabsState* s = TabsOf(h);
        int x = S(2), head = TabHeight(h);
        for (const wstring& n : s->names)
        {
            int w = TextWidth(FontBold, n) + 2 * S(14);
            r.push_back(RECT{ x, 0, x + w, head });
            x += w;
        }
        return r;
    }

    static LRESULT CALLBACK TabsProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
    {
        TabsState* s = TabsOf(h);
        switch (m)
        {
        case WM_NCCREATE:
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)new TabsState());
            break;
        case WM_NCDESTROY:
            delete s;
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            break;
        case WM_LBUTTONDOWN:
        {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            std::vector<RECT> r = TabRects(h);
            for (size_t i = 0; i < r.size(); i++)
                if (PtInRect(&r[i], p)) TabSelect(h, (int)i);
            return 0;
        }
        case WM_MOUSEMOVE:
        {
            POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            std::vector<RECT> r = TabRects(h);
            int over = -1;
            for (size_t i = 0; i < r.size(); i++) if (PtInRect(&r[i], p)) over = (int)i;
            if (over != s->hover)
            {
                s->hover = over;
                TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, h, 0 };
                TrackMouseEvent(&t);
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            s->hover = -1;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC target = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            HDC dc = CreateCompatibleDC(target);
            HBITMAP bmp = CreateCompatibleBitmap(target, std::max(1L, rc.right), std::max(1L, rc.bottom));
            HGDIOBJ ob = SelectObject(dc, bmp);
            FillRect(dc, &rc, Brush(color::Back));
            int head = TabHeight(h);
            RECT line = { 0, head, rc.right, head + 1 };
            FillRect(dc, &line, Brush(color::Line));
            std::vector<RECT> r = TabRects(h);
            SetBkMode(dc, TRANSPARENT);
            for (size_t i = 0; i < r.size(); i++)
            {
                bool sel = (int)i == s->sel;
                SetTextColor(dc, sel ? color::White : ((int)i == s->hover ? color::Text : color::Dim));
                HGDIOBJ of = SelectObject(dc, sel ? FontBold : FontUI);
                DrawTextW(dc, s->names[i].c_str(), -1, &r[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                SelectObject(dc, of);
                if (sel)
                {
                    RECT u = { r[i].left + S(4), head - S(2), r[i].right - S(4), head };
                    FillRect(dc, &u, Brush(color::Accent));
                }
            }
            BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
            SelectObject(dc, ob);
            DeleteObject(bmp);
            DeleteDC(dc);
            EndPaint(h, &ps);
            return 0;
        }
        }
        return DefWindowProcW(h, m, wp, lp);
    }

    int TabAdd(HWND h, const wstring& text)
    {
        TabsState* s = TabsOf(h);
        s->names.push_back(text);
        InvalidateRect(h, nullptr, FALSE);
        return (int)s->names.size() - 1;
    }

    int TabIndex(HWND h) { return TabsOf(h)->sel; }

    void TabSelect(HWND h, int index)
    {
        TabsState* s = TabsOf(h);
        if (index == s->sel || index < 0 || index >= (int)s->names.size()) return;
        s->sel = index;
        InvalidateRect(h, nullptr, FALSE);
        Fire(h, false);
    }

    // ---------- progress line ----------

    struct ProgressState
    {
        int value = 0, phase = 0;
        bool marquee = false;
    };

    static ProgressState* ProgressOf(HWND h) { return (ProgressState*)GetWindowLongPtrW(h, GWLP_USERDATA); }

    static LRESULT CALLBACK ProgressProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
    {
        ProgressState* s = ProgressOf(h);
        switch (m)
        {
        case WM_NCCREATE:
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)new ProgressState());
            break;
        case WM_NCDESTROY:
            delete s;
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            break;
        case WM_TIMER:
            s->phase = (s->phase + 1) % 120;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            FillRect(dc, &rc, Brush(color::Line));
            RECT f = rc;
            if (s->marquee)
            {
                int w = std::max(S(40), (int)rc.right / 4);
                f.left = (int)((rc.right + w) * (s->phase / 120.0)) - w;
                f.right = f.left + w;
            }
            else f.right = rc.right * s->value / 100;
            if (s->marquee || s->value > 0) FillRect(dc, &f, Brush(color::Accent));
            EndPaint(h, &ps);
            return 0;
        }
        }
        return DefWindowProcW(h, m, wp, lp);
    }

    void ProgressValue(HWND p, int value)
    {
        ProgressState* s = ProgressOf(p);
        int v = std::max(0, std::min(100, value));
        if (v != s->value) { s->value = v; InvalidateRect(p, nullptr, FALSE); }
    }

    void ProgressMarquee(HWND p, bool on)
    {
        ProgressState* s = ProgressOf(p);
        if (s->marquee == on) return;
        s->marquee = on;
        if (on) ::SetTimer(p, 1, 30, nullptr); else ::KillTimer(p, 1);
        InvalidateRect(p, nullptr, FALSE);
    }

    // ---------- start ----------

    void Init(HINSTANCE inst)
    {
        Inst = inst;
        INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_STANDARD_CLASSES | ICC_UPDOWN_CLASS | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES };
        InitCommonControlsEx(&icc);
        HDC dc = GetDC(nullptr);
        Scale = GetDeviceCaps(dc, LOGPIXELSY) / 96.0f;
        ReleaseDC(nullptr, dc);

        WNDCLASSEXW wc = { sizeof wc };
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpfnWndProc = WindowProc;
        wc.lpszClassName = PanelClass;
        RegisterClassExW(&wc);
        wc.lpszClassName = TopClass;
        wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
        wc.hIconSm = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
        RegisterClassExW(&wc);
        wc.hIcon = wc.hIconSm = nullptr;
        wc.lpfnWndProc = CheckProc;
        wc.lpszClassName = CheckClass;
        RegisterClassExW(&wc);
        wc.lpfnWndProc = TabsProc;
        wc.lpszClassName = TabsClass;
        RegisterClassExW(&wc);
        wc.lpfnWndProc = ProgressProc;
        wc.lpszClassName = ProgressClass;
        RegisterClassExW(&wc);
    }

    static int CALLBACK FontExists(const LOGFONTW*, const TEXTMETRICW*, DWORD, LPARAM lp)
    {
        *(bool*)lp = true;
        return 0;
    }

    static HFONT MakeFont(const wstring& family, int points, bool bold)
    {
        LOGFONTW lf = {};
        lf.lfHeight = -MulDiv(points, (int)(96 * Scale + 0.5f), 72);
        lf.lfWeight = bold ? FW_BOLD : FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcsncpy_s(lf.lfFaceName, family.c_str(), _TRUNCATE);
        return CreateFontIndirectW(&lf);
    }

    void InitFonts(const wstring& family)
    {
        wstring f = family;
        LOGFONTW lf = {};
        lf.lfCharSet = DEFAULT_CHARSET;
        wcsncpy_s(lf.lfFaceName, f.c_str(), _TRUNCATE);
        bool found = false;
        HDC dc = GetDC(nullptr);
        EnumFontFamiliesExW(dc, &lf, FontExists, (LPARAM)&found, 0);
        ReleaseDC(nullptr, dc);
        if (!found) f = L"Segoe UI";             // not installed
        for (HFONT* p : { &FontUI, &FontBold, &FontMono }) if (*p) { DeleteObject(*p); *p = nullptr; }
        FontUI = MakeFont(f, 9, false);
        FontBold = MakeFont(f, 9, true);
        FontMono = MakeFont(L"Consolas", 9, false);
    }

    void DarkTitle(HWND top)
    {
        BOOL on = TRUE;
        // DWMWA_USE_IMMERSIVE_DARK_MODE: 20 (Windows 10 2004+), 19 before
        if (FAILED(DwmSetWindowAttribute(top, 20, &on, sizeof on))) DwmSetWindowAttribute(top, 19, &on, sizeof on);
    }

    int MessageLoop()
    {
        MSG msg = {};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            HWND top = GetAncestor(msg.hwnd, GA_ROOT);
            if (top && IsDialogMessageW(top, &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return (int)msg.wParam;
    }

    // ---------- windows ----------

    HWND TopWindow(const wstring& title, int w, int h, HWND owner, DWORD style)
    {
        RECT rc = { 0, 0, S(w), S(h) };
        AdjustWindowRectEx(&rc, style, FALSE, WS_EX_CONTROLPARENT);
        HWND win = CreateWindowExW(WS_EX_CONTROLPARENT, TopClass, title.c_str(), style | WS_CLIPCHILDREN,
            CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, owner, nullptr, Inst, nullptr);
        backColors[win] = color::Back;
        DarkTitle(win);
        return win;
    }

    HWND Panel(HWND parent, COLORREF back)
    {
        HWND p = CreateWindowExW(WS_EX_CONTROLPARENT, PanelClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, 10, 10, parent, nullptr, Inst, nullptr);
        backColors[p] = back;
        return p;
    }

    int GroupTop() { return TextHeight(FontBold) + S(4); }

    HWND Group(HWND parent, const wstring& title)
    {
        HWND g = Panel(parent, color::Back);
        painters[g] = [title](HDC dc, const RECT& r)
        {
            int th = TextHeight(FontBold), y = th / 2;
            RECT frame = { 0, y, r.right, r.bottom };
            FrameRect(dc, &frame, Brush(color::Line));
            HGDIOBJ o = SelectObject(dc, FontBold);
            RECT tr = { S(8), 0, S(8) + TextWidth(FontBold, title) + S(4), th };
            FillRect(dc, &tr, Brush(color::Back));
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, color::Dim);
            DrawTextW(dc, title.c_str(), -1, &tr, DT_CENTER | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, o);
        };
        return g;
    }

    void SetBack(HWND panel, COLORREF back) { backColors[panel] = back; InvalidateRect(panel, nullptr, TRUE); }
    void SetLayout(HWND panel, std::function<void(int, int)> f) { layouts[panel] = f; }
    void SetPaint(HWND panel, std::function<void(HDC, const RECT&)> f) { painters[panel] = f; }
    void SetMinSize(HWND top, int w, int h) { minSizes[top] = SIZE{ w, h }; }
    void OnClose(HWND top, std::function<bool()> f) { closers[top] = f; }
    void OnDestroy(HWND top, Fn f) { destroyers[top] = f; }
    void SetTimer(HWND w, UINT_PTR id, UINT ms, Fn f) { timers[std::make_pair(w, id)] = f; ::SetTimer(w, id, ms, nullptr); }
    void KillTimer(HWND w, UINT_PTR id) { ::KillTimer(w, id); timers.erase(std::make_pair(w, id)); }
    void Post(HWND w, Fn f) { PostMessageW(w, WM_APP_POST, 0, (LPARAM)new Fn(f)); }

    void Relayout(HWND panel)
    {
        RECT rc;
        GetClientRect(panel, &rc);
        auto it = layouts.find(panel);
        if (it != layouts.end()) { auto f = it->second; f(rc.right, rc.bottom); }
    }

    // ---------- controls ----------

    static HWND Child(const wchar_t* cls, HWND parent, const wstring& text, DWORD style, DWORD ex = 0)
    {
        HWND h = CreateWindowExW(ex, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, parent, nullptr, Inst, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)FontUI, FALSE);
        return h;
    }

    HWND Label(HWND parent, const wstring& text, HFONT font, COLORREF fore, DWORD style)
    {
        HWND h = Child(L"STATIC", parent, text, SS_NOPREFIX | (style ? style : SS_LEFT));
        if (font) SendMessageW(h, WM_SETFONT, (WPARAM)font, FALSE);
        foreColors[h] = fore;
        return h;
    }

    HWND Link(HWND parent, const wstring& text)
    {
        HWND h = Label(parent, text, FontUI, color::AccentSoft, SS_LEFT | SS_NOTIFY);
        SetWindowSubclass(h, LinkSub, 1, 0);
        return h;
    }

    void SetColor(HWND label, COLORREF fore) { foreColors[label] = fore; InvalidateRect(label, nullptr, TRUE); }

    HWND Button(HWND parent, const wstring& text)
    {
        HWND h = Child(L"BUTTON", parent, text, WS_TABSTOP | BS_OWNERDRAW);
        SetWindowSubclass(h, ButtonSub, 1, 0);
        return h;
    }

    void Primary(HWND button, bool danger)
    {
        buttonKinds[button] = danger ? 2 : 1;
        SendMessageW(button, WM_SETFONT, (WPARAM)FontBold, TRUE);
    }

    HWND Edit(HWND parent, DWORD style)
    {
        HWND h = Child(L"EDIT", parent, L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | style);
        // dark scroll bars; a single line edit keeps the classic look, so that
        // WM_CTLCOLORSTATIC colors it when disabled (the theme would draw it grey)
        SetWindowTheme(h, style & ES_MULTILINE ? L"DarkMode_Explorer" : L"", style & ES_MULTILINE ? nullptr : L"");
        SetWindowSubclass(h, EditSub, 1, 0);
        return h;
    }

    HWND Combo(HWND parent, bool editable)
    {
        HWND h = Child(L"COMBOBOX", parent, L"", WS_TABSTOP | WS_VSCROLL | CBS_AUTOHSCROLL | (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST));
        SetWindowTheme(h, L"DarkMode_CFD", nullptr);
        if (editable) SetWindowSubclass(h, EditComboSub, 1, 0);
        return h;
    }

    HWND Number(HWND parent, int min, int max, int value)
    {
        HWND e = Edit(parent, ES_NUMBER);
        HWND u = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS | UDS_HOTTRACK,
            0, 0, 10, 10, parent, nullptr, Inst, nullptr);
        SetWindowTheme(u, L"DarkMode_Explorer", nullptr);
        SendMessageW(u, UDM_SETBUDDY, (WPARAM)e, 0);
        SendMessageW(u, UDM_SETRANGE32, (WPARAM)min, (LPARAM)max);
        SendMessageW(u, UDM_SETPOS32, 0, (LPARAM)value);
        upDowns[e] = u;
        return e;
    }

    int NumberValue(HWND h)
    {
        auto it = upDowns.find(h);
        int lo = 0, hi = 0;
        if (it != upDowns.end()) SendMessageW(it->second, UDM_GETRANGE32, (WPARAM)&lo, (LPARAM)&hi);
        long long v;
        if (!sevenwim::TryParseLong(GetText(h), v)) return lo;
        if (it != upDowns.end()) v = std::max<long long>(lo, std::min<long long>(hi, v));
        return (int)v;
    }

    void SetNumber(HWND h, int value)
    {
        auto it = upDowns.find(h);
        if (it != upDowns.end()) SendMessageW(it->second, UDM_SETPOS32, 0, (LPARAM)value);
        else SetText(h, std::to_wstring(value));
    }

    void NumberStep(HWND h, int step)
    {
        auto it = upDowns.find(h);
        if (it == upDowns.end()) return;
        UDACCEL a = { 0, (UINT)step };
        SendMessageW(it->second, UDM_SETACCEL, 1, (LPARAM)&a);
    }

    static HWND CheckCore(HWND parent, const wstring& text, bool radio, int group)
    {
        HWND h = Child(CheckClass, parent, text, WS_TABSTOP);
        CheckState* s = StateOf(h);
        s->radio = radio;
        s->group = group;
        return h;
    }

    HWND Check(HWND parent, const wstring& text) { return CheckCore(parent, text, false, 0); }
    HWND Radio(HWND parent, const wstring& text, int group) { return CheckCore(parent, text, true, group); }

    HWND Tabs(HWND parent)
    {
        HWND h = CreateWindowExW(0, TabsClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, nullptr, Inst, nullptr);
        return h;
    }

    // the list draws its rows itself; a selected row gets the accent tone
    // (also without focus), a bold row the bold font
    static LRESULT ListDraw(HWND list, NMLVCUSTOMDRAW* cd)
    {
        int row = (int)cd->nmcd.dwItemSpec;
        auto bold = [list, row]
        {
            auto b = boldRows.find(list);
            return b != boldRows.end() && row < (int)b->second.size() && b->second[row];
        };
        switch (cd->nmcd.dwDrawStage)
        {
        case CDDS_PREPAINT:
            return CDRF_NOTIFYITEMDRAW;
        case CDDS_ITEMPREPAINT:
            // a selected row is drawn cell by cell below (the theme would draw it blue)
            if (ListView_GetItemState(list, row, LVIS_SELECTED)) return CDRF_NOTIFYSUBITEMDRAW;
            cd->clrTextBk = color::Panel;
            cd->clrText = color::Text;
            if (bold()) { SelectObject(cd->nmcd.hdc, FontBold); return CDRF_NEWFONT; }
            return CDRF_DODEFAULT;
        case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
        {
            int col = cd->iSubItem;
            RECT r;
            ListView_GetSubItemRect(list, row, col, col == 0 ? LVIR_LABEL : LVIR_BOUNDS, &r);
            if (col == 0)
            {
                // the first column: from the left edge of the row
                RECT whole;
                ListView_GetItemRect(list, row, &whole, LVIR_BOUNDS);
                r.left = whole.left;
            }
            HDC dc = cd->nmcd.hdc;
            FillRect(dc, &r, Brush(color::Selected));
            if (col == 0)
            {
                LVITEMW it = {};
                it.mask = LVIF_IMAGE;
                it.iItem = row;
                HIMAGELIST il = ListView_GetImageList(list, LVSIL_SMALL);
                if (il && ListView_GetItem(list, &it) && it.iImage >= 0)
                {
                    int cx = 16, cy = 16;
                    ImageList_GetIconSize(il, &cx, &cy);
                    ImageList_Draw(il, it.iImage, dc, r.left + S(4), r.top + (r.bottom - r.top - cy) / 2, ILD_TRANSPARENT);
                    r.left += S(4) + cx;
                }
            }
            wchar_t text[512] = { 0 };
            ListView_GetItemText(list, row, col, text, 512);
            LVCOLUMNW c = {};
            c.mask = LVCF_FMT;
            ListView_GetColumn(list, col, &c);
            RECT t = { r.left + S(6), r.top, r.right - S(6), r.bottom };
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, color::White);
            HGDIOBJ o = SelectObject(dc, bold() ? FontBold : FontUI);
            DrawTextW(dc, text, -1, &t, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX
                | ((c.fmt & LVCFMT_JUSTIFYMASK) == LVCFMT_RIGHT ? DT_RIGHT : DT_LEFT));
            SelectObject(dc, o);
            return CDRF_SKIPDEFAULT;
        }
        }
        return CDRF_DODEFAULT;
    }

    // the header of a list: drawn here (the dark theme leaves its text dark);
    // its WM_DRAWITEM goes to the list, its parent
    static LRESULT CALLBACK ListSub(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
    {
        if (m == WM_DRAWITEM)
        {
            const DRAWITEMSTRUCT* d = (const DRAWITEMSTRUCT*)lp;
            if (d->CtlType != ODT_HEADER) return DefSubclassProc(h, m, wp, lp);
            RECT r = d->rcItem;
            FillRect(d->hDC, &r, Brush(color::Button));
            RECT sep = { r.right - 1, r.top + S(4), r.right, r.bottom - S(4) };
            FillRect(d->hDC, &sep, Brush(color::Border));
            RECT line = { r.left, r.bottom - 1, r.right, r.bottom };
            FillRect(d->hDC, &line, Brush(color::Border));
            wchar_t text[256] = { 0 };
            HDITEMW it = {};
            it.mask = HDI_TEXT | HDI_FORMAT;
            it.pszText = text;
            it.cchTextMax = 256;
            Header_GetItem(d->hwndItem, d->itemID, &it);
            RECT t = { r.left + S(6), r.top, r.right - S(6), r.bottom };
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, color::Dim);
            HGDIOBJ o = SelectObject(d->hDC, FontUI);
            DrawTextW(d->hDC, text, -1, &t, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX
                | ((it.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT ? DT_RIGHT : DT_LEFT));
            SelectObject(d->hDC, o);
            return TRUE;
        }
        if (m == WM_NOTIFY)
        {
            // the empty part right of the last column
            NMHDR* n = (NMHDR*)lp;
            if (n->code == NM_CUSTOMDRAW && n->hwndFrom == ListView_GetHeader(h))
            {
                NMCUSTOMDRAW* cd = (NMCUSTOMDRAW*)lp;
                if (cd->dwDrawStage == CDDS_PREPAINT)
                {
                    RECT rc;
                    GetClientRect(n->hwndFrom, &rc);
                    FillRect(cd->hdc, &rc, Brush(color::Button));
                    return CDRF_DODEFAULT;
                }
            }
        }
        return DefSubclassProc(h, m, wp, lp);
    }

    void OnListNotify(HWND list, std::function<bool(NMHDR*, LRESULT&)> f) { listExtra[list] = f; }

    HWND List(HWND parent, const std::vector<std::pair<wstring, int>>& columns, DWORD style)
    {
        HWND h = Child(WC_LISTVIEWW, parent, L"", WS_TABSTOP | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SHAREIMAGELISTS | style);
        ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
        SetWindowTheme(h, L"DarkMode_Explorer", nullptr);
        HWND header = ListView_GetHeader(h);
        if (header) SetWindowTheme(header, L"DarkMode_ItemsView", nullptr);
        ListView_SetBkColor(h, color::Panel);
        ListView_SetTextBkColor(h, color::Panel);
        ListView_SetTextColor(h, color::Text);
        for (size_t i = 0; i < columns.size(); i++)
        {
            LVCOLUMNW c = {};
            c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_FMT;
            c.fmt = columns[i].second < 0 ? LVCFMT_RIGHT : LVCFMT_LEFT;
            c.cx = S(columns[i].second < 0 ? -columns[i].second : columns[i].second);
            c.pszText = (LPWSTR)columns[i].first.c_str();
            ListView_InsertColumn(h, (int)i, &c);
            if (header)
            {
                HDITEMW hi = {};
                hi.mask = HDI_FORMAT;
                Header_GetItem(header, (int)i, &hi);
                hi.fmt |= HDF_OWNERDRAW;
                Header_SetItem(header, (int)i, &hi);
            }
        }
        SetWindowSubclass(h, ListSub, 1, 0);
        // no dotted focus frame around the selected row
        SendMessageW(h, WM_CHANGEUISTATE, MAKEWPARAM(UIS_SET, UISF_HIDEFOCUS), 0);
        handlers[h].notify = [h](NMHDR* n) -> LRESULT
        {
            if (n->code == NM_CUSTOMDRAW) return ListDraw(h, (NMLVCUSTOMDRAW*)n);
            auto x = listExtra.find(h);
            if (x != listExtra.end())
            {
                LRESULT r = 0;
                auto f = x->second;
                if (f(n, r)) return r;
            }
            // an owner data list tells a range of selected rows at once
            if (n->code == LVN_ODSTATECHANGED) Fire(h, false);
            if (n->code == LVN_ITEMCHANGED)
            {
                NMLISTVIEW* lv = (NMLISTVIEW*)n;
                if ((lv->uChanged & LVIF_STATE) && ((lv->uOldState ^ lv->uNewState) & LVIS_SELECTED)) Fire(h, false);
            }
            if (n->code == NM_DBLCLK)
            {
                auto it = listDouble.find(h);
                if (it != listDouble.end()) { Fn f = it->second; f(); }
            }
            return 0;
        };
        return h;
    }

    int ListAdd(HWND list, const std::vector<wstring>& cells, bool bold)
    {
        LVITEMW it = {};
        it.mask = LVIF_TEXT;
        it.iItem = ListView_GetItemCount(list);
        it.pszText = (LPWSTR)(cells.empty() ? L"" : cells[0].c_str());
        int row = ListView_InsertItem(list, &it);
        for (size_t i = 1; i < cells.size(); i++) ListView_SetItemText(list, row, (int)i, (LPWSTR)cells[i].c_str());
        std::vector<bool>& b = boldRows[list];
        if ((int)b.size() <= row) b.resize(row + 1, false);
        b[row] = bold;
        return row;
    }

    void ListClear(HWND list)
    {
        ListView_DeleteAllItems(list);
        boldRows[list].clear();
    }

    int ListSelected(HWND list) { return ListView_GetNextItem(list, -1, LVNI_SELECTED); }

    void ListSelect(HWND list, int index)
    {
        ListView_SetItemState(list, index, LVIS_SELECTED, LVIS_SELECTED);
        ListView_EnsureVisible(list, index, FALSE);
    }

    void OnListDoubleClick(HWND list, Fn f) { listDouble[list] = f; }

    HWND Progress(HWND parent)
    {
        return CreateWindowExW(0, ProgressClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, nullptr, Inst, nullptr);
    }

    void Tooltip(HWND control, const wstring& text)
    {
        HWND tip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, GetAncestor(control, GA_ROOT), nullptr, Inst, nullptr);
        TOOLINFOW ti = { sizeof ti };
        ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        ti.hwnd = GetParent(control);
        ti.uId = (UINT_PTR)control;
        ti.lpszText = (LPWSTR)text.c_str();
        SendMessageW(tip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
        SendMessageW(tip, TTM_SETMAXTIPWIDTH, 0, S(520));
        SendMessageW(tip, TTM_SETDELAYTIME, TTDT_AUTOPOP, 30000);
    }

    wstring GetText(HWND h)
    {
        int n = GetWindowTextLengthW(h);
        wstring s((size_t)n + 1, L'\0');
        n = GetWindowTextW(h, &s[0], n + 1);
        s.resize(n);
        return s;
    }

    void SetText(HWND h, const wstring& text)
    {
        if (GetText(h) == text) return;
        SetWindowTextW(h, text.c_str());
        if (ClassOf(h) == L"Static") InvalidateRect(h, nullptr, TRUE);
    }

    void Enable(HWND h, bool on)
    {
        if ((IsWindowEnabled(h) != 0) == on) return;
        EnableWindow(h, on);
        auto it = upDowns.find(h);
        if (it != upDowns.end()) EnableWindow(it->second, on);
        if (!on && hovered == h) hovered = nullptr;
    }

    void Show(HWND h, bool on)
    {
        ShowWindow(h, on ? SW_SHOWNA : SW_HIDE);
        auto it = upDowns.find(h);
        if (it != upDowns.end()) ShowWindow(it->second, on ? SW_SHOWNA : SW_HIDE);
    }

    int ComboCount(HWND h) { return (int)SendMessageW(h, CB_GETCOUNT, 0, 0); }
    int ComboIndex(HWND h) { return (int)SendMessageW(h, CB_GETCURSEL, 0, 0); }

    void ComboSelect(HWND h, int index)
    {
        if (ComboIndex(h) == index) return;
        SendMessageW(h, CB_SETCURSEL, (WPARAM)index, 0);
        Fire(h, false);
    }

    int ComboAdd(HWND h, const wstring& text) { return (int)SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)text.c_str()); }

    void ComboClear(HWND h)
    {
        bool had = ComboIndex(h) >= 0;
        SendMessageW(h, CB_RESETCONTENT, 0, 0);
        if (had) Fire(h, false);
    }

    wstring ComboItem(HWND h, int index)
    {
        int n = (int)SendMessageW(h, CB_GETLBTEXTLEN, (WPARAM)index, 0);
        if (n < 0) return wstring();
        wstring s((size_t)n + 1, L'\0');
        SendMessageW(h, CB_GETLBTEXT, (WPARAM)index, (LPARAM)&s[0]);
        s.resize(n);
        return s;
    }

    int TextWidth(HFONT font, const wstring& text)
    {
        HDC dc = GetDC(nullptr);
        HGDIOBJ o = SelectObject(dc, font ? font : FontUI);
        RECT r = { 0, 0, 0, 0 };
        DrawTextW(dc, text.c_str(), (int)text.size(), &r, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, o);
        ReleaseDC(nullptr, dc);
        return r.right;
    }

    int TextHeight(HFONT font)
    {
        HDC dc = GetDC(nullptr);
        HGDIOBJ o = SelectObject(dc, font ? font : FontUI);
        TEXTMETRICW tm;
        GetTextMetricsW(dc, &tm);
        SelectObject(dc, o);
        ReleaseDC(nullptr, dc);
        return tm.tmHeight;
    }

    int CheckWidth(HWND check) { return S(14) + S(6) + TextWidth(FontOf(check), GetText(check)) + S(4); }

    void Move(HWND h, int x, int y, int w, int hgt)
    {
        auto it = upDowns.find(h);
        if (it != upDowns.end())
        {
            int uw = GetSystemMetrics(SM_CXVSCROLL);
            MoveWindow(h, x, y, std::max(0, w - uw), hgt, TRUE);
            MoveWindow(it->second, x + w - uw, y, uw, hgt, TRUE);
            return;
        }
        // a combo box: the height of its list
        if (ClassOf(h) == L"ComboBox") hgt = S(300);
        MoveWindow(h, x, y, std::max(0, w), std::max(0, hgt), TRUE);
    }

    int TextHeightWrapped(HFONT font, const wstring& text, int width)
    {
        HDC dc = GetDC(nullptr);
        HGDIOBJ o = SelectObject(dc, font ? font : FontUI);
        RECT r = { 0, 0, std::max(1, width), 0 };
        DrawTextW(dc, text.c_str(), (int)text.size(), &r, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
        SelectObject(dc, o);
        ReleaseDC(nullptr, dc);
        return r.bottom;
    }

    // ---------- messages and dialogs ----------

    static HWND ActiveOwner()
    {
        HWND a = GetActiveWindow();
        return a && IsWindowEnabled(a) ? a : Main;
    }

    void Info(const wstring& text, UINT icon) { MessageBoxW(ActiveOwner(), text.c_str(), L"7z_wimlib", MB_OK | icon); }

    bool YesNo(const wstring& text, UINT icon)
    {
        return MessageBoxW(ActiveOwner(), text.c_str(), L"7z_wimlib", MB_YESNO | icon) == IDYES;
    }

    void OnKeys(HWND top, Fn ok, Fn cancel) { keyHandlers[top] = std::make_pair(ok, cancel); }

    void EndModal(HWND dlg, int result) { modalResults[dlg] = result; }

    HWND Dialog(const wstring& title, int w, int h)
    {
        HWND d = TopWindow(title, w, h, Main, WS_POPUP | WS_CAPTION | WS_SYSMENU);
        RECT o, me;
        GetWindowRect(Main, &o);
        GetWindowRect(d, &me);
        SetWindowPos(d, nullptr, o.left + ((o.right - o.left) - (me.right - me.left)) / 2,
            o.top + ((o.bottom - o.top) - (me.bottom - me.top)) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
        OnClose(d, [d] { EndModal(d, 0); return false; });
        return d;
    }

    int RunModal(HWND dlg)
    {
        HWND owner = GetWindow(dlg, GW_OWNER);
        modalResults.erase(dlg);
        if (owner) EnableWindow(owner, FALSE);
        ShowWindow(dlg, SW_SHOW);
        SetForegroundWindow(dlg);
        MSG msg = {};
        bool quit = false;
        while (modalResults.find(dlg) == modalResults.end())
        {
            if (GetMessageW(&msg, nullptr, 0, 0) <= 0) { quit = true; break; }
            if (IsDialogMessageW(dlg, &msg)) continue;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        int result = quit ? 0 : modalResults[dlg];
        modalResults.erase(dlg);
        if (owner) EnableWindow(owner, TRUE);
        DestroyWindow(dlg);
        if (owner) SetActiveWindow(owner);
        if (quit) PostQuitMessage((int)msg.wParam);
        return result;
    }

    // ---------- file dialogs (IFileDialog) ----------

    static bool RunFileDialog(IFileDialog* d, const Filters& filters, const wstring& initialDir)
    {
        std::vector<COMDLG_FILTERSPEC> spec;
        for (const auto& f : filters) spec.push_back(COMDLG_FILTERSPEC{ f.first.c_str(), f.second.c_str() });
        if (!spec.empty()) d->SetFileTypes((UINT)spec.size(), spec.data());
        if (!initialDir.empty() && sevenwim::DirExists(initialDir))
        {
            IShellItem* folder = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(initialDir.c_str(), nullptr, IID_PPV_ARGS(&folder))))
            {
                d->SetFolder(folder);
                folder->Release();
            }
        }
        return SUCCEEDED(d->Show(ActiveOwner()));
    }

    static wstring ItemPath(IShellItem* item)
    {
        wstring r;
        PWSTR p = nullptr;
        if (item && SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) { r = p; CoTaskMemFree(p); }
        return r;
    }

    bool OpenFile(const Filters& filters, const wstring& initialDir, wstring& file)
    {
        IFileOpenDialog* d = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return false;
        DWORD o = 0;
        d->GetOptions(&o);
        d->SetOptions(o | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST);
        bool ok = false;
        if (RunFileDialog(d, filters, initialDir))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(d->GetResult(&item))) { file = ItemPath(item); item->Release(); ok = !file.empty(); }
        }
        d->Release();
        return ok;
    }

    bool OpenFiles(const Filters& filters, const wstring& initialDir, std::vector<wstring>& files)
    {
        IFileOpenDialog* d = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return false;
        DWORD o = 0;
        d->GetOptions(&o);
        d->SetOptions(o | FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_ALLOWMULTISELECT);
        files.clear();
        if (RunFileDialog(d, filters, initialDir))
        {
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(d->GetResults(&items)))
            {
                DWORD n = 0;
                items->GetCount(&n);
                for (DWORD i = 0; i < n; i++)
                {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &item))) { wstring p = ItemPath(item); if (!p.empty()) files.push_back(p); item->Release(); }
                }
                items->Release();
            }
        }
        d->Release();
        return !files.empty();
    }

    bool SaveFile(const Filters& filters, const wstring& initialDir, const wstring& fileName, bool overwritePrompt, wstring& file)
    {
        IFileSaveDialog* d = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return false;
        DWORD o = 0;
        d->GetOptions(&o);
        o |= FOS_FORCEFILESYSTEM;
        if (overwritePrompt) o |= FOS_OVERWRITEPROMPT; else o &= ~FOS_OVERWRITEPROMPT;
        d->SetOptions(o);
        if (!fileName.empty()) d->SetFileName(fileName.c_str());
        // the extension of the first filter is added when none is typed
        if (!filters.empty())
        {
            wstring ext = filters[0].second;
            size_t dot = ext.find(L'.');
            size_t semi = ext.find(L';');
            if (dot != wstring::npos) d->SetDefaultExtension(ext.substr(dot + 1, semi == wstring::npos ? wstring::npos : semi - dot - 1).c_str());
        }
        bool ok = false;
        if (RunFileDialog(d, filters, initialDir))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(d->GetResult(&item))) { file = ItemPath(item); item->Release(); ok = !file.empty(); }
        }
        d->Release();
        return ok;
    }

    bool PickFolder(const wstring& title, const wstring& initialDir, wstring& folder)
    {
        IFileOpenDialog* d = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return false;
        DWORD o = 0;
        d->GetOptions(&o);
        d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
        if (!title.empty()) d->SetTitle(title.c_str());
        bool ok = false;
        if (RunFileDialog(d, Filters(), initialDir))
        {
            IShellItem* item = nullptr;
            if (SUCCEEDED(d->GetResult(&item))) { folder = ItemPath(item); item->Release(); ok = !folder.empty(); }
        }
        d->Release();
        return ok;
    }

    // ---------- row layout ----------

    Item Field(HWND h, int w) { Item i; i.h = h; i.w = w; i.top = 5; i.right = 6; i.height = 23; return i; }
    Item Fill(HWND h) { Item i = Field(h, 0); i.fill = true; return i; }
    Item Btn(HWND h, int w) { Item i; i.h = h; i.w = w; i.top = 2; i.right = 8; i.height = 28; return i; }
    Item Box(HWND h) { Item i; i.h = h; i.top = 7; i.right = 16; i.height = 20; return i; }
    Item Txt(HWND h, int left, int right) { Item i; i.h = h; i.left = left; i.top = 8; i.right = right; i.height = 18; return i; }

    void Grid::Add(const wstring& label, std::initializer_list<Item> items)
    {
        Row r;
        r.label = Label(parent, label, FontBold, color::Dim, SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS);
        r.items.assign(items.begin(), items.end());
        rows.push_back(r);
    }

    void Grid::PlaceRow(const std::vector<Item>& items, int x, int y, int w)
    {
        int cx = x, end = x + w;
        for (const Item& it : items)
        {
            // hidden items take no room (the style: a page of another tab is hidden as a whole)
            if (!it.h || !(GetWindowLongPtrW(it.h, GWL_STYLE) & WS_VISIBLE)) continue;
            cx += S(it.left);
            int width;
            if (it.fill) width = end - cx - S(it.right);
            else if (it.w > 0) width = S(it.w);
            else width = ClassOf(it.h) == CheckClass ? CheckWidth(it.h) : TextWidth(FontOf(it.h), GetText(it.h)) + S(2);
            Move(it.h, cx, y + S(it.top), width, S(it.height));
            cx += width + S(it.right);
        }
    }

    int Grid::Place(int x, int y, int w)
    {
        for (const Row& r : rows)
        {
            Move(r.label, x, y, S(labelWidth), S(rowHeight));
            PlaceRow(r.items, x + S(labelWidth), y, w - S(labelWidth));
            y += S(rowHeight);
        }
        return y;
    }
}
