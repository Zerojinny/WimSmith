// ============================================================
//  WimSmith - ui.cpp
//  The window toolkit (see ui.h).
// ============================================================
#include "ui.h"
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <initguid.h>   // the GUIDs of oleacc.h (PROPID_ACC_*, CLSID_AccPropServices) defined here
#include <oleacc.h>     // MSAA: only the interfaces; oleacc.dll is loaded at run time (a WinPE may lack it)
#include <algorithm>
#include <map>
#include <set>

namespace ui
{
    float Scale = 1.0f;
    HINSTANCE Inst;
    HWND Main;
    HFONT FontUI, FontBold, FontMono;

    static const wchar_t* const PanelClass = L"WimSmithPanel";
    static const wchar_t* const TopClass = L"WimSmithWindow";
    static const wchar_t* const MenuClass = L"WimSmithMenuOwner";
    static const wchar_t* const CheckClass = L"WimSmithCheck";
    static const wchar_t* const TabsClass = L"WimSmithTabs";
    static const wchar_t* const ProgressClass = L"WimSmithProgress";
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
    struct ScrollState { HWND inner = nullptr; int minW = 0, minH = 0, x = 0, y = 0; bool busy = false; };
    static std::map<HWND, ScrollState> scrollers;          // scrolling host -> its inner panel
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

    bool DarkMode = true;

    void SetTheme(bool dark)
    {
        DarkMode = dark;
        using namespace color;
        if (dark)
        {
            Back = RGB(0x17, 0x19, 0x1C); color::Panel = RGB(0x20, 0x23, 0x28);
            color::Button = RGB(0x2A, 0x2E, 0x34); ButtonHover = RGB(0x34, 0x39, 0x40); ButtonDown = RGB(0x3F, 0x45, 0x4D);
            Border = RGB(0x35, 0x3A, 0x40); FieldBorder = RGB(0x6E, 0x75, 0x7E); Line = RGB(0x2C, 0x30, 0x35); Footer = RGB(0x11, 0x13, 0x16);
            Text = RGB(0xE6, 0xE8, 0xEB); Dim = RGB(0xA0, 0xA6, 0xAE);
            Accent = RGB(0x3D, 0xD6, 0xC6); AccentText = RGB(0x08, 0x20, 0x1E); AccentSoft = RGB(0x7F, 0xE3, 0xD8);
            Danger = RGB(0xF4, 0x70, 0x67); DangerText = RGB(0x1A, 0x08, 0x07);
            Selected = RGB(0x1D, 0x3B, 0x39); Strong = RGB(0xFF, 0xFF, 0xFF);
        }
        else
        {
            Back = RGB(0xF3, 0xF4, 0xF6); color::Panel = RGB(0xFF, 0xFF, 0xFF);
            color::Button = RGB(0xE4, 0xE7, 0xEB); ButtonHover = RGB(0xD8, 0xDC, 0xE1); ButtonDown = RGB(0xB8, 0xBE, 0xC6);
            Border = RGB(0xB8, 0xBE, 0xC6); FieldBorder = RGB(0x84, 0x8C, 0x96); Line = RGB(0xD3, 0xD7, 0xDC); Footer = RGB(0xE6, 0xE8, 0xEB);
            Text = RGB(0x1E, 0x22, 0x27); Dim = RGB(0x5C, 0x63, 0x6C);
            Accent = RGB(0x0C, 0x7D, 0x73); AccentText = RGB(0xFF, 0xFF, 0xFF);   // white on it: 5.0:1 AccentSoft = RGB(0x0A, 0x6E, 0x65);
            Danger = RGB(0xC6, 0x35, 0x2B); DangerText = RGB(0xFF, 0xFF, 0xFF);
            Selected = RGB(0xC8, 0xEC, 0xE7); Strong = RGB(0x00, 0x00, 0x00);
        }
    }

    const wchar_t* ThemeApp(const wchar_t* darkName)
    {
        if (DarkMode || wcsncmp(darkName, L"DarkMode_", 9) != 0) return darkName;
        return darkName + 9;
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

    // the callbacks of a window and its children (before it is destroyed:
    // a later window may get the same handle)
    static void AccForget(HWND h);

    static void Forget(HWND h)
    {
        handlers.erase(h); foreColors.erase(h); backColors.erase(h); layouts.erase(h); painters.erase(h);
        minSizes.erase(h); closers.erase(h); destroyers.erase(h); buttonKinds.erase(h); upDowns.erase(h);
        boldRows.erase(h); listDouble.erase(h); listExtra.erase(h); keyHandlers.erase(h); scrollers.erase(h);
        for (auto it = timers.begin(); it != timers.end();) it = it->first.first == h ? timers.erase(it) : std::next(it);
        if (hovered == h) hovered = nullptr;
        AccForget(h);
    }

    static void ForgetTree(HWND top)
    {
        EnumChildWindows(top, [](HWND c, LPARAM) -> BOOL { Forget(c); return TRUE; }, 0);
        Forget(top);
    }

    void Reset()
    {
        handlers.clear(); foreColors.clear(); backColors.clear(); layouts.clear(); painters.clear();
        minSizes.clear(); closers.clear(); destroyers.clear(); timers.clear(); buttonKinds.clear();
        upDowns.clear(); boldRows.clear(); listDouble.clear(); listExtra.clear(); keyHandlers.clear(); modalResults.clear(); hovered = nullptr;
        scrollers.clear();
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

    // ---------- accessibility (MSAA: what a screen reader reads) ----------
    // Dynamic Annotation (AccPropServices of oleacc, through COM, so that the
    // exe does not need oleacc.dll to start): the role, state, name or value
    // of a control Windows cannot know, such as the check boxes drawn here.
    // Without the service (a WinPE without oleacc) nothing is annotated.

    static std::set<HWND> annotated;

    static IAccPropServices* AccProps()
    {
        static IAccPropServices* svc = nullptr;
        static bool tried = false;
        if (!tried)
        {
            tried = true;
            if (FAILED(CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IAccPropServices), (void**)&svc))) svc = nullptr;
        }
        return svc;
    }

    static void AccInt(HWND h, const MSAAPROPID& prop, int value)
    {
        IAccPropServices* s = AccProps();
        if (!s) return;
        VARIANT v = {};
        v.vt = VT_I4;
        v.lVal = value;
        if (SUCCEEDED(s->SetHwndProp(h, (DWORD)OBJID_CLIENT, CHILDID_SELF, prop, v))) annotated.insert(h);
    }

    static void AccStr(HWND h, const MSAAPROPID& prop, const wstring& text)
    {
        IAccPropServices* s = AccProps();
        if (s && SUCCEEDED(s->SetHwndPropStr(h, (DWORD)OBJID_CLIENT, CHILDID_SELF, prop, text.c_str()))) annotated.insert(h);
    }

    void AccName(HWND h, const wstring& name) { AccStr(h, PROPID_ACC_NAME, name); }

    static void AccForget(HWND h)
    {
        if (!annotated.erase(h)) return;
        MSAAPROPID props[] = { PROPID_ACC_NAME, PROPID_ACC_ROLE, PROPID_ACC_STATE, PROPID_ACC_VALUE };
        if (IAccPropServices* s = AccProps()) s->ClearHwndProps(h, (DWORD)OBJID_CLIENT, CHILDID_SELF, props, 4);
    }

    // oleacc.dll, loaded once when a screen reader first asks (WM_GETOBJECT)
    static HMODULE Oleacc()
    {
        static HMODULE m = LoadLibraryExW(L"oleacc.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        return m;
    }

    // ---------- owner-drawn buttons ----------

    static void DrawButton(const DRAWITEMSTRUCT* d)
    {
        HWND h = d->hwndItem;
        auto k = buttonKinds.find(h);
        int kind = k == buttonKinds.end() ? 0 : k->second;
        bool enabled = !(d->itemState & ODS_DISABLED), down = (d->itemState & ODS_SELECTED) != 0, hover = hovered == h;
        bool focus = (d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT);
        wchar_t text[256] = { 0 };
        GetWindowTextW(h, text, 256);
        if (kind == 3)
        {
            // a link: accent text on the background of its parent; a frame
            // around the text with keyboard focus
            RECT r = d->rcItem;
            FillRect(d->hDC, &r, Brush(BackOf(GetParent(h))));
            auto fc = foreColors.find(h);
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, !enabled ? color::Dim : fc != foreColors.end() ? fc->second : color::AccentSoft);
            HGDIOBJ old = SelectObject(d->hDC, FontOf(h));
            UINT fmt = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS;
            DrawTextW(d->hDC, text, -1, &r, fmt);
            if (focus)
            {
                RECT t = r;
                DrawTextW(d->hDC, text, -1, &t, fmt | DT_CALCRECT);
                RECT f = { r.left, r.top, std::min(r.right, t.right + 2), r.bottom };
                FrameRect(d->hDC, &f, Brush(color::Dim));
            }
            SelectObject(d->hDC, old);
            return;
        }
        COLORREF fill, border, fore;
        if (!enabled) { fill = color::Panel; border = color::Line; fore = color::Dim; }
        else if (kind)
        {
            COLORREF base = kind == 2 ? color::Danger : color::Accent;
            fill = down ? Lighter(base, 0.3f) : hover ? Lighter(base, 0.15f) : base;
            border = base;
            fore = kind == 2 ? color::DangerText : color::AccentText;
        }
        else
        {
            fill = down ? color::ButtonDown : hover ? color::ButtonHover : color::Button;
            border = focus ? color::Dim : color::Border;
            fore = color::Text;
        }
        RECT r = d->rcItem;
        FillRect(d->hDC, &r, Brush(fill));
        FrameRect(d->hDC, &r, Brush(border));
        if (kind && enabled && focus)
        {
            // a primary or danger button: its border is its fill, so the focus
            // is a ring inside it in the text color
            RECT f = r;
            InflateRect(&f, -S(2), -S(2));
            FrameRect(d->hDC, &f, Brush(fore));
        }
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, fore);
        HGDIOBJ old = SelectObject(d->hDC, FontOf(h));
        InflateRect(&r, -S(4), 0);
        DrawTextW(d->hDC, text, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(d->hDC, old);
    }

    // a mouse message made by Windows from a touch or a pen: no hover for it
    // (a tap never "leaves", so the hover would stay on)
    static bool FromTouch()
    {
        return (GetMessageExtraInfo() & 0xFFFFFF00) == 0xFF515700;   // MI_WP_SIGNATURE
    }

    static void TrackHover(HWND h, UINT m)
    {
        if (m == WM_MOUSEMOVE && FromTouch()) return;
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

    // ---------- fields: edits, combo boxes, lists, the tree ----------
    // one frame for all of them: FieldBorder, Dim with focus, Border disabled

    static COLORREF FieldEdge(bool enabled, bool active)
    {
        return !enabled ? color::Border : active ? color::Dim : color::FieldBorder;
    }

    static void FrameField(HWND h, HDC dc)
    {
        RECT rc;
        GetWindowRect(h, &rc);
        OffsetRect(&rc, -rc.left, -rc.top);
        FrameRect(dc, &rc, Brush(FieldEdge(IsWindowEnabled(h) != 0, GetFocus() == h)));
    }

    static LRESULT CALLBACK FrameSub(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
    {
        switch (m)
        {
        case WM_NCPAINT:
        case WM_PAINT:   // an edit draws its border with its text, not in WM_NCPAINT
        {
            LRESULT r = DefSubclassProc(h, m, wp, lp);
            HDC dc = GetWindowDC(h);
            FrameField(h, dc);
            ReleaseDC(h, dc);
            return r;
        }
        case WM_PRINT:
        {
            LRESULT r = DefSubclassProc(h, m, wp, lp);
            if (lp & PRF_NONCLIENT) FrameField(h, (HDC)wp);
            return r;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE:
        {
            LRESULT r = DefSubclassProc(h, m, wp, lp);
            SendMessageW(h, WM_NCPAINT, 1, 0);
            return r;
        }
        }
        return DefSubclassProc(h, m, wp, lp);
    }

    void FieldFrame(HWND h) { SetWindowSubclass(h, FrameSub, 7, 0); }

    // a combo box drawn here like an edit (the theme would draw it grey with a
    // light frame): the field, the frame, the text of a drop-down list and the
    // arrow. The edit part of an editable one paints itself.
    static void PaintCombo(HWND h, HDC target)
    {
        RECT rc;
        GetClientRect(h, &rc);
        COMBOBOXINFO ci = { sizeof ci };
        GetComboBoxInfo(h, &ci);
        // a drop-down list reports itself as its "item": only an editable one has an edit part
        if ((GetWindowLongW(h, GWL_STYLE) & 3) == CBS_DROPDOWNLIST || ci.hwndItem == h) ci.hwndItem = nullptr;
        HWND focus = GetFocus();
        bool en = IsWindowEnabled(h) != 0;
        bool active = focus == h || (ci.hwndItem && focus == ci.hwndItem) || SendMessageW(h, CB_GETDROPPEDSTATE, 0, 0) || hovered == h;
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bmp = CreateCompatibleBitmap(target, std::max(1L, rc.right), std::max(1L, rc.bottom));
        HGDIOBJ oldBmp = SelectObject(dc, bmp);
        FillRect(dc, &rc, Brush(color::Panel));
        FrameRect(dc, &rc, Brush(FieldEdge(en, active)));
        if (!ci.hwndItem)
        {
            int sel = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
            if (sel >= 0)
            {
                int len = (int)SendMessageW(h, CB_GETLBTEXTLEN, sel, 0);
                std::vector<wchar_t> text(std::max(0, len) + 1, 0);
                if (len > 0) SendMessageW(h, CB_GETLBTEXT, sel, (LPARAM)text.data());
                RECT tr = { S(3) + 1, 0, ci.rcButton.left > 0 ? ci.rcButton.left : rc.right - S(20), rc.bottom };
                SetBkMode(dc, TRANSPARENT);
                SetTextColor(dc, en ? color::Text : color::Dim);
                HGDIOBJ of = SelectObject(dc, FontOf(h));
                DrawTextW(dc, text.data(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                SelectObject(dc, of);
            }
        }
        // the arrow: a chevron in the middle of the button part
        RECT b = ci.rcButton;
        if (b.right <= b.left) b = { rc.right - S(20), 0, rc.right, rc.bottom };
        int cx = (b.left + b.right) / 2, cy = (b.top + b.bottom) / 2, half = S(4);
        HPEN pen = CreatePen(PS_SOLID, std::max(1, S(1)), en ? color::Dim : color::Border);
        HGDIOBJ op = SelectObject(dc, pen);
        POINT p[3] = { { cx - half, cy - half / 2 }, { cx, cy + half / 2 }, { cx + half + 1, cy - half / 2 - 1 } };
        Polyline(dc, p, 3);
        SelectObject(dc, op);
        DeleteObject(pen);
        if (ci.hwndItem)
        {
            // leave the edit part alone: it paints itself
            RECT er;
            GetWindowRect(ci.hwndItem, &er);
            MapWindowPoints(nullptr, h, (POINT*)&er, 2);
            ExcludeClipRect(target, er.left, er.top, er.right, er.bottom);
        }
        BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
        DeleteDC(dc);
    }

    static LRESULT CALLBACK ComboSub(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR)
    {
        switch (m)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            PaintCombo(h, dc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_PRINTCLIENT:
            PaintCombo(h, (HDC)wp);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
            TrackHover(h, m);
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_ENABLE:
        case WM_SETTEXT:
        case CB_SETCURSEL:
        case CB_RESETCONTENT:
        {
            LRESULT r = DefSubclassProc(h, m, wp, lp);
            InvalidateRect(h, nullptr, FALSE);
            return r;
        }
        case WM_COMMAND:
        {
            // the edit part of an editable combo box got or lost the focus
            LRESULT r = DefSubclassProc(h, m, wp, lp);
            if (HIWORD(wp) == EN_SETFOCUS || HIWORD(wp) == EN_KILLFOCUS) InvalidateRect(h, nullptr, FALSE);
            return r;
        }
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
        case WM_SIZE:   // a combo box selects all its text when it is resized
        case WM_WINDOWPOSCHANGED:
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

    // ---------- scrolling host ----------

    // the bars, the range and the inner panel for the host's size; a bar
    // shown or hidden changes the size (WM_SIZE again: busy keeps it out)
    static void UpdateScroll(HWND v)
    {
        auto it = scrollers.find(v);
        if (it == scrollers.end() || it->second.busy) return;
        ScrollState& s = it->second;
        s.busy = true;
        RECT wr;
        GetWindowRect(v, &wr);
        int W = wr.right - wr.left, H = wr.bottom - wr.top;
        int minW = S(s.minW), minH = S(s.minH);
        int sbw = GetSystemMetrics(SM_CXVSCROLL), sbh = GetSystemMetrics(SM_CYHSCROLL);
        bool needH = W < minW, needV = H < minH;
        if (needH && !needV && H - sbh < minH) needV = true;
        if (needV && !needH && W - sbw < minW) needH = true;
        ShowScrollBar(v, SB_HORZ, needH);
        ShowScrollBar(v, SB_VERT, needV);
        int cw = std::max(0, W - (needV ? sbw : 0)), ch = std::max(0, H - (needH ? sbh : 0));
        int iw = std::max(cw, minW), ih = std::max(ch, minH);
        s.x = std::max(0, std::min(s.x, iw - cw));
        s.y = std::max(0, std::min(s.y, ih - ch));
        if (needH) { SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, iw - 1, (UINT)cw, s.x }; SetScrollInfo(v, SB_HORZ, &si, TRUE); }
        if (needV) { SCROLLINFO si = { sizeof si, SIF_RANGE | SIF_PAGE | SIF_POS, 0, ih - 1, (UINT)ch, s.y }; SetScrollInfo(v, SB_VERT, &si, TRUE); }
        if (!needH) s.x = 0;
        if (!needV) s.y = 0;
        SetWindowPos(s.inner, nullptr, -s.x, -s.y, iw, ih, SWP_NOZORDER | SWP_NOACTIVATE);
        s.busy = false;
    }

    static void OnScroll(HWND v, bool vertical, int code)
    {
        ScrollState& s = scrollers[v];
        SCROLLINFO si = { sizeof si, SIF_ALL };
        GetScrollInfo(v, vertical ? SB_VERT : SB_HORZ, &si);
        int pos = vertical ? s.y : s.x;
        switch (code)
        {
        case SB_LINEUP: pos -= S(24); break;
        case SB_LINEDOWN: pos += S(24); break;
        case SB_PAGEUP: pos -= (int)si.nPage; break;
        case SB_PAGEDOWN: pos += (int)si.nPage; break;
        case SB_THUMBTRACK: case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = si.nMax; break;
        default: return;
        }
        (vertical ? s.y : s.x) = pos;
        UpdateScroll(v);
    }

    void ScrollIntoView(HWND c)
    {
        for (HWND p = c ? GetParent(c) : nullptr; p; p = GetParent(p))
        {
            auto it = scrollers.find(p);
            if (it == scrollers.end()) continue;
            RECT r, cr;
            GetWindowRect(c, &r);
            MapWindowPoints(nullptr, p, (POINT*)&r, 2);
            GetClientRect(p, &cr);
            ScrollState& s = it->second;
            int m = S(8), x = s.x, y = s.y;
            if (r.bottom + m > cr.bottom) s.y += r.bottom + m - cr.bottom;
            if (r.top - m < s.y - y) s.y += r.top - m;
            if (r.right + m > cr.right) s.x += r.right + m - cr.right;
            if (r.left - m < s.x - x) s.x += r.left - m;
            if (s.x != x || s.y != y) UpdateScroll(p);
            return;
        }
    }

    void SetScrollMin(HWND inner, int minW, int minH)
    {
        for (auto& kv : scrollers)
            if (kv.second.inner == inner) { kv.second.minW = minW; kv.second.minH = minH; UpdateScroll(kv.first); }
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
        case WM_VSCROLL:
        case WM_HSCROLL:
            // the scroll bars of a scrolling host (lp: not a scroll bar control)
            if (!lp && scrollers.count(h)) { OnScroll(h, m == WM_VSCROLL, LOWORD(wp)); return 0; }
            break;
        case WM_MOUSEWHEEL:
            if (scrollers.count(h))
            {
                ScrollState& s = scrollers[h];
                int step = GET_WHEEL_DELTA_WPARAM(wp) * S(48) / WHEEL_DELTA;
                bool vert = (GetWindowLongPtrW(h, GWL_STYLE) & WS_VSCROLL) != 0;
                bool horz = (GetWindowLongPtrW(h, GWL_STYLE) & WS_HSCROLL) != 0;
                // the wheel scrolls up / down; with Shift (or without a vertical bar) left / right
                if (vert && !(GET_KEYSTATE_WPARAM(wp) & MK_SHIFT)) s.y -= step;
                else if (horz) s.x -= step;
                else break;
                UpdateScroll(h);
                return 0;
            }
            break;
        case WM_GETMINMAXINFO:
        {
            auto it = minSizes.find(h);
            if (it != minSizes.end())
            {
                RECT rc = { 0, 0, S(it->second.cx), S(it->second.cy) };
                AdjustWindowRectEx(&rc, (DWORD)GetWindowLongPtrW(h, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE));
                MINMAXINFO* mm = (MINMAXINFO*)lp;
                // a screen smaller than that (640 x 480 in a Windows PE): the screen
                MONITORINFO mi = { sizeof mi };
                GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi);
                mm->ptMinTrackSize.x = std::min(rc.right - rc.left, mi.rcWork.right - mi.rcWork.left);
                mm->ptMinTrackSize.y = std::min(rc.bottom - rc.top, mi.rcWork.bottom - mi.rcWork.top);
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
        bool checked = false, radio = false, down = false, key = false;   // down: mouse, key: Space held
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
        // pressed (mouse or Space held): the accent edge, a checked box lighter
        // (like a pressed primary button)
        POINT cur = {};
        GetCursorPos(&cur);
        ScreenToClient(h, &cur);
        bool press = en && (s->key || (s->down && PtInRect(&rc, cur)));
        COLORREF edge = !en ? color::Line : (s->checked || press ? color::Accent : hover ? color::Dim : color::FieldBorder);
        COLORREF mark = !en ? color::Dim : press ? Lighter(color::Accent, 0.3f) : color::Accent;
        if (s->radio)
        {
            HPEN pen = CreatePen(PS_SOLID, 1, edge);
            HGDIOBJ op = SelectObject(dc, pen), ob = SelectObject(dc, Brush(color::Panel));
            Ellipse(dc, b.left, b.top, b.right, b.bottom);
            if (s->checked)
            {
                SelectObject(dc, Brush(mark));
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
            FillRect(dc, &b, Brush(s->checked ? mark : color::Panel));
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
            FrameRect(dc, &fr, Brush(color::Dim));
        }
        SelectObject(dc, of);
        BitBlt(target, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, oldBmp);
        DeleteObject(bmp);
        DeleteDC(dc);
    }

    // what a screen reader is told of a check box / radio button: its role
    // and state (checked, unavailable, focused); the name is its text
    static void AccCheck(HWND h, bool notify)
    {
        CheckState* s = StateOf(h);
        if (!s) return;
        AccInt(h, PROPID_ACC_ROLE, s->radio ? ROLE_SYSTEM_RADIOBUTTON : ROLE_SYSTEM_CHECKBUTTON);
        int state = STATE_SYSTEM_FOCUSABLE;
        if (s->checked) state |= STATE_SYSTEM_CHECKED;
        if (!IsWindowEnabled(h)) state |= STATE_SYSTEM_UNAVAILABLE;
        if (GetFocus() == h) state |= STATE_SYSTEM_FOCUSED;
        AccInt(h, PROPID_ACC_STATE, state);
        if (notify) NotifyWinEvent(EVENT_OBJECT_STATECHANGE, h, OBJID_CLIENT, CHILDID_SELF);
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
            AccForget(h);
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
        case WM_KEYDOWN:
            if (wp != VK_SPACE) break;
            if (s && !s->key) { s->key = true; InvalidateRect(h, nullptr, FALSE); }
            return 0;
        case WM_KEYUP:
            if (wp == VK_SPACE && s && s->key)
            {
                s->key = false;
                InvalidateRect(h, nullptr, FALSE);
                UserToggle(h);
            }
            return 0;
        case WM_LBUTTONDOWN:
            SetFocus(h);
            SetCapture(h);
            if (s) s->down = true;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        case WM_LBUTTONUP:
            if (s && s->down)
            {
                s->down = false;
                ReleaseCapture();
                InvalidateRect(h, nullptr, FALSE);
                POINT p = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                RECT rc;
                GetClientRect(h, &rc);
                if (PtInRect(&rc, p)) UserToggle(h);
            }
            return 0;
        case WM_CAPTURECHANGED:
            if (s && s->down && (HWND)lp != h) { s->down = false; InvalidateRect(h, nullptr, FALSE); }
            return 0;
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
            TrackHover(h, m);
            if (s && s->down) InvalidateRect(h, nullptr, FALSE);   // pressed only while over it
            return 0;
        case WM_KILLFOCUS:
            if (s) s->key = false;
            InvalidateRect(h, nullptr, FALSE);
            AccCheck(h, false);
            return 0;
        case WM_SETFOCUS:
            InvalidateRect(h, nullptr, FALSE);
            AccCheck(h, false);
            return 0;
        case WM_ENABLE:
            InvalidateRect(h, nullptr, FALSE);
            AccCheck(h, true);
            return 0;
        case WM_SETTEXT:
        {
            LRESULT r = DefWindowProcW(h, m, wp, lp);
            InvalidateRect(h, nullptr, FALSE);
            NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, h, OBJID_CLIENT, CHILDID_SELF);
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
        AccCheck(h, true);
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
        // a short strip (a small screen): less room around the names, then
        // all of them narrower (drawn with "...")
        RECT cr;
        GetClientRect(h, &cr);
        int avail = std::max(0L, cr.right - S(4)), n = (int)s->names.size(), text = 0;
        std::vector<int> tw;
        for (const wstring& name : s->names) { tw.push_back(TextWidth(FontBold, name)); text += tw.back(); }
        int pad = S(14);
        if (n > 0 && text + 2 * pad * n > avail) pad = std::max(S(4), (avail - text) / (2 * n));
        double k = n > 0 && text + 2 * pad * n > avail ? std::max(0.0, (double)(avail - 2 * pad * n)) / text : 1.0;
        for (int i = 0; i < n; i++)
        {
            int w = (int)(tw[i] * k) + 2 * pad;
            r.push_back(RECT{ x, 0, x + w, head });
            x += w;
        }
        return r;
    }

    // the tab strip for a screen reader: a page tab list with one page tab per
    // name (children 1..n). The strip itself (parent, location) comes from the
    // standard object of the window.
    class TabsAccessible : public IAccessible
    {
        LONG refs = 1;
        HWND h;
        IAccessible* base = nullptr;

        TabsState* State() const { return IsWindow(h) ? TabsOf(h) : nullptr; }
        bool Child(const VARIANT& v, int& index) const
        {
            TabsState* s = State();
            if (!s || v.vt != VT_I4 || v.lVal < 1 || v.lVal > (long)s->names.size()) return false;
            index = v.lVal - 1;
            return true;
        }
        static bool Self(const VARIANT& v) { return v.vt == VT_I4 && v.lVal == CHILDID_SELF; }
        static void Int(VARIANT* out, long value) { out->vt = VT_I4; out->lVal = value; }

    public:
        TabsAccessible(HWND w) : h(w)
        {
            typedef HRESULT(WINAPI* CreateStdFn)(HWND, LONG, REFIID, void**);
            CreateStdFn create = Oleacc() ? (CreateStdFn)GetProcAddress(Oleacc(), "CreateStdAccessibleObject") : nullptr;
            if (create) create(h, OBJID_CLIENT, __uuidof(IAccessible), (void**)&base);
        }
        ~TabsAccessible() { if (base) base->Release(); }

        // IUnknown
        STDMETHODIMP QueryInterface(REFIID riid, void** out) override
        {
            if (riid == IID_IUnknown || riid == IID_IDispatch || riid == __uuidof(IAccessible)) { *out = this; AddRef(); return S_OK; }
            *out = nullptr;
            return E_NOINTERFACE;
        }
        STDMETHODIMP_(ULONG) AddRef() override { return InterlockedIncrement(&refs); }
        STDMETHODIMP_(ULONG) Release() override { LONG r = InterlockedDecrement(&refs); if (!r) delete this; return r; }

        // IDispatch: not used by screen readers
        STDMETHODIMP GetTypeInfoCount(UINT* n) override { *n = 0; return S_OK; }
        STDMETHODIMP GetTypeInfo(UINT, LCID, ITypeInfo**) override { return E_NOTIMPL; }
        STDMETHODIMP GetIDsOfNames(REFIID, LPOLESTR*, UINT, LCID, DISPID*) override { return E_NOTIMPL; }
        STDMETHODIMP Invoke(DISPID, REFIID, LCID, WORD, DISPPARAMS*, VARIANT*, EXCEPINFO*, UINT*) override { return E_NOTIMPL; }

        // IAccessible
        STDMETHODIMP get_accParent(IDispatch** out) override { *out = nullptr; return base ? base->get_accParent(out) : S_FALSE; }
        STDMETHODIMP get_accChildCount(long* n) override { TabsState* s = State(); if (!s) return E_FAIL; *n = (long)s->names.size(); return S_OK; }
        STDMETHODIMP get_accChild(VARIANT, IDispatch** out) override { *out = nullptr; return S_FALSE; }   // simple elements
        STDMETHODIMP get_accName(VARIANT v, BSTR* out) override
        {
            *out = nullptr;
            int i;
            if (Child(v, i)) { *out = SysAllocString(TabsOf(h)->names[i].c_str()); return S_OK; }
            return Self(v) ? S_FALSE : E_INVALIDARG;
        }
        STDMETHODIMP get_accValue(VARIANT, BSTR* out) override { *out = nullptr; return S_FALSE; }
        STDMETHODIMP get_accDescription(VARIANT, BSTR* out) override { *out = nullptr; return S_FALSE; }
        STDMETHODIMP get_accRole(VARIANT v, VARIANT* out) override
        {
            int i;
            if (Child(v, i)) { Int(out, ROLE_SYSTEM_PAGETAB); return S_OK; }
            if (Self(v)) { Int(out, ROLE_SYSTEM_PAGETABLIST); return S_OK; }
            return E_INVALIDARG;
        }
        STDMETHODIMP get_accState(VARIANT v, VARIANT* out) override
        {
            TabsState* s = State();
            if (!s) return E_FAIL;
            bool focused = GetFocus() == h;
            int i;
            if (Child(v, i))
            {
                long st = STATE_SYSTEM_SELECTABLE | STATE_SYSTEM_FOCUSABLE;
                if (i == s->sel) st |= STATE_SYSTEM_SELECTED | (focused ? STATE_SYSTEM_FOCUSED : 0);
                Int(out, st);
                return S_OK;
            }
            if (Self(v)) { Int(out, STATE_SYSTEM_FOCUSABLE | (focused ? STATE_SYSTEM_FOCUSED : 0)); return S_OK; }
            return E_INVALIDARG;
        }
        STDMETHODIMP get_accHelp(VARIANT, BSTR* out) override { *out = nullptr; return S_FALSE; }
        STDMETHODIMP get_accHelpTopic(BSTR* file, VARIANT, long* topic) override { *file = nullptr; *topic = 0; return S_FALSE; }
        STDMETHODIMP get_accKeyboardShortcut(VARIANT v, BSTR* out) override
        {
            *out = nullptr;
            int i;
            if (Child(v, i)) { *out = SysAllocString(L"Ctrl+Tab"); return S_OK; }
            return S_FALSE;
        }
        STDMETHODIMP get_accFocus(VARIANT* out) override
        {
            TabsState* s = State();
            if (!s) return E_FAIL;
            if (GetFocus() == h && s->sel >= 0) Int(out, s->sel + 1); else out->vt = VT_EMPTY;
            return S_OK;
        }
        STDMETHODIMP get_accSelection(VARIANT* out) override
        {
            TabsState* s = State();
            if (!s) return E_FAIL;
            if (s->sel >= 0) Int(out, s->sel + 1); else out->vt = VT_EMPTY;
            return S_OK;
        }
        STDMETHODIMP get_accDefaultAction(VARIANT v, BSTR* out) override
        {
            *out = nullptr;
            int i;
            if (Child(v, i)) { *out = SysAllocString(L"Switch"); return S_OK; }
            return S_FALSE;
        }
        STDMETHODIMP accSelect(long flags, VARIANT v) override
        {
            int i;
            if (!Child(v, i)) return E_INVALIDARG;
            if (flags & (SELFLAG_TAKEFOCUS)) SetFocus(h);
            if (flags & (SELFLAG_TAKESELECTION | SELFLAG_ADDSELECTION)) TabSelect(h, i);
            return S_OK;
        }
        STDMETHODIMP accLocation(long* x, long* y, long* w, long* hh, VARIANT v) override
        {
            int i;
            if (Child(v, i))
            {
                RECT r = TabRects(h)[i];
                MapWindowPoints(h, nullptr, (POINT*)&r, 2);
                *x = r.left; *y = r.top; *w = r.right - r.left; *hh = r.bottom - r.top;
                return S_OK;
            }
            return base ? base->accLocation(x, y, w, hh, v) : E_FAIL;
        }
        STDMETHODIMP accNavigate(long dir, VARIANT start, VARIANT* out) override
        {
            TabsState* s = State();
            if (!s) return E_FAIL;
            out->vt = VT_EMPTY;
            long n = (long)s->names.size();
            int i;
            if (Child(start, i))
            {
                long to = 0;
                if (dir == NAVDIR_NEXT || dir == NAVDIR_RIGHT) to = i + 2;
                else if (dir == NAVDIR_PREVIOUS || dir == NAVDIR_LEFT) to = i;
                if (to >= 1 && to <= n) { Int(out, to); return S_OK; }
                return S_FALSE;
            }
            if (Self(start) && n > 0)
            {
                if (dir == NAVDIR_FIRSTCHILD) { Int(out, 1); return S_OK; }
                if (dir == NAVDIR_LASTCHILD) { Int(out, n); return S_OK; }
            }
            return base ? base->accNavigate(dir, start, out) : S_FALSE;
        }
        STDMETHODIMP accHitTest(long x, long y, VARIANT* out) override
        {
            POINT p = { x, y };
            ScreenToClient(h, &p);
            std::vector<RECT> r = TabRects(h);
            for (size_t i = 0; i < r.size(); i++) if (PtInRect(&r[i], p)) { Int(out, (long)i + 1); return S_OK; }
            return base ? base->accHitTest(x, y, out) : S_FALSE;
        }
        STDMETHODIMP accDoDefaultAction(VARIANT v) override
        {
            int i;
            if (!Child(v, i)) return E_INVALIDARG;
            TabSelect(h, i);
            return S_OK;
        }
        STDMETHODIMP put_accName(VARIANT, BSTR) override { return E_NOTIMPL; }
        STDMETHODIMP put_accValue(VARIANT, BSTR) override { return E_NOTIMPL; }
    };

    // Ctrl+Tab / Ctrl+Shift+Tab, Ctrl+PgDn / Ctrl+PgUp: the next / previous tab
    // of the window the key was pressed in (from any control in it)
    static std::vector<HWND> tabStrips;

    static bool TabKeys(const MSG& m)
    {
        if (m.message != WM_KEYDOWN || GetKeyState(VK_CONTROL) >= 0 || GetKeyState(VK_MENU) < 0) return false;
        int step = 0;
        if (m.wParam == VK_TAB) step = GetKeyState(VK_SHIFT) < 0 ? -1 : 1;
        else if (m.wParam == VK_NEXT) step = 1;
        else if (m.wParam == VK_PRIOR) step = -1;
        if (!step) return false;
        HWND root = GetAncestor(m.hwnd, GA_ROOT);
        for (HWND t : tabStrips)
        {
            if (!IsWindowVisible(t) || GetAncestor(t, GA_ROOT) != root) continue;
            TabsState* s = TabsOf(t);
            int n = (int)s->names.size();
            if (n == 0) return false;
            TabSelect(t, ((s->sel + step) % n + n) % n);
            return true;
        }
        return false;
    }

    static LRESULT CALLBACK TabsProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
    {
        TabsState* s = TabsOf(h);
        switch (m)
        {
        case WM_NCCREATE:
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)new TabsState());
            tabStrips.push_back(h);
            break;
        case WM_NCDESTROY:
            tabStrips.erase(std::remove(tabStrips.begin(), tabStrips.end(), h), tabStrips.end());
            delete s;
            SetWindowLongPtrW(h, GWLP_USERDATA, 0);
            break;
        case WM_GETOBJECT:
            if ((LONG)lp == OBJID_CLIENT && Oleacc())
            {
                typedef LRESULT(WINAPI* ResultFn)(REFIID, WPARAM, LPUNKNOWN);
                ResultFn result = (ResultFn)GetProcAddress(Oleacc(), "LresultFromObject");
                if (result)
                {
                    TabsAccessible* acc = new TabsAccessible(h);
                    LRESULT r = result(__uuidof(IAccessible), wp, acc);
                    acc->Release();
                    return r;
                }
            }
            break;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS;
        case WM_KEYDOWN:
        {
            // Left / Right: the previous / next tab, Home / End: the first / last
            int n = (int)s->names.size(), to = -1;
            if (wp == VK_LEFT) to = std::max(0, s->sel - 1);
            else if (wp == VK_RIGHT) to = std::min(n - 1, s->sel + 1);
            else if (wp == VK_HOME) to = 0;
            else if (wp == VK_END) to = n - 1;
            else break;
            if (to >= 0) TabSelect(h, to);
            return 0;
        }
        case WM_SETFOCUS:
            InvalidateRect(h, nullptr, FALSE);
            if (s->sel >= 0) NotifyWinEvent(EVENT_OBJECT_FOCUS, h, OBJID_CLIENT, s->sel + 1);
            return 0;
        case WM_KILLFOCUS:
        case WM_UPDATEUISTATE:
            InvalidateRect(h, nullptr, FALSE);
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
            if (FromTouch()) return 0;
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
                SetTextColor(dc, sel ? color::Strong : ((int)i == s->hover ? color::Text : color::Dim));
                HGDIOBJ of = SelectObject(dc, sel ? FontBold : FontUI);
                DrawTextW(dc, s->names[i].c_str(), -1, &r[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
                SelectObject(dc, of);
                if (sel)
                {
                    RECT u = { r[i].left + S(4), head - S(2), r[i].right - S(4), head };
                    FillRect(dc, &u, Brush(color::Accent));
                    // keyboard focus: a frame around the selected tab
                    if (GetFocus() == h)
                    {
                        RECT f = { r[i].left + S(2), r[i].top + S(3), r[i].right - S(2), head - S(4) };
                        FrameRect(dc, &f, Brush(color::Dim));
                    }
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
        NotifyWinEvent(EVENT_OBJECT_SELECTION, h, OBJID_CLIENT, index + 1);
        if (GetFocus() == h) NotifyWinEvent(EVENT_OBJECT_FOCUS, h, OBJID_CLIENT, index + 1);
        Fire(h, false);
    }

    // ---------- progress line ----------

    struct ProgressState
    {
        int value = 0, phase = 0;
        bool marquee = false, still = false;   // still: a marquee without motion
    };

    // "Show animations in Windows" (Settings > Accessibility > Visual effects)
    static bool Animations()
    {
        BOOL on = TRUE;
        if (!SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &on, 0)) return true;
        return on != FALSE;
    }

    static COLORREF Mix(COLORREF a, COLORREF b, float t)
    {
        auto m = [t](int x, int y) { return (int)(x + (y - x) * t + 0.5f); };
        return RGB(m(GetRValue(a), GetRValue(b)), m(GetGValue(a), GetGValue(b)), m(GetBValue(a), GetBValue(b)));
    }

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
            if (s->marquee && s->still)
            {
                // without animations: the whole bar, half toned (busy, no amount)
                FillRect(dc, &rc, Brush(Mix(color::Line, color::Accent, 0.4f)));
                EndPaint(h, &ps);
                return 0;
            }
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
        if (v != s->value)
        {
            s->value = v;
            InvalidateRect(p, nullptr, FALSE);
            if (!s->marquee)
            {
                AccStr(p, PROPID_ACC_VALUE, std::to_wstring(v) + L"%");
                NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, p, OBJID_CLIENT, CHILDID_SELF);
            }
        }
    }

    void ProgressMarquee(HWND p, bool on)
    {
        ProgressState* s = ProgressOf(p);
        if (s->marquee == on) return;
        s->marquee = on;
        s->still = on && !Animations();
        if (on && !s->still) ::SetTimer(p, 1, 30, nullptr); else ::KillTimer(p, 1);
        InvalidateRect(p, nullptr, FALSE);
        // busy without an amount: no value for a screen reader
        AccStr(p, PROPID_ACC_VALUE, on ? wstring() : std::to_wstring(s->value) + L"%");
        if (on) AccInt(p, PROPID_ACC_STATE, STATE_SYSTEM_BUSY | STATE_SYSTEM_READONLY);
        else if (IAccPropServices* a = AccProps()) { MSAAPROPID st = PROPID_ACC_STATE; a->ClearHwndProps(p, (DWORD)OBJID_CLIENT, CHILDID_SELF, &st, 1); }
        NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, p, OBJID_CLIENT, CHILDID_SELF);
    }

    // ---------- menus ----------
    // Owner-drawn items (WM_MEASUREITEM / WM_DRAWITEM go to a hidden owner
    // window), the menu background from SetMenuInfo, and the frame of the
    // menu window (#32768, caught by a CBT hook while it is created) painted
    // in WM_NCPAINT / WM_PRINT. Works without the theme service (WinPE).

    static const int MenuPadX = 14, MenuGapKey = 36;

    static void MenuFrame(HWND h, HDC dc)
    {
        RECT wr, cr;
        GetWindowRect(h, &wr);
        GetClientRect(h, &cr);
        MapWindowPoints(h, nullptr, (POINT*)&cr, 2);
        OffsetRect(&cr, -wr.left, -wr.top);
        OffsetRect(&wr, -wr.left, -wr.top);
        int saved = SaveDC(dc);
        ExcludeClipRect(dc, cr.left, cr.top, cr.right, cr.bottom);
        FillRect(dc, &wr, Brush(color::Panel));
        HBRUSH border = Brush(color::Border);
        FrameRect(dc, &wr, border);
        RestoreDC(dc, saved);
    }

    static LRESULT CALLBACK MenuWindowSub(HWND h, UINT m, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR)
    {
        switch (m)
        {
        case WM_NCPAINT:
        {
            HDC dc = GetWindowDC(h);
            MenuFrame(h, dc);
            ReleaseDC(h, dc);
            return 0;
        }
        case WM_PRINT:
        {
            // the fade animation draws the menu through WM_PRINT
            LRESULT r = DefSubclassProc(h, m, wp, lp);
            if (lp & PRF_NONCLIENT) MenuFrame(h, (HDC)wp);
            return r;
        }
        case WM_NCDESTROY:
            RemoveWindowSubclass(h, MenuWindowSub, id);
            break;
        }
        return DefSubclassProc(h, m, wp, lp);
    }

    static HHOOK menuHook;

    static LRESULT CALLBACK MenuHook(int code, WPARAM wp, LPARAM lp)
    {
        if (code == HCBT_CREATEWND)
        {
            HWND h = (HWND)wp;
            wchar_t cls[16] = {};
            GetClassNameW(h, cls, 16);
            if (wcscmp(cls, L"#32768") == 0)
            {
                SetWindowSubclass(h, MenuWindowSub, 1, 0);
                // Windows 11: the color of the rounded frame (ignored before)
                COLORREF c = color::Border;
                DwmSetWindowAttribute(h, 34 /* DWMWA_BORDER_COLOR */, &c, sizeof c);
            }
        }
        return CallNextHookEx(menuHook, code, wp, lp);
    }

    static LRESULT CALLBACK MenuOwnerProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
    {
        switch (m)
        {
        case WM_MEASUREITEM:
        {
            MEASUREITEMSTRUCT* mi = (MEASUREITEMSTRUCT*)lp;
            if (mi->CtlType != ODT_MENU) break;
            const MenuItem* it = (const MenuItem*)mi->itemData;
            if (!it || (it->id == 0 && it->text.empty()))
            {
                mi->itemWidth = 0;
                mi->itemHeight = S(9);
                return TRUE;
            }
            int w = S(MenuPadX) + TextWidth(FontUI, it->text) + S(MenuPadX);
            if (!it->key.empty()) w += S(MenuGapKey) + TextWidth(FontUI, it->key);
            mi->itemWidth = (UINT)w;
            mi->itemHeight = (UINT)std::max(TextHeight(FontUI) + S(10), S(26));
            return TRUE;
        }
        case WM_DRAWITEM:
        {
            const DRAWITEMSTRUCT* d = (const DRAWITEMSTRUCT*)lp;
            if (d->CtlType != ODT_MENU) break;
            const MenuItem* it = (const MenuItem*)d->itemData;
            HDC dc = d->hDC;
            RECT r = d->rcItem;
            FillRect(dc, &r, Brush(color::Panel));
            if (!it || (it->id == 0 && it->text.empty()))
            {
                RECT line = { r.left + S(8), (r.top + r.bottom) / 2, r.right - S(8), (r.top + r.bottom) / 2 + 1 };
                FillRect(dc, &line, Brush(color::Border));
                return TRUE;
            }
            bool grey = (d->itemState & (ODS_GRAYED | ODS_DISABLED)) != 0;
            bool hot = (d->itemState & ODS_SELECTED) != 0 && !grey;
            if (hot)
            {
                RECT sel = { r.left + S(3), r.top + S(1), r.right - S(3), r.bottom - S(1) };
                FillRect(dc, &sel, Brush(color::Selected));
            }
            HGDIOBJ old = SelectObject(dc, FontUI);
            SetBkMode(dc, TRANSPARENT);
            RECT t = { r.left + S(MenuPadX), r.top, r.right - S(MenuPadX), r.bottom };
            SetTextColor(dc, grey ? color::Dim : hot ? color::Strong : color::Text);
            DrawTextW(dc, it->text.c_str(), -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            if (!it->key.empty())
            {
                SetTextColor(dc, grey ? color::Dim : hot ? color::Strong : color::Dim);
                DrawTextW(dc, it->key.c_str(), -1, &t, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            }
            SelectObject(dc, old);
            return TRUE;
        }
        }
        return DefWindowProcW(h, m, wp, lp);
    }

    int PopupMenu(HWND owner, POINT pt, const std::vector<MenuItem>& items)
    {
        if (items.empty()) return 0;
        HWND host = CreateWindowExW(WS_EX_TOOLWINDOW, MenuClass, L"", WS_POPUP, 0, 0, 0, 0, owner, nullptr, Inst, nullptr);
        if (!host) return 0;
        HMENU m = CreatePopupMenu();
        for (const MenuItem& it : items)
        {
            UINT flags = MF_OWNERDRAW;
            if (it.id == 0 && it.text.empty()) flags |= MF_SEPARATOR;
            else if (!it.enabled || it.id == 0) flags |= MF_GRAYED;
            AppendMenuW(m, flags, (UINT_PTR)it.id, (LPCWSTR)&it);
        }
        MENUINFO mi = { sizeof mi };
        mi.fMask = MIM_BACKGROUND | MIM_APPLYTOSUBMENUS;
        mi.hbrBack = Brush(color::Panel);
        SetMenuInfo(m, &mi);
        menuHook = SetWindowsHookExW(WH_CBT, MenuHook, nullptr, GetCurrentThreadId());
        int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, host, nullptr);
        if (menuHook) { UnhookWindowsHookEx(menuHook); menuHook = nullptr; }
        DestroyMenu(m);
        DestroyWindow(host);
        return cmd;
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
        wc.lpfnWndProc = MenuOwnerProc;
        wc.lpszClassName = MenuClass;
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
        BOOL on = DarkMode ? TRUE : FALSE;
        // DWMWA_USE_IMMERSIVE_DARK_MODE: 20 (Windows 10 2004+), 19 before
        if (FAILED(DwmSetWindowAttribute(top, 20, &on, sizeof on))) DwmSetWindowAttribute(top, 19, &on, sizeof on);
    }

    int MessageLoop()
    {
        MSG msg = {};
        HWND focus = nullptr;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            HWND top = GetAncestor(msg.hwnd, GA_ROOT);
            if (TabKeys(msg)) continue;
            if (!(top && IsDialogMessageW(top, &msg)))
            {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            // the focus moved (Tab): a control outside a scrolled page comes into view
            HWND f = GetFocus();
            if (f != focus) { focus = f; if (f && !scrollers.empty()) ScrollIntoView(f); }
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
        FitToWorkArea(win);
        return win;
    }

    bool FitsWorkArea(int w, int h)
    {
        RECT rc = { 0, 0, S(w), S(h) }, work;
        AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT);
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        return rc.right - rc.left <= work.right - work.left && rc.bottom - rc.top <= work.bottom - work.top;
    }

    void FitToWorkArea(HWND top)
    {
        MONITORINFO mi = { sizeof mi };
        GetMonitorInfoW(MonitorFromWindow(top, MONITOR_DEFAULTTONEAREST), &mi);
        RECT work = mi.rcWork, wr;
        GetWindowRect(top, &wr);
        int w = std::min(wr.right - wr.left, work.right - work.left);
        int h = std::min(wr.bottom - wr.top, work.bottom - work.top);
        int x = std::max(work.left, std::min(wr.left, work.right - w));
        int y = std::max(work.top, std::min(wr.top, work.bottom - h));
        if (x != wr.left || y != wr.top || w != wr.right - wr.left || h != wr.bottom - wr.top)
            SetWindowPos(top, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    HWND ScrollHost(HWND parent, int minW, int minH, HWND& inner)
    {
        HWND v = CreateWindowExW(WS_EX_CONTROLPARENT, PanelClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            0, 0, 10, 10, parent, nullptr, Inst, nullptr);
        backColors[v] = color::Back;
        // dark scroll bars in the dark theme
        SetWindowTheme(v, ThemeApp(L"DarkMode_Explorer"), nullptr);
        inner = Panel(v, color::Back);
        ScrollState s;
        s.inner = inner;
        s.minW = minW;
        s.minH = minH;
        scrollers[v] = s;
        layouts[v] = [v](int, int) { UpdateScroll(v); };
        return v;
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
        // an owner-drawn button drawn as a link (DrawButton, kind 3): a tab stop,
        // Space / Enter open it, a screen reader knows it as a button
        HWND h = Button(parent, text);
        buttonKinds[h] = 3;
        foreColors[h] = color::AccentSoft;
        SetWindowSubclass(h, LinkSub, 2, 0);
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
        SetWindowTheme(h, style & ES_MULTILINE ? ThemeApp(L"DarkMode_Explorer") : L"", style & ES_MULTILINE ? nullptr : L"");
        FieldFrame(h);
        return h;
    }

    HWND Combo(HWND parent, bool editable)
    {
        HWND h = Child(L"COMBOBOX", parent, L"", WS_TABSTOP | WS_VSCROLL | CBS_AUTOHSCROLL | (editable ? CBS_DROPDOWN : CBS_DROPDOWNLIST));
        SetWindowTheme(h, ThemeApp(L"DarkMode_CFD"), nullptr);   // still for the drop-down list
        if (editable) SetWindowSubclass(h, EditComboSub, 1, 0);
        SetWindowSubclass(h, ComboSub, 2, 0);
        return h;
    }

    HWND Number(HWND parent, int min, int max, int value)
    {
        HWND e = Edit(parent, ES_NUMBER);
        HWND u = CreateWindowExW(0, UPDOWN_CLASSW, L"", WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ARROWKEYS | UDS_NOTHOUSANDS | UDS_HOTTRACK,
            0, 0, 10, 10, parent, nullptr, Inst, nullptr);
        SetWindowTheme(u, ThemeApp(L"DarkMode_Explorer"), nullptr);
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
        if (!wimsmith::TryParseLong(GetText(h), v)) return lo;
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
        AccCheck(h, false);
        return h;
    }

    HWND Check(HWND parent, const wstring& text) { return CheckCore(parent, text, false, 0); }
    HWND Radio(HWND parent, const wstring& text, int group) { return CheckCore(parent, text, true, group); }

    HWND Tabs(HWND parent)
    {
        // a tab stop: the arrow keys switch the tabs (Ctrl+Tab from anywhere)
        HWND h = CreateWindowExW(0, TabsClass, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0, 10, 10, parent, nullptr, Inst, nullptr);
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
            SetTextColor(dc, color::Strong);
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
        SetWindowTheme(h, ThemeApp(L"DarkMode_Explorer"), nullptr);
        HWND header = ListView_GetHeader(h);
        if (header) SetWindowTheme(header, ThemeApp(L"DarkMode_ItemsView"), nullptr);
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
        FieldFrame(h);
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
        HWND h = CreateWindowExW(0, ProgressClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, nullptr, Inst, nullptr);
        // a screen reader: a progress bar with its percentage as the value
        AccInt(h, PROPID_ACC_ROLE, ROLE_SYSTEM_PROGRESSBAR);
        AccStr(h, PROPID_ACC_VALUE, L"0%");
        return h;
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

    // without a window (the boot-once of the right-click menu): to the front
    static UINT Front(HWND owner) { return owner ? 0 : MB_SETFOREGROUND | MB_TOPMOST; }

    void Info(const wstring& text, UINT icon) { HWND o = ActiveOwner(); MessageBoxW(o, text.c_str(), L"WimSmith", MB_OK | icon | Front(o)); }

    bool YesNo(const wstring& text, UINT icon)
    {
        HWND o = ActiveOwner();
        return MessageBoxW(o, text.c_str(), L"WimSmith", MB_YESNO | icon | Front(o)) == IDYES;
    }

    // a task dialog without an owner: in front, like MB_SETFOREGROUND | MB_TOPMOST
    static HRESULT CALLBACK ConfirmFront(HWND h, UINT n, WPARAM, LPARAM, LONG_PTR)
    {
        if (n == TDN_CREATED)
        {
            SetWindowPos(h, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            SetForegroundWindow(h);
        }
        return S_OK;
    }

    bool Confirm(const wstring& text, const wstring& action, const wstring& cancel, UINT icon)
    {
        // TaskDialogIndirect: comctl32 6 only, looked up at run time (no import
        // that could stop the exe from starting); without it a Yes / No box
        typedef HRESULT(WINAPI* TaskDialogFn)(const TASKDIALOGCONFIG*, int*, int*, BOOL*);
        HMODULE cc = GetModuleHandleW(L"comctl32.dll");
        TaskDialogFn dialog = cc ? (TaskDialogFn)GetProcAddress(cc, "TaskDialogIndirect") : nullptr;
        if (!dialog) return YesNo(text, icon);
        HWND o = ActiveOwner();
        const int Act = 100;
        TASKDIALOG_BUTTON buttons[] = { { Act, action.c_str() }, { IDCANCEL, cancel.c_str() } };
        TASKDIALOGCONFIG c = { sizeof c };
        c.hwndParent = o;
        c.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | (o ? TDF_POSITION_RELATIVE_TO_WINDOW : 0);
        c.pszWindowTitle = L"WimSmith";
        c.pszMainIcon = (icon & MB_ICONMASK) == MB_ICONWARNING ? TD_WARNING_ICON
            : (icon & MB_ICONMASK) == MB_ICONERROR ? TD_ERROR_ICON : TD_INFORMATION_ICON;
        c.pszContent = text.c_str();
        c.cButtons = 2;
        c.pButtons = buttons;
        c.nDefaultButton = IDCANCEL;   // Enter does not destroy anything
        if (!o) c.pfCallback = ConfirmFront;
        int pressed = 0;
        if (FAILED(dialog(&c, &pressed, nullptr, nullptr))) return YesNo(text, icon);
        return pressed == Act;
    }

    void OnKeys(HWND top, Fn ok, Fn cancel) { keyHandlers[top] = std::make_pair(ok, cancel); }

    void EndModal(HWND dlg, int result) { modalResults[dlg] = result; }

    HWND Dialog(const wstring& title, int w, int h)
    {
        HWND d = TopWindow(title, w, h, Main, WS_POPUP | WS_CAPTION | WS_SYSMENU);
        RECT o, me;
        // no main window: in the middle of the screen
        if (!Main || !GetWindowRect(Main, &o)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &o, 0);
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
        FitToWorkArea(dlg);
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
        ForgetTree(dlg);
        DestroyWindow(dlg);
        if (owner) SetActiveWindow(owner);
        if (quit) PostQuitMessage((int)msg.wParam);
        return result;
    }

    // (the file dialogs are in filedlg.cpp)

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
        // the label just before the first control of its row in the z order:
        // a screen reader names a field after the label before it (the
        // controls are made before the row, so the label would come last)
        HWND first = r.items.empty() ? nullptr : r.items.front().h;
        if (!label.empty() && first && GetParent(first) == parent)
        {
            HWND above = GetWindow(first, GW_HWNDPREV);
            SetWindowPos(r.label, above ? above : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
            // a field later in the row with no text of its own before it (after
            // a check box, as in "Drivers: [x] Add with DISM [folder]"): the row label
            for (size_t i = 1; i < r.items.size(); i++)
            {
                HWND c = r.items[i].h;
                wstring cls = c ? ClassOf(c) : wstring();
                bool field = _wcsicmp(cls.c_str(), L"Edit") == 0 || _wcsicmp(cls.c_str(), L"ComboBox") == 0;
                bool labelled = ClassOf(r.items[i - 1].h) == L"Static";
                if (field && !labelled) AccName(c, label);
            }
        }
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
