// ============================================================
//  7z_wimlib - Libs.cs
//  The third-party programs in <exe folder>\lib:
//    libwim-15.dll                 wimlib (x64), backup / restore /
//                                  export / info
//    7z.exe, 7z.dll, 7zFM.exe      7-Zip (x64), the browse tab
//  Migrate moves a libwim-15.dll of the old places (pe_tools\, the exe
//  folder) into lib\. A missing program is downloaded in its newest
//  version (EnsureWimlib / Ensure7zip, with a small progress window):
//    wimlib : the newest wimlib-<ver>-windows-x86_64-bin.zip linked on
//             https://wimlib.net/ (libwim-15.dll and the COPYING files)
//    7-Zip  : the newest 7z<ver>-x64.exe linked on
//             https://www.7-zip.org/download.html (else the latest
//             GitHub release of ip7z/7zip); the installer is unpacked
//             with 7zr.exe of the same place, nothing is installed.
//  lib\versions.ini records the downloaded versions.
//  The license texts go next to the programs (wimlib_COPYING*.txt,
//  7-Zip_License.txt); FetchMissingLicenses gets only the texts, in the
//  background, for a program that is there without them.
// ============================================================
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

namespace Dvt
{
    public static class Libs
    {
        public const string SevenZipExe = "7z.exe", SevenZipDll = "7z.dll", SevenZipFm = "7zFM.exe";
        const string UserAgent = "7z_wimlib";

        public static string Dir(string root) { return Path.Combine(root, "lib"); }
        public static string WimlibDll(string root) { return Path.Combine(Dir(root), WimLib.DllName); }
        public static string SevenZip(string root) { return Path.Combine(Dir(root), SevenZipExe); }
        public static string SevenZipManager(string root) { return Path.Combine(Dir(root), SevenZipFm); }

        public static bool HasWimlib(string root) { return File.Exists(WimlibDll(root)); }
        public static bool Has7zip(string root)
        {
            return File.Exists(SevenZip(root)) && File.Exists(Path.Combine(Dir(root), SevenZipDll));
        }

        // libwim-15.dll of pe_tools\ or of the exe folder -> lib\ (once)
        public static void Migrate(string root)
        {
            try
            {
                if (HasWimlib(root)) return;
                foreach (string dir in new string[] { Path.Combine(root, "pe_tools"), root })
                {
                    string old = Path.Combine(dir, WimLib.DllName);
                    if (!File.Exists(old)) continue;
                    Directory.CreateDirectory(Dir(root));
                    File.Move(old, WimlibDll(root));
                    // an empty pe_tools folder is not needed any more
                    if (dir != root && Directory.GetFileSystemEntries(dir).Length == 0) Directory.Delete(dir);
                    return;
                }
            }
            catch (Exception) { }     // the download is the fallback
        }

        public static string Version(string root, string key)
        {
            try
            {
                string f = Path.Combine(Dir(root), "versions.ini");
                if (!File.Exists(f)) return "";
                foreach (string l in File.ReadAllLines(f))
                {
                    int eq = l.IndexOf('=');
                    if (eq > 0 && l.Substring(0, eq).Trim().Equals(key, StringComparison.OrdinalIgnoreCase)) return l.Substring(eq + 1).Trim();
                }
            }
            catch (Exception) { }
            return "";
        }

        static void SetVersion(string root, string key, string value)
        {
            string f = Path.Combine(Dir(root), "versions.ini");
            List<string> lines = new List<string>();
            if (File.Exists(f)) lines.AddRange(File.ReadAllLines(f));
            lines.RemoveAll(delegate(string l)
            {
                int eq = l.IndexOf('=');
                return eq > 0 && l.Substring(0, eq).Trim().Equals(key, StringComparison.OrdinalIgnoreCase);
            });
            lines.Add(key + " = " + value);
            File.WriteAllLines(f, lines.ToArray());
        }

        // ---------- the downloads (null = ready, else the reason) ----------

        // libwim-15.dll in lib\, downloaded when missing
        public static string EnsureWimlib(IWin32Window owner, string root)
        {
            Migrate(root);
            if (HasWimlib(root)) return null;
            return DownloadDialog.Run(owner, L.T("lib.title_wimlib"), delegate(DownloadProgress p) { return DownloadWimlib(root, p); });
        }

        // 7z.exe + 7z.dll (+ 7zFM.exe) in lib\, downloaded when missing
        public static string Ensure7zip(IWin32Window owner, string root)
        {
            if (Has7zip(root)) return null;
            return DownloadDialog.Run(owner, L.T("lib.title_7zip"), delegate(DownloadProgress p) { return Download7zip(root, p); });
        }

        // ---------- license texts ----------
        // The license texts belong next to the programs (wimlib: GPLv3 / LGPL,
        // libdivsufsort-lite; 7-Zip: LGPL + BSD + unRAR restriction). A program
        // that came without them (libwim-15.dll moved from pe_tools\, files put
        // there by hand) gets them here: the same download, but only the texts
        // are unpacked; the DLL / exe files are not touched.

        public static bool HasWimlibLicense(string root)
        {
            try { return Directory.GetFiles(Dir(root), "wimlib_COPYING*").Length > 0; }
            catch (Exception) { return false; }
        }

        public static bool Has7zipLicense(string root)
        {
            return File.Exists(Path.Combine(Dir(root), "7-Zip_License.txt"));
        }

        // in the background, without a window; a failure (no internet) is
        // tried again at the next start
        static bool licensesChecked;          // once per process (MainForm is made again after a language change)

        public static void FetchMissingLicenses(string root)
        {
            if (licensesChecked) return;
            licensesChecked = true;
            bool wim = HasWimlib(root) && !HasWimlibLicense(root);
            bool sz = Has7zip(root) && !Has7zipLicense(root);
            if (!wim && !sz) return;
            Thread t = new Thread(delegate()
            {
                DownloadProgress p = new DownloadProgress();
                if (wim) try { DownloadWimlib(root, p, true); } catch (Exception) { }
                if (sz) try { Download7zip(root, p, true); } catch (Exception) { }
            });
            t.IsBackground = true;
            t.Start();
        }

        static string DownloadWimlib(string root, DownloadProgress p) { return DownloadWimlib(root, p, false); }

        // licenseOnly: only the COPYING files (the DLL stays as it is)
        static string DownloadWimlib(string root, DownloadProgress p, bool licenseOnly)
        {
            p.Text(L.T("lib.finding"));
            string page = GetText("https://wimlib.net/", p);
            string best = null;
            System.Version bestVer = null;
            foreach (Match m in Regex.Matches(page, @"href=""([^""]*wimlib-([0-9][0-9.]*[0-9])-windows-x86_64-bin\.zip)"""))
            {
                System.Version v;
                if (!System.Version.TryParse(m.Groups[2].Value, out v)) continue;
                if (bestVer == null || v > bestVer) { bestVer = v; best = m.Groups[1].Value; }
            }
            if (best == null) throw new Exception("No wimlib-<version>-windows-x86_64-bin.zip link on https://wimlib.net/");
            string url = new Uri(new Uri("https://wimlib.net/"), best).ToString();

            string tmp = TempDir();
            try
            {
                string zip = Path.Combine(tmp, "wimlib.zip");
                p.Text(L.F("lib.downloading", "wimlib " + bestVer));
                GetFile(url, zip, p);
                p.Text(L.F("lib.unpacking", "wimlib " + bestVer));
                string dir = Dir(root);
                Directory.CreateDirectory(dir);
                bool found = false;
                using (ZipArchive z = ZipFile.OpenRead(zip))
                    foreach (ZipArchiveEntry e in z.Entries)
                    {
                        // only the DLL and the license texts (no subfolders)
                        if (e.FullName.IndexOf('/') >= 0) continue;
                        if (e.Name.Equals(WimLib.DllName, StringComparison.OrdinalIgnoreCase))
                        {
                            found = true;
                            if (licenseOnly) continue;
                            string part = WimlibDll(root) + ".part";
                            e.ExtractToFile(part, true);
                            if (File.Exists(WimlibDll(root))) File.Delete(WimlibDll(root));
                            File.Move(part, WimlibDll(root));
                        }
                        else if (e.Name.StartsWith("COPYING", StringComparison.OrdinalIgnoreCase))
                            e.ExtractToFile(Path.Combine(dir, "wimlib_" + e.Name), true);
                    }
                if (!found) throw new Exception(WimLib.DllName + " is not in " + url);
                if (!licenseOnly) SetVersion(root, "wimlib", bestVer.ToString());
                return null;
            }
            finally { DeleteDir(tmp); }
        }

        static string Download7zip(string root, DownloadProgress p) { return Download7zip(root, p, false); }

        // licenseOnly: only License.txt (7z.exe / 7z.dll / 7zFM.exe stay as they are)
        static string Download7zip(string root, DownloadProgress p, bool licenseOnly)
        {
            p.Text(L.T("lib.finding"));
            string installer = null, sevenZr = null, version = null;
            try
            {
                const string site = "https://www.7-zip.org/download.html";
                string page = GetText(site, p);
                // the page lists the newest version first
                Match m = Regex.Match(page, @"href=""([^""]*7z(\d{4})-x64\.exe)""");
                Match r = Regex.Match(page, @"href=""([^""]*7zr\.exe)""");
                if (m.Success && r.Success)
                {
                    installer = new Uri(new Uri(site), m.Groups[1].Value).ToString();
                    sevenZr = new Uri(new Uri(site), r.Groups[1].Value).ToString();
                    version = m.Groups[2].Value;
                }
            }
            catch (Exception) { if (p.Cancelled) throw; }
            if (installer == null)
            {
                // the GitHub releases of 7-Zip
                string json = GetText("https://api.github.com/repos/ip7z/7zip/releases/latest", p);
                foreach (Match m in Regex.Matches(json, @"""browser_download_url""\s*:\s*""([^""]+)"""))
                {
                    string u = m.Groups[1].Value;
                    Match v = Regex.Match(u, @"/7z(\d{4})-x64\.exe$");
                    if (v.Success) { installer = u; version = v.Groups[1].Value; }
                    else if (u.EndsWith("/7zr.exe")) sevenZr = u;
                }
                if (installer == null || sevenZr == null) throw new Exception("No 7-Zip x64 download found (7-zip.org, GitHub ip7z/7zip).");
            }
            string shown = version.Length == 4 ? version.Substring(0, 2) + "." + version.Substring(2) : version;

            string tmp = TempDir();
            try
            {
                string zr = Path.Combine(tmp, "7zr.exe"), inst = Path.Combine(tmp, "7z-x64.exe"), outDir = Path.Combine(tmp, "out");
                p.Text(L.F("lib.downloading", "7zr.exe"));
                GetFile(sevenZr, zr, p);
                p.Text(L.F("lib.downloading", "7-Zip " + shown));
                GetFile(installer, inst, p);
                p.Text(L.F("lib.unpacking", "7-Zip " + shown));
                // the installer is a 7z archive: unpack the needed files, run nothing of it
                ProcessStartInfo psi = new ProcessStartInfo(zr, "x \"" + inst + "\" -o\"" + outDir + "\" -y "
                    + (licenseOnly ? "" : SevenZipExe + " " + SevenZipDll + " " + SevenZipFm + " ") + "License.txt");
                psi.UseShellExecute = false;
                psi.CreateNoWindow = true;
                psi.RedirectStandardOutput = true;
                psi.RedirectStandardError = true;
                string output;
                int code;
                using (Process pr = Process.Start(psi))
                {
                    Thread errReader = new Thread(delegate() { pr.StandardError.ReadToEnd(); });
                    errReader.Start();
                    output = pr.StandardOutput.ReadToEnd();
                    pr.WaitForExit();
                    errReader.Join();
                    code = pr.ExitCode;
                }
                if (code != 0 || (!licenseOnly && (!File.Exists(Path.Combine(outDir, SevenZipExe)) || !File.Exists(Path.Combine(outDir, SevenZipDll)))))
                    throw new Exception("7zr.exe could not unpack " + installer + " (code " + code + ").\r\n" + output.Trim());
                string dir = Dir(root);
                Directory.CreateDirectory(dir);
                if (!licenseOnly)
                {
                    foreach (string f in new string[] { SevenZipDll, SevenZipFm, SevenZipExe })
                    {
                        string src = Path.Combine(outDir, f);
                        if (File.Exists(src)) File.Copy(src, Path.Combine(dir, f), true);
                    }
                }
                string lic = Path.Combine(outDir, "License.txt");
                if (File.Exists(lic)) File.Copy(lic, Path.Combine(dir, "7-Zip_License.txt"), true);
                else if (licenseOnly) throw new Exception("License.txt is not in " + installer);
                if (!licenseOnly) SetVersion(root, "7zip", shown);
                return null;
            }
            finally { DeleteDir(tmp); }
        }

        // ---------- HTTP ----------

        static HttpWebRequest Request(string url, DownloadProgress p)
        {
            // TLS 1.2 (and 1.3 where Windows has it) for wimlib.net, 7-zip.org and GitHub
            try { ServicePointManager.SecurityProtocol |= (SecurityProtocolType)3072 | (SecurityProtocolType)12288; }
            catch (Exception) { ServicePointManager.SecurityProtocol |= (SecurityProtocolType)3072; }
            HttpWebRequest rq = (HttpWebRequest)WebRequest.Create(url);
            rq.UserAgent = UserAgent;
            rq.AllowAutoRedirect = true;
            rq.Timeout = 30000;
            rq.ReadWriteTimeout = 60000;
            p.Request = rq;
            return rq;
        }

        static string GetText(string url, DownloadProgress p)
        {
            HttpWebRequest rq = Request(url, p);
            using (WebResponse rs = rq.GetResponse())
            using (StreamReader r = new StreamReader(rs.GetResponseStream(), Encoding.UTF8))
                return r.ReadToEnd();
        }

        static void GetFile(string url, string file, DownloadProgress p)
        {
            HttpWebRequest rq = Request(url, p);
            using (WebResponse rs = rq.GetResponse())
            using (Stream s = rs.GetResponseStream())
            using (FileStream f = new FileStream(file, FileMode.Create, FileAccess.Write))
            {
                long total = rs.ContentLength, done = 0;
                byte[] buf = new byte[65536];
                int n;
                while ((n = s.Read(buf, 0, buf.Length)) > 0)
                {
                    if (p.Cancelled) throw new OperationCanceledException();
                    f.Write(buf, 0, n);
                    done += n;
                    p.Percent(total > 0 ? (int)(done * 100 / total) : -1);
                }
            }
        }

        static string TempDir()
        {
            string d = Path.Combine(Path.GetTempPath(), "7z_wimlib_download_" + DateTime.Now.ToString("yyyyMMdd-HHmmss-fff"));
            Directory.CreateDirectory(d);
            return d;
        }

        static void DeleteDir(string d)
        {
            try { if (Directory.Exists(d)) Directory.Delete(d, true); } catch (Exception) { }
        }
    }

    // progress of a download, reported from the worker thread
    public class DownloadProgress
    {
        public volatile bool Cancelled;
        public volatile HttpWebRequest Request;
        public Action<string> OnText;
        public Action<int> OnPercent;
        public void Text(string t) { if (OnText != null) OnText(t); }
        public void Percent(int pct) { if (OnPercent != null) OnPercent(pct); }
    }

    // a small modal window while a download runs in the background
    public static class DownloadDialog
    {
        // work returns null (done) or the reason; the result is that, or the exception text
        public static string Run(IWin32Window owner, string title, Func<DownloadProgress, string> work)
        {
            using (Form f = new Form())
            {
                f.Text = title;
                f.Font = Ui.FontUI;
                f.FormBorderStyle = FormBorderStyle.FixedDialog;
                f.StartPosition = owner != null ? FormStartPosition.CenterParent : FormStartPosition.CenterScreen;
                f.MinimizeBox = false;
                f.MaximizeBox = false;
                f.ShowInTaskbar = owner == null;
                f.ClientSize = new Size(Ui.S(460), Ui.S(120));
                Label l = new Label();
                l.SetBounds(Ui.S(14), Ui.S(12), Ui.S(432), Ui.S(36));
                l.Text = L.T("lib.finding");
                FlatProgress bar = new FlatProgress();
                bar.SetBounds(Ui.S(14), Ui.S(60), Ui.S(432), Ui.S(4));
                bar.Style = ProgressBarStyle.Marquee;
                Button cancel = new Button();
                cancel.Text = L.T("bk.btn_cancel");
                cancel.SetBounds(Ui.S(346), Ui.S(82), Ui.S(100), Ui.S(28));
                f.Controls.AddRange(new Control[] { l, bar, cancel });

                DownloadProgress p = new DownloadProgress();
                string result = null;
                bool finished = false;
                p.OnText = delegate(string t) { Post(f, delegate { l.Text = t; bar.Style = ProgressBarStyle.Marquee; }); };
                p.OnPercent = delegate(int pct)
                {
                    Post(f, delegate
                    {
                        if (pct < 0) { bar.Style = ProgressBarStyle.Marquee; return; }
                        bar.Style = ProgressBarStyle.Blocks;
                        bar.Value = Math.Max(0, Math.Min(100, pct));
                    });
                };
                cancel.Click += delegate
                {
                    p.Cancelled = true;
                    cancel.Enabled = false;
                    HttpWebRequest rq = p.Request;
                    if (rq != null) try { rq.Abort(); } catch (Exception) { }
                };
                f.FormClosing += delegate(object s, FormClosingEventArgs e)
                {
                    if (finished) return;
                    e.Cancel = true;      // wait for the worker; [X] = cancel
                    cancel.PerformClick();
                };
                f.Shown += delegate
                {
                    Thread t = new Thread(delegate()
                    {
                        string r;
                        try { r = work(p); }
                        catch (Exception ex) { r = p.Cancelled ? L.T("lib.cancelled") : ex.Message; }
                        Post(f, delegate { result = r; finished = true; f.Close(); });
                    });
                    t.IsBackground = true;
                    t.Start();
                };
                Theme.Apply(f);
                f.ShowDialog(owner);
                return result;
            }
        }

        static void Post(Control c, MethodInvoker a)
        {
            try { if (c.IsHandleCreated) c.BeginInvoke(a); } catch (Exception) { }
        }
    }
}
