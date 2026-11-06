// ============================================================
//  Z_wimlib - Program.cs
//  Entry point of Z_wimlib.exe, the WIM backup / restore tool taken
//  from the DVT GUI. It works from any folder:
//    <exe folder>\lib\libwim-15.dll        wimlib (x64)    } downloaded when
//    <exe folder>\lib\7z.exe, 7z.dll ...   7-Zip (x64)     } missing (Libs.cs)
//    <exe folder>\lang\*.ini               optional, else the embedded copy
//    <exe folder>\Z_wimlib.ini             settings:
//        language     = <code>     (without it: Korean on a Korean
//                                   Windows, else English)
//        image_folder = <folder>   folder of the last backup
//  Z_wimlib.exe --wim-helper <work folder> is the elevated helper
//  (see WimLib.cs); it opens no window.
// ============================================================
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Globalization;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace Dvt
{
    static class Program
    {
        static string exeDir = "";

        [STAThread]
        static int Main(string[] args)
        {
            if (args.Length > 0 && args[0] == "--wim-helper")
                return WimLib.RunHelper(args);

            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);

            // the manifest declares DPI awareness: scale the layout sizes
            try
            {
                using (Graphics g = Graphics.FromHwnd(IntPtr.Zero)) Ui.Scale = g.DpiX / 96f;
            }
            catch (Exception) { Ui.Scale = 1f; }

            exeDir = Path.GetFullPath(AppDomain.CurrentDomain.BaseDirectory).TrimEnd('\\');
            L.Init(exeDir);
            while (true)
            {
                L.Load(GetSetting("language", DefaultLanguage()));
                Ui.InitFonts(L.FontFamily);
                if (L.Problems.Count > 0)
                    MessageBox.Show(string.Join("\r\n", L.Problems.ToArray()), "Z_wimlib", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                MainForm form = new MainForm(exeDir);
                Application.Run(form);
                bool again = form.RestartRequested;
                form.Dispose();
                if (!again) break;
            }
            return 0;
        }

        static string DefaultLanguage()
        {
            return CultureInfo.CurrentUICulture.TwoLetterISOLanguageName == "ko" ? "ko" : "en";
        }

        static string SettingsFile { get { return Path.Combine(exeDir, "Z_wimlib.ini"); } }

        // value of "key = value" in Z_wimlib.ini, or fallback
        public static string GetSetting(string key, string fallback)
        {
            try
            {
                if (!File.Exists(SettingsFile)) return fallback;
                foreach (string l in File.ReadAllLines(SettingsFile, Encoding.UTF8))
                {
                    string t = l.Trim();
                    if (t.StartsWith(";") || t.StartsWith("#")) continue;
                    int eq = t.IndexOf('=');
                    if (eq > 0 && t.Substring(0, eq).Trim().Equals(key, StringComparison.OrdinalIgnoreCase))
                    {
                        string v = t.Substring(eq + 1).Trim();
                        return v.Length > 0 ? v : fallback;
                    }
                }
            }
            catch (Exception) { }
            return fallback;
        }

        // sets "key = value" in Z_wimlib.ini; the other lines stay
        public static void SetSetting(string key, string value)
        {
            try
            {
                List<string> lines = new List<string>();
                if (File.Exists(SettingsFile)) lines.AddRange(File.ReadAllLines(SettingsFile, Encoding.UTF8));
                bool done = false;
                for (int n = 0; n < lines.Count; n++)
                {
                    string t = lines[n].Trim();
                    int eq = t.IndexOf('=');
                    if (!t.StartsWith(";") && eq > 0 && t.Substring(0, eq).Trim().Equals(key, StringComparison.OrdinalIgnoreCase))
                    {
                        lines[n] = key + " = " + value;
                        done = true;
                    }
                }
                if (!done) lines.Add(key + " = " + value);
                File.WriteAllLines(SettingsFile, lines.ToArray(), new UTF8Encoding(false));
            }
            catch (Exception ex) { Ui.Status(ex.Message); }
        }
    }
}
