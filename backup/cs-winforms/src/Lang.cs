// ============================================================
//  7z_wimlib - Lang.cs (taken from the DVT GUI)
//  All text on the screen comes from language files:
//      lang\<code>.ini      e.g. lang\en.ini, lang\ko.ini
//  Searched in this order: <exe folder>\lang, then the copy embedded
//  in 7z_wimlib.exe (the first file found is used). The files of
//  7z_wimlib\lang are embedded at build time, so the exe
//  works alone; an external file replaces the embedded one with the
//  same code. en.ini is the complete reference: anything missing in
//  another language file is taken from it.
//
//  File format (UTF-8):
//      ; comment
//      [meta]                 name = English, font = Segoe UI
//      [ui]                   key = text
//      [sections]             <section name in config.cmd> = text
//      [setting <key>]        summary, details, advanced, choice <value>
//      key += more text       adds a new line to the previous value
//  {0}, {1} ... in a text are filled in by the program.
// ============================================================
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;

namespace Dvt
{
    public static class L
    {
        public static string Code = "en";
        public static string FontFamily = "Segoe UI";
        public static readonly List<string> Problems = new List<string>();

        static Dictionary<string, List<KeyValuePair<string, string>>> merged;
        static string[] searchDirs = new string[0];

        // ---------- loading ----------

        static string baseDir = "";

        public static void Init(string exeDir)
        {
            baseDir = exeDir;
            searchDirs = new[] { Path.Combine(exeDir, "lang") };
        }

        // Folder that holds the external language files ("" if none)
        public static string ExternalDir()
        {
            foreach (string d in searchDirs) if (Directory.Exists(d)) return d;
            return "";
        }

        // code -> display name of every available language
        public static List<KeyValuePair<string, string>> Available()
        {
            List<string> codes = new List<string>();
            foreach (string r in Assembly.GetExecutingAssembly().GetManifestResourceNames())
            {
                if (r.StartsWith("Dvt.lang.", StringComparison.OrdinalIgnoreCase) && r.EndsWith(".ini", StringComparison.OrdinalIgnoreCase))
                    AddCode(codes, r.Substring(9, r.Length - 13));
            }
            foreach (string d in searchDirs)
            {
                if (!Directory.Exists(d)) continue;
                foreach (string f in Directory.GetFiles(d, "*.ini")) AddCode(codes, Path.GetFileNameWithoutExtension(f));
            }
            codes.Sort(StringComparer.OrdinalIgnoreCase);
            List<KeyValuePair<string, string>> list = new List<KeyValuePair<string, string>>();
            foreach (string c in codes)
            {
                string name = c;
                Dictionary<string, List<KeyValuePair<string, string>>> f = ReadLanguage(c);
                if (f != null)
                {
                    string n = Get(f, "meta", "name");
                    if (!string.IsNullOrEmpty(n)) name = n;
                }
                list.Add(new KeyValuePair<string, string>(c, name));
            }
            return list;
        }

        static void AddCode(List<string> codes, string c)
        {
            foreach (string x in codes) if (string.Equals(x, c, StringComparison.OrdinalIgnoreCase)) return;
            codes.Add(c.ToLowerInvariant());
        }

        // The language to use when none was chosen: always English
        // (the user picks another language at the bottom of the sidebar)
        public static string DefaultCode()
        {
            return "en";
        }

        public static void Load(string code)
        {
            Problems.Clear();
            Dictionary<string, List<KeyValuePair<string, string>>> en = ReadLanguage("en");
            if (en == null)
            {
                Problems.Add("en.ini was not found (embedded or in a lang folder).");
                en = new Dictionary<string, List<KeyValuePair<string, string>>>(StringComparer.OrdinalIgnoreCase);
            }
            merged = en;
            Code = "en";
            if (!string.IsNullOrEmpty(code) && !string.Equals(code, "en", StringComparison.OrdinalIgnoreCase))
            {
                Dictionary<string, List<KeyValuePair<string, string>>> other = ReadLanguage(code);
                if (other == null) Problems.Add(code + ".ini was not found. English is used.");
                else
                {
                    Overlay(merged, other);
                    Code = code.ToLowerInvariant();
                }
            }
            string font = Get(merged, "meta", "font");
            FontFamily = string.IsNullOrEmpty(font) ? "Segoe UI" : font;
        }

        // external file first, then the embedded copy
        static Dictionary<string, List<KeyValuePair<string, string>>> ReadLanguage(string code)
        {
            foreach (string d in searchDirs)
            {
                string f = Path.Combine(d, code + ".ini");
                if (File.Exists(f))
                {
                    try { return Parse(File.ReadAllText(f, Encoding.UTF8), f); }
                    catch (Exception ex) { Problems.Add(f + ": " + ex.Message); }
                }
            }
            string res = "Dvt.lang." + code + ".ini";
            foreach (string r in Assembly.GetExecutingAssembly().GetManifestResourceNames())
            {
                if (!string.Equals(r, res, StringComparison.OrdinalIgnoreCase)) continue;
                using (Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream(r))
                using (StreamReader sr = new StreamReader(s, Encoding.UTF8))
                    return Parse(sr.ReadToEnd(), "embedded " + code + ".ini");
            }
            return null;
        }

        // section -> ordered list of (key, value)
        static Dictionary<string, List<KeyValuePair<string, string>>> Parse(string text, string source)
        {
            Dictionary<string, List<KeyValuePair<string, string>>> d =
                new Dictionary<string, List<KeyValuePair<string, string>>>(StringComparer.OrdinalIgnoreCase);
            List<KeyValuePair<string, string>> cur = null;
            string[] lines = text.Replace("\r\n", "\n").Split('\n');
            for (int n = 0; n < lines.Length; n++)
            {
                string l = lines[n].Trim();
                if (n == 0) l = l.TrimStart('\uFEFF');
                if (l.Length == 0 || l.StartsWith(";") || l.StartsWith("#")) continue;
                if (l.StartsWith("[") && l.EndsWith("]"))
                {
                    string name = l.Substring(1, l.Length - 2).Trim();
                    if (!d.TryGetValue(name, out cur))
                    {
                        cur = new List<KeyValuePair<string, string>>();
                        d[name] = cur;
                    }
                    continue;
                }
                int eq = l.IndexOf('=');
                if (eq <= 0 || cur == null)
                {
                    Problems.Add(source + " line " + (n + 1) + ": not 'key = text'");
                    continue;
                }
                string key = l.Substring(0, eq).Trim();
                string val = l.Substring(eq + 1).Trim();
                if (key.EndsWith("+"))
                {
                    // key += text : one more line for the previous value
                    key = key.Substring(0, key.Length - 1).Trim();
                    int i = IndexOf(cur, key);
                    if (i >= 0) { cur[i] = new KeyValuePair<string, string>(key, cur[i].Value + "\r\n" + val); continue; }
                }
                Set(cur, key, val);
            }
            return d;
        }

        static int IndexOf(List<KeyValuePair<string, string>> list, string key)
        {
            for (int i = 0; i < list.Count; i++)
                if (string.Equals(list[i].Key, key, StringComparison.OrdinalIgnoreCase)) return i;
            return -1;
        }

        static void Set(List<KeyValuePair<string, string>> list, string key, string val)
        {
            int i = IndexOf(list, key);
            if (i >= 0) list[i] = new KeyValuePair<string, string>(key, val);
            else list.Add(new KeyValuePair<string, string>(key, val));
        }

        static void Overlay(Dictionary<string, List<KeyValuePair<string, string>>> target,
                            Dictionary<string, List<KeyValuePair<string, string>>> over)
        {
            foreach (KeyValuePair<string, List<KeyValuePair<string, string>>> sec in over)
            {
                List<KeyValuePair<string, string>> t;
                if (!target.TryGetValue(sec.Key, out t))
                {
                    t = new List<KeyValuePair<string, string>>();
                    target[sec.Key] = t;
                }
                foreach (KeyValuePair<string, string> kv in sec.Value) Set(t, kv.Key, kv.Value);
            }
        }

        static string Get(Dictionary<string, List<KeyValuePair<string, string>>> d, string section, string key)
        {
            List<KeyValuePair<string, string>> s;
            if (d == null || !d.TryGetValue(section, out s)) return null;
            int i = IndexOf(s, key);
            return i < 0 ? null : s[i].Value;
        }

        // ---------- data files (adk.ini ...) ----------

        // <exe folder>\<file>, <exe folder>\gui_cs\<file>, then the
        // embedded resource Dvt.data.<file>; null when none exists
        public static Dictionary<string, List<KeyValuePair<string, string>>> ReadData(string fileName)
        {
            // <exe>\data first (adk.ini lives there), then <exe>, <exe>\gui_cs
            foreach (string d in new[] { Path.Combine(baseDir, "data"), baseDir, Path.Combine(baseDir, "gui_cs") })
            {
                string f = Path.Combine(d, fileName);
                if (File.Exists(f))
                {
                    try { return Parse(File.ReadAllText(f, Encoding.UTF8), f); }
                    catch (Exception ex) { Problems.Add(f + ": " + ex.Message); }
                }
            }
            string res = "Dvt.data." + fileName;
            foreach (string r in Assembly.GetExecutingAssembly().GetManifestResourceNames())
            {
                if (!string.Equals(r, res, StringComparison.OrdinalIgnoreCase)) continue;
                using (Stream s = Assembly.GetExecutingAssembly().GetManifestResourceStream(r))
                using (StreamReader sr = new StreamReader(s, Encoding.UTF8))
                    return Parse(sr.ReadToEnd(), "embedded " + fileName);
            }
            return null;
        }

        // Value of [section] key in a data file, or "" when missing
        public static string Value(Dictionary<string, List<KeyValuePair<string, string>>> d, string section, string key)
        {
            return Get(d, section, key) ?? "";
        }

        // ---------- text ----------

        // Text of [ui] key; "[key]" when it is missing everywhere
        public static string T(string key)
        {
            string v = Get(merged, "ui", key);
            return v ?? "[" + key + "]";
        }

        // T() with {0}, {1} ... filled in
        public static string F(string key, params object[] args)
        {
            string t = T(key);
            try { return string.Format(t, args); }
            catch (FormatException) { return t; }
        }

    }
}
