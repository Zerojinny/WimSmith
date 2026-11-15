// ============================================================
//  WimSmith - gather.cpp
//  One window for the items of one menu click (gather.h).
// ============================================================
#include "gather.h"
#include "ui.h"

namespace wimsmith
{
    namespace gather
    {
        static const ULONG_PTR CopyId = 0x57494D31;   // "WIM1"
        static HANDLE mutex;
        static HWND receiver;
        static std::function<bool(const std::vector<wstring>&)> onItems;

        static wstring MutexName(const wstring& name) { return L"Local\\WimSmith." + name; }
        static wstring ReceiverClass(const wstring& name) { return L"WimSmith." + name + L".Receiver"; }

        static bool Send(const wstring& name, const std::vector<wstring>& items)
        {
            HWND w = FindWindowExW(HWND_MESSAGE, nullptr, ReceiverClass(name).c_str(), nullptr);
            if (!w) return false;
            wstring text;
            for (const wstring& f : items) text += f + L"\n";
            COPYDATASTRUCT cd = { CopyId, (DWORD)((text.size() + 1) * sizeof(wchar_t)), (void*)text.c_str() };
            DWORD_PTR r = 0;
            return SendMessageTimeoutW(w, WM_COPYDATA, 0, (LPARAM)&cd, SMTO_ABORTIFHUNG, 5000, &r) && r == TRUE;
        }

        bool Forward(const wstring& name, const std::vector<wstring>& items)
        {
            // the first process makes the mutex; the others wait for its window
            // (it may still be starting, or busy) for about 10 s
            for (int n = 0; n < 200; n++)
            {
                HANDLE m = CreateMutexW(nullptr, FALSE, MutexName(name).c_str());
                if (m && GetLastError() != ERROR_ALREADY_EXISTS) { mutex = m; return false; }
                if (m) CloseHandle(m);
                if (Send(name, items)) return true;
                Sleep(50);
            }
            return false;
        }

        static LRESULT CALLBACK ReceiverProc(HWND h, UINT msg, WPARAM w, LPARAM l)
        {
            if (msg == WM_COPYDATA)
            {
                const COPYDATASTRUCT* cd = (const COPYDATASTRUCT*)l;
                if (!cd || cd->dwData != CopyId || cd->cbData < sizeof(wchar_t) || !onItems) return FALSE;
                wstring text((const wchar_t*)cd->lpData, cd->cbData / sizeof(wchar_t) - 1);
                std::vector<wstring> list;
                for (const wstring& s : Split(text, L'\n')) if (!s.empty()) list.push_back(s);
                return onItems(list) ? TRUE : FALSE;
            }
            return DefWindowProcW(h, msg, w, l);
        }

        void Receive(const wstring& name, std::function<bool(const std::vector<wstring>&)> f)
        {
            onItems = f;
            wstring cls = ReceiverClass(name);
            WNDCLASSW wc = {};
            wc.lpfnWndProc = ReceiverProc;
            wc.hInstance = ui::Inst;
            wc.lpszClassName = cls.c_str();
            RegisterClassW(&wc);
            receiver = CreateWindowExW(0, cls.c_str(), L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, ui::Inst, nullptr);
        }

        void Stop()
        {
            onItems = nullptr;
            if (receiver) { DestroyWindow(receiver); receiver = nullptr; }
            if (mutex) { CloseHandle(mutex); mutex = nullptr; }
        }
    }
}
