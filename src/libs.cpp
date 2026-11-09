// ============================================================
//  WimSmith - libs.cpp
//  lib\: moving and downloading wimlib (see libs.h).
// ============================================================
#include "libs.h"
#include "lang.h"
#include "ui.h"
#include "unzip.h"
#include <winhttp.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <regex>
#include <thread>

namespace wimsmith
{
    namespace libs
    {
        static const wchar_t* const WimlibDllName = L"libwim-15.dll";
        static const wchar_t* const UserAgent = L"WimSmith";

        wstring Dir(const wstring& root) { return Combine(root, L"lib"); }
        static wstring WimlibDll(const wstring& root) { return Combine(Dir(root), WimlibDllName); }
        bool HasWimlib(const wstring& root) { return FileExists(WimlibDll(root)); }

        // libwim-15.dll of pe_tools\ or of the exe folder -> lib\ (once)
        void Migrate(const wstring& root)
        {
            if (HasWimlib(root)) return;
            for (const wstring& dir : { Combine(root, L"pe_tools"), root })
            {
                wstring old = Combine(dir, WimlibDllName);
                if (!FileExists(old)) continue;
                try
                {
                    CreateDirs(Dir(root));
                    MoveFileOrThrow(old, WimlibDll(root));
                    // an empty pe_tools folder is not needed any more
                    if (dir != root && !DirHasEntries(dir)) RemoveDirectoryW(dir.c_str());
                }
                catch (const HelperError&) {}   // the download is the fallback
                return;
            }
        }

        static void SetVersion(const wstring& root, const wstring& key, const wstring& value)
        {
            wstring f = Combine(Dir(root), L"versions.ini");
            std::vector<wstring> lines;
            if (FileExists(f))
            {
                try { lines = SplitLines(ReadTextFile(f)); } catch (const HelperError&) {}
            }
            wstring text;
            for (const wstring& l : lines)
            {
                size_t eq = l.find(L'=');
                if (Trim(l).empty() || (eq != wstring::npos && eq > 0 && EqualsI(Trim(l.substr(0, eq)), key))) continue;
                text += l + L"\r\n";
            }
            text += key + L" = " + value + L"\r\n";
            WriteBytes(f, ToUtf8(text));
        }

        wstring WimlibVersion(const wstring& root)
        {
            wstring f = Combine(Dir(root), L"versions.ini");
            if (!FileExists(f)) return L"";
            try
            {
                for (const wstring& l : SplitLines(ReadTextFile(f)))
                {
                    size_t eq = l.find(L'=');
                    if (eq != wstring::npos && EqualsI(Trim(l.substr(0, eq)), L"wimlib")) return Trim(l.substr(eq + 1));
                }
            }
            catch (const HelperError&) {}
            return L"";
        }

        // ---------- progress of a download ----------

        struct Progress
        {
            std::atomic<bool> cancelled{ false };
            std::function<void(const wstring&)> onText;
            std::function<void(int)> onPercent;
            void Text(const wstring& t) { if (onText) onText(t); }
            void Percent(int p) { if (onPercent) onPercent(p); }
        };

        struct Cancelled {};

        // ---------- HTTP (WinHTTP) ----------

        struct Http
        {
            HINTERNET h = nullptr;
            explicit Http(HINTERNET x) : h(x) {}
            ~Http() { if (h) WinHttpCloseHandle(h); }
        };

        // the body of url; to a file when file is given (else returned)
        static std::string Get(const wstring& url, Progress& p, const wstring& file = wstring())
        {
            URL_COMPONENTS uc = { sizeof uc };
            wchar_t host[256] = { 0 }, path[2048] = { 0 };
            uc.lpszHostName = host;
            uc.dwHostNameLength = 256;
            uc.lpszUrlPath = path;
            uc.dwUrlPathLength = 2048;
            uc.dwSchemeLength = 1;
            uc.dwExtraInfoLength = 1;
            if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) throw HelperError{ L"Not a URL: " + url };
            wstring object = path;
            if (uc.lpszExtraInfo && uc.dwExtraInfoLength) object += wstring(uc.lpszExtraInfo, uc.dwExtraInfoLength);
            Http session(WinHttpOpen(UserAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
            if (!session.h) session.h = WinHttpOpen(UserAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
            if (!session.h) throw HelperError{ L"WinHttpOpen: " + ErrorMessage(GetLastError()) };
            DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
            protocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
            if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols))
            {
                protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
                WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof protocols);
            }
            WinHttpSetTimeouts(session.h, 30000, 30000, 30000, 60000);
            Http connect(WinHttpConnect(session.h, host, uc.nPort, 0));
            if (!connect.h) throw HelperError{ url + L": " + ErrorMessage(GetLastError()) };
            Http request(WinHttpOpenRequest(connect.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
            if (!request.h) throw HelperError{ url + L": " + ErrorMessage(GetLastError()) };
            if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                || !WinHttpReceiveResponse(request.h, nullptr))
            {
                DWORD e = GetLastError();
                if (p.cancelled) throw Cancelled();
                throw HelperError{ url + L": " + ErrorMessage(e) };
            }
            DWORD status = 0, size = sizeof status;
            WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
            if (status != 200) throw HelperError{ url + L": HTTP " + Num(status) };
            wchar_t lenText[32] = { 0 };
            DWORD lenSize = sizeof lenText;
            long long total = -1;
            if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenText, &lenSize, WINHTTP_NO_HEADER_INDEX))
                total = ParseLong(lenText, -1);

            HANDLE f = INVALID_HANDLE_VALUE;
            if (!file.empty())
            {
                f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (f == INVALID_HANDLE_VALUE) throw HelperError{ file + L": " + ErrorMessage(GetLastError()) };
            }
            std::string body;
            long long done = 0;
            std::vector<char> buf(65536);
            DWORD got = 0;
            bool ok = true;
            while (true)
            {
                if (p.cancelled) { ok = false; break; }
                if (!WinHttpReadData(request.h, buf.data(), (DWORD)buf.size(), &got)) { ok = false; break; }
                if (got == 0) break;
                if (f != INVALID_HANDLE_VALUE)
                {
                    DWORD put = 0;
                    if (!WriteFile(f, buf.data(), got, &put, nullptr) || put != got) { ok = false; break; }
                }
                else body.append(buf.data(), got);
                done += got;
                p.Percent(total > 0 ? (int)(done * 100 / total) : -1);
            }
            DWORD e = GetLastError();
            if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
            if (p.cancelled) throw Cancelled();
            if (!ok) throw HelperError{ url + L": " + ErrorMessage(e) };
            return body;
        }

        // an absolute URL from a link of a page
        static wstring Resolve(const wstring& base, const wstring& link)
        {
            if (StartsWithI(link, L"http://") || StartsWithI(link, L"https://")) return link;
            size_t hostEnd = base.find(L'/', base.find(L"://") + 3);
            wstring origin = hostEnd == wstring::npos ? base : base.substr(0, hostEnd);
            if (!link.empty() && link[0] == L'/') return origin + link;
            size_t slash = base.rfind(L'/');
            return (slash == wstring::npos || slash < origin.size() ? origin + L"/" : base.substr(0, slash + 1)) + link;
        }

        static wstring TempDir()
        {
            wchar_t tmp[MAX_PATH + 1];
            GetTempPathW(MAX_PATH + 1, tmp);
            SYSTEMTIME t;
            GetLocalTime(&t);
            wchar_t stamp[64];
            swprintf_s(stamp, L"WimSmith_download_%04d%02d%02d-%02d%02d%02d-%03d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
            wstring d = Combine(tmp, stamp);
            CreateDirs(d);
            return d;
        }

        struct TempFolder
        {
            wstring path;
            TempFolder() : path(TempDir()) {}
            ~TempFolder() { try { if (DirExists(path)) DeleteTree(path); } catch (const HelperError&) {} }
        };

        // "1.14.4" > "1.9.2"
        static bool NewerVersion(const std::string& a, const std::string& b)
        {
            std::vector<long long> x, y;
            for (const wstring& s : Split(FromUtf8(a), L'.')) x.push_back(ParseLong(s, 0));
            for (const wstring& s : Split(FromUtf8(b), L'.')) y.push_back(ParseLong(s, 0));
            return std::lexicographical_compare(y.begin(), y.end(), x.begin(), x.end());
        }

        // ---------- wimlib ----------

        // licenseOnly: only the COPYING files (the DLL stays as it is)
        static void DownloadWimlib(const wstring& root, Progress& p, bool licenseOnly)
        {
            p.Text(L::T(L"lib.finding"));
            std::string page = Get(L"https://wimlib.net/", p);
            std::regex rx("href=\"([^\"]*wimlib-([0-9][0-9.]*[0-9])-windows-x86_64-bin\\.zip)\"");
            std::string best, bestVer;
            for (std::sregex_iterator it(page.begin(), page.end(), rx), end; it != end; ++it)
                if (bestVer.empty() || NewerVersion((*it)[2].str(), bestVer)) { bestVer = (*it)[2].str(); best = (*it)[1].str(); }
            if (best.empty()) throw HelperError{ L"No wimlib-<version>-windows-x86_64-bin.zip link on https://wimlib.net/" };
            wstring url = Resolve(L"https://wimlib.net/", FromUtf8(best));
            wstring ver = FromUtf8(bestVer);

            TempFolder tmp;
            wstring zip = Combine(tmp.path, L"wimlib.zip"), outDir = Combine(tmp.path, L"out");
            p.Text(L::F(L"lib.downloading", L"wimlib " + ver));
            Get(url, p, zip);
            p.Text(L::F(L"lib.unpacking", L"wimlib " + ver));
            CreateDirs(outDir);
            // only the DLL and the license texts of the top folder (unzip.cpp: no other program)
            unzip::ExtractTop(zip, outDir, [licenseOnly](const wstring& name)
            {
                return StartsWithI(name, L"COPYING") || (!licenseOnly && EqualsI(name, WimlibDllName));
            });
            wstring dll = Combine(outDir, WimlibDllName);
            if (!licenseOnly && !FileExists(dll)) throw HelperError{ wstring(WimlibDllName) + L" is not in " + url };
            wstring dir = Dir(root);
            CreateDirs(dir);
            // only the DLL and the license texts (the top folder of the zip)
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(Combine(outDir, L"COPYING*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE)
            {
                do
                    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                        CopyFileW(Combine(outDir, fd.cFileName).c_str(), Combine(dir, wstring(L"wimlib_") + fd.cFileName).c_str(), FALSE);
                while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            if (licenseOnly) return;
            wstring part = WimlibDll(root) + L".part";
            if (!CopyFileW(dll.c_str(), part.c_str(), FALSE)) throw HelperError{ part + L": " + ErrorMessage(GetLastError()) };
            DeleteFileOrThrow(WimlibDll(root));
            MoveFileOrThrow(part, WimlibDll(root));
            SetVersion(root, L"wimlib", ver);
        }

        // ---------- the window while a download runs ----------

        // work runs in the background; "" = done, else the reason
        static wstring RunDialog(const wstring& title, std::function<void(Progress&)> work)
        {
            using namespace ui;
            HWND d = Dialog(title, 460, 120);
            HWND l = Label(d, L::T(L"lib.finding"));
            HWND bar = ui::Progress(d);
            HWND cancel = Button(d, L::T(L"bk.btn_cancel"));
            Move(l, S(14), S(12), S(432), S(36));
            Move(bar, S(14), S(60), S(432), S(4));
            Move(cancel, S(346), S(82), S(100), S(28));
            ProgressMarquee(bar, true);
            std::shared_ptr<Progress> p(new Progress());
            std::shared_ptr<wstring> result(new wstring());
            std::shared_ptr<bool> finished(new bool(false));
            p->onText = [d, l, bar](const wstring& t) { Post(d, [l, bar, t] { SetText(l, t); ProgressMarquee(bar, true); }); };
            p->onPercent = [d, bar](int pct)
            {
                Post(d, [bar, pct]
                {
                    if (pct < 0) { ProgressMarquee(bar, true); return; }
                    ProgressMarquee(bar, false);
                    ProgressValue(bar, pct);
                });
            };
            auto doCancel = [p, cancel] { p->cancelled = true; Enable(cancel, false); };
            OnClick(cancel, doCancel);
            // [X] / Esc = cancel; the window waits for the worker
            OnClose(d, [doCancel, finished] { if (!*finished) doCancel(); return false; });
            OnKeys(d, nullptr, doCancel);
            std::thread([d, p, result, finished, work]
            {
                wstring r;
                try { work(*p); }
                catch (const Cancelled&) { r = L::T(L"lib.cancelled"); }
                catch (const HelperError& e) { r = p->cancelled ? L::T(L"lib.cancelled") : e.msg; }
                catch (const std::exception& e) { r = FromUtf8(e.what()); }
                Post(d, [d, result, finished, r] { *result = r; *finished = true; EndModal(d, 1); });
            }).detach();
            RunModal(d);
            return *result;
        }

        wstring EnsureWimlib(const wstring& root)
        {
            Migrate(root);
            if (HasWimlib(root)) return wstring();
            return RunDialog(L::T(L"lib.title_wimlib"), [root](Progress& p) { DownloadWimlib(root, p, false); });
        }

        // ---------- license texts ----------
        // A program that came without its license texts (libwim-15.dll moved
        // from pe_tools\, files put there by hand) gets them here: the same
        // download, but only the texts are unpacked.

        static bool HasWimlibLicense(const wstring& root)
        {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(Combine(Dir(root), L"wimlib_COPYING*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) return false;
            FindClose(h);
            return true;
        }

        static bool licensesChecked;          // once per process (the window is made again after a language change)

        // in the background, without a window; a failure (no internet) is tried again at the next start
        void FetchMissingLicenses(const wstring& root)
        {
            if (licensesChecked) return;
            licensesChecked = true;
            if (!HasWimlib(root) || HasWimlibLicense(root)) return;
            std::thread([root]
            {
                Progress p;
                try { DownloadWimlib(root, p, true); } catch (...) {}
            }).detach();
        }
    }
}
