// ============================================================
//  Z_wimlib - WimLib.cs (taken from the DVT GUI)
//  wimlib called directly through <PE_TOOLS_DIR>\libwim-15.dll (x64,
//  the DLL of the wimlib download on the WinPE tools tab):
//   - ReadImages: the images of a WIM file (index, name, size); read in
//     the GUI process, no administrator rights needed.
//   - ReadInfo: the header of a WIM file and the details of its images
//     (edition, architecture, version, languages, dates) for the info tab.
//   - RunHelper: the elevated helper of the "Backup / restore" page,
//     started as Z_wimlib.exe --wim-helper <work folder>. It reads
//     job.ini of the work folder and
//       capture: saves a volume or folder (threads = compressor threads,
//                0 = automatic) as a new WIM file (written to
//                <file>.tmp first) or as a new image of an existing one;
//       export : copies one image (or all) of a WIM / ESD file into a
//                new WIM file (written to <file>.tmp first) or into an
//                existing one; runs without administrator rights;
//       split  : writes a WIM as split parts (<name>.swm, <name>2.swm,
//                ...) of at most part_mb MiB; no administrator rights;
//       join   : joins all parts of a split WIM into one WIM file
//                (written to <file>.tmp first); no administrator rights;
//       apply  : formats a volume (NTFS) and applies one image to it,
//                or (folder = ...) applies it into an empty folder;
//                with boot = UEFI|BIOS|ALL bcdboot then writes the boot
//                files to the chosen system partition (a partition
//                without a letter gets one for that time).
//     status.txt of the work folder gets the progress
//     ("P <percent> <phase> ...", percent -1 = unknown), log lines and
//     at the end "EXIT <code> <text>". A file "cancel" in the work
//     folder stops the job. wimlib's own messages go to
//     wimlib_errors.txt.
//  Constants and structure offsets: wimlib.h of wimlib 1.14 (x64).
// ============================================================
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace Dvt
{
    public class WimImage
    {
        public int Index;
        public string Name = "", Description = "";
        public long TotalBytes = -1;
        // filled by ReadInfo only
        public string Flags = "", DisplayName = "", ProductName = "", EditionId = "", InstallationType = "";
        public string Arch = "", Version = "", DefaultLanguage = "", Languages = "";
        public long DirCount = -1, FileCount = -1;
        public DateTime Created = DateTime.MinValue, Modified = DateTime.MinValue;
    }

    // the header of a WIM file (struct wimlib_wim_info)
    public class WimFileInfo
    {
        public string Guid = "";
        public int ImageCount, BootIndex, ChunkSize, PartNumber, TotalParts, Compression;
        public uint Version;
        public long TotalBytes, FileBytes;
        public bool Integrity, ReadOnlyFlag, Pipable, Spanned;
        public List<WimImage> Images = new List<WimImage>();
        // wim_version 0xE00: solid resources (ESD)
        public bool Solid { get { return Version == 0xE00; } }
    }

    // a partition of a local disk (Get-Partition), for the boot files
    public class DiskPartition
    {
        public const string EspGuid = "{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}";
        public const string MsrGuid = "{e3c9e316-0b5c-4db8-817d-f92df00215ae}";
        public int Disk, Number;
        public char Letter;                   // '\0' = no drive letter
        public string GptType = "", FileSystem = "", Style = "";
        public int MbrType;
        public bool Active, IsSystem;
        public long Size, Offset;

        public bool Gpt { get { return Style == "GPT"; } }
        // EFI system partition (GPT type, or MBR type 0xEF)
        public bool IsEsp { get { return string.Equals(GptType, EspGuid, StringComparison.OrdinalIgnoreCase) || MbrType == 0xEF; } }
        public bool IsMsr { get { return string.Equals(GptType, MsrGuid, StringComparison.OrdinalIgnoreCase); } }
    }

    public static class WimLib
    {
        public const string DllName = "libwim-15.dll";
        public const int CompressionXpress = 1, CompressionLzx = 2, CompressionLzms = 3;

        const int InitDontAcquirePrivileges = 0x00000002;
        const int OpenWriteAccess = 0x00000004;
        const int AddVerbose = 0x00000004, AddWinConfig = 0x00000800;
        const int WriteSolid = 0x00001000;
        const int ExportBoot = 0x00000001;
        public const int AllImages = -1;
        const int MsgExtractFileStructure = 3, MsgExtractStreams = 4, MsgExtractMetadata = 6;
        const int MsgScanDentry = 10, MsgWriteStreams = 12, MsgWriteMetadataBegin = 13;
        const int MsgSplitBeginPart = 19, MsgSplitEndPart = 20;
        const int MsgVerifyIntegrity = 16, MsgBeginVerifyImage = 27, MsgVerifyStreams = 29;
        const int OpenCheckIntegrity = 0x00000001;
        const int StatusContinue = 0, StatusAbort = 1;
        public const int ErrAborted = 76;
        const long MiB = 1048576;

        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        delegate int ProgressFunc(int msg, IntPtr info, IntPtr ctx);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        static extern IntPtr LoadLibraryW(string path);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        static extern int wimlib_global_init(int initFlags);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        static extern int wimlib_create_new_wim(int ctype, out IntPtr wim);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        static extern int wimlib_open_wim_with_progress(string wimFile, int openFlags, out IntPtr wim, ProgressFunc progfunc, IntPtr progctx);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        static extern void wimlib_register_progress_function(IntPtr wim, ProgressFunc progfunc, IntPtr progctx);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        static extern int wimlib_add_image(IntPtr wim, string source, string name, string configFile, int addFlags);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        static extern int wimlib_write(IntPtr wim, string path, int image, int writeFlags, uint numThreads);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        static extern int wimlib_overwrite(IntPtr wim, int writeFlags, uint numThreads);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        static extern int wimlib_extract_image(IntPtr wim, int image, string target, int extractFlags);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        static extern IntPtr wimlib_get_image_property(IntPtr wim, int image, string propertyName);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        static extern int wimlib_set_image_property(IntPtr wim, int image, string propertyName, string propertyValue);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        static extern int wimlib_get_wim_info(IntPtr wim, IntPtr info);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        static extern int wimlib_export_image(IntPtr srcWim, int srcImage, IntPtr destWim, string destName, string destDescription, int exportFlags);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        static extern int wimlib_split(IntPtr wim, string swmName, ulong partSize, int writeFlags);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        static extern int wimlib_reference_resource_files(IntPtr wim, [MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.LPWStr)] string[] files,
            uint count, int refFlags, int openFlags);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        static extern int wimlib_verify_wim(IntPtr wim, int verifyFlags);

        // a missing part of a split WIM (OpenRead): not a wimlib code
        public const int ErrPartsMissing = -2;

        // opens a WIM file to read it. A part of a split WIM (.swm, any part)
        // is opened as its first part, the other parts (same folder, the names
        // that wimlib_split gives) are referenced; total / found tell a missing
        // part (rc = ErrPartsMissing). wim stays IntPtr.Zero when rc != 0.
        static int OpenRead(string file, ProgressFunc pf, out IntPtr wim, out int total, out int found)
        {
            return OpenRead(file, 0, pf, out wim, out total, out found);
        }

        static int OpenRead(string file, int openFlags, ProgressFunc pf, out IntPtr wim, out int total, out int found)
        {
            total = 1; found = 1;
            int rc = wimlib_open_wim_with_progress(file, openFlags, out wim, pf, IntPtr.Zero);
            if (rc != 0) { wim = IntPtr.Zero; return rc; }
            int part;
            PartInfo(wim, out part, out total);
            if (total <= 1) return 0;
            List<string> parts = SwmParts(file);
            found = parts.Count;
            if (parts.Count != total) { wimlib_free(wim); wim = IntPtr.Zero; return ErrPartsMissing; }
            if (part != 1)
            {
                wimlib_free(wim);
                rc = wimlib_open_wim_with_progress(parts[0], openFlags, out wim, pf, IntPtr.Zero);
                if (rc != 0) { wim = IntPtr.Zero; return rc; }
            }
            string[] rest = parts.GetRange(1, parts.Count - 1).ToArray();
            rc = wimlib_reference_resource_files(wim, rest, (uint)rest.Length, 0, openFlags);
            if (rc != 0) { wimlib_free(wim); wim = IntPtr.Zero; }
            return rc;
        }

        // part_number and total_parts of wimlib_wim_info
        static void PartInfo(IntPtr wim, out int part, out int total)
        {
            part = 1; total = 1;
            IntPtr info = Marshal.AllocHGlobal(256);
            try
            {
                for (int n = 0; n < 256; n++) Marshal.WriteByte(info, n, 0);
                if (wimlib_get_wim_info(wim, info) != 0) return;
                part = (ushort)Marshal.ReadInt16(info, 32);
                total = Math.Max(1, (int)(ushort)Marshal.ReadInt16(info, 34));
            }
            finally { Marshal.FreeHGlobal(info); }
        }

        // the text of an OpenRead failure (English, for the helper's status)
        static string OpenText(int rc, string file, int total, int found)
        {
            if (rc == ErrPartsMissing)
                return file + " belongs to " + total + " parts, but " + found + " were found in its folder (all parts must keep their names).";
            return ErrorText(rc);
        }
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        static extern int wimlib_join_with_progress([MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.LPWStr)] string[] swms,
            uint numSwms, string outputPath, int swmOpenFlags, int wimWriteFlags, ProgressFunc progfunc, IntPtr progctx);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        static extern IntPtr wimlib_get_error_string(int code);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
        static extern void wimlib_free(IntPtr wim);
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Unicode)]
        static extern int wimlib_set_error_file_by_name(string path);

        static bool loaded;

        // Loads <toolDir>\libwim-15.dll and initializes wimlib; null = ready,
        // else the reason. The helper acquires the backup / restore
        // privileges, the GUI does not need them.
        public static string Load(string toolDir, bool acquirePrivileges)
        {
            if (loaded) return null;
            if (!Environment.Is64BitProcess) return DllName + " needs a 64-bit process.";
            string dll = Path.Combine(toolDir, DllName);
            if (!File.Exists(dll)) return "Not found: " + dll;
            if (LoadLibraryW(dll) == IntPtr.Zero) return "LoadLibrary " + dll + ": error " + Marshal.GetLastWin32Error();
            int rc = wimlib_global_init(acquirePrivileges ? 0 : InitDontAcquirePrivileges);
            if (rc != 0) return ErrorText(rc);
            loaded = true;
            return null;
        }

        public static string ErrorText(int code)
        {
            string s = null;
            try { s = Marshal.PtrToStringUni(wimlib_get_error_string(code)); } catch (Exception) { }
            return "wimlib error " + code + (string.IsNullOrEmpty(s) ? "" : ": " + s);
        }

        static string Property(IntPtr wim, int image, string name)
        {
            IntPtr p = wimlib_get_image_property(wim, image, name);
            return p == IntPtr.Zero ? "" : Marshal.PtrToStringUni(p);
        }

        // struct wimlib_wim_info: guid[16], then uint32 image_count
        static int ImageCount(IntPtr wim)
        {
            IntPtr info = Marshal.AllocHGlobal(256);
            try
            {
                for (int n = 0; n < 256; n++) Marshal.WriteByte(info, n, 0);
                return wimlib_get_wim_info(wim, info) == 0 ? Marshal.ReadInt32(info, 16) : 0;
            }
            finally { Marshal.FreeHGlobal(info); }
        }

        // struct wimlib_wim_info: boot_index at 20 (0 = none)
        static int BootIndex(IntPtr wim)
        {
            IntPtr info = Marshal.AllocHGlobal(256);
            try
            {
                for (int n = 0; n < 256; n++) Marshal.WriteByte(info, n, 0);
                return wimlib_get_wim_info(wim, info) == 0 ? Marshal.ReadInt32(info, 20) : 0;
            }
            finally { Marshal.FreeHGlobal(info); }
        }

        static long ParseLong(string s, long fallback)
        {
            long v;
            return long.TryParse(s.Trim(), out v) ? v : fallback;
        }

        // The images of a WIM file, or null with the reason (Load first)
        public static List<WimImage> ReadImages(string wimFile, out string error)
        {
            error = null;
            IntPtr wim;
            int total, found;
            int rc = OpenRead(wimFile, null, out wim, out total, out found);
            if (rc == ErrPartsMissing) { error = L.F("sj.err_part_count", wimFile, total, found); return null; }
            if (rc != 0) { error = ErrorText(rc); return null; }
            try
            {
                List<WimImage> list = new List<WimImage>();
                int count = ImageCount(wim);
                for (int i = 1; i <= count; i++)
                {
                    WimImage w = new WimImage();
                    w.Index = i;
                    w.Name = Property(wim, i, "NAME");
                    w.Description = Property(wim, i, "DESCRIPTION");
                    w.TotalBytes = ParseLong(Property(wim, i, "TOTALBYTES"), -1);
                    list.Add(w);
                }
                return list;
            }
            finally { wimlib_free(wim); }
        }

        // The header and the image details of a WIM file, or null with the reason (Load first)
        public static WimFileInfo ReadInfo(string wimFile, out string error)
        {
            error = null;
            IntPtr wim;
            int rc = wimlib_open_wim_with_progress(wimFile, 0, out wim, null, IntPtr.Zero);
            if (rc != 0) { error = ErrorText(rc); return null; }
            IntPtr info = Marshal.AllocHGlobal(256);
            try
            {
                for (int n = 0; n < 256; n++) Marshal.WriteByte(info, n, 0);
                rc = wimlib_get_wim_info(wim, info);
                if (rc != 0) { error = ErrorText(rc); return null; }
                // struct wimlib_wim_info: guid[16], image_count (16), boot_index (20),
                // wim_version (24), chunk_size (28), part_number (32, 16 bit),
                // total_parts (34, 16 bit), compression_type (36), total_bytes (40),
                // flag bits (48): has_integrity_table, opened_from_file, is_readonly,
                // has_rpfix, is_marked_readonly, spanned, write_in_progress,
                // metadata_only, resource_only, pipable
                WimFileInfo f = new WimFileInfo();
                byte[] guid = new byte[16];
                Marshal.Copy(info, guid, 0, 16);
                f.Guid = new Guid(guid).ToString("B");
                f.ImageCount = Marshal.ReadInt32(info, 16);
                f.BootIndex = Marshal.ReadInt32(info, 20);
                f.Version = (uint)Marshal.ReadInt32(info, 24);
                f.ChunkSize = Marshal.ReadInt32(info, 28);
                f.PartNumber = (ushort)Marshal.ReadInt16(info, 32);
                f.TotalParts = (ushort)Marshal.ReadInt16(info, 34);
                f.Compression = Marshal.ReadInt32(info, 36);
                f.TotalBytes = Marshal.ReadInt64(info, 40);
                int bits = Marshal.ReadInt32(info, 48);
                f.Integrity = (bits & 0x1) != 0;
                f.ReadOnlyFlag = (bits & 0x10) != 0;
                f.Spanned = (bits & 0x20) != 0;
                f.Pipable = (bits & 0x200) != 0;
                try { f.FileBytes = new FileInfo(wimFile).Length; } catch (Exception) { f.FileBytes = -1; }
                for (int i = 1; i <= f.ImageCount; i++) f.Images.Add(ImageDetails(wim, i));
                return f;
            }
            finally
            {
                Marshal.FreeHGlobal(info);
                wimlib_free(wim);
            }
        }

        static WimImage ImageDetails(IntPtr wim, int i)
        {
            WimImage w = new WimImage();
            w.Index = i;
            w.Name = Property(wim, i, "NAME");
            w.Description = Property(wim, i, "DESCRIPTION");
            w.TotalBytes = ParseLong(Property(wim, i, "TOTALBYTES"), -1);
            w.Flags = Property(wim, i, "FLAGS");
            w.DisplayName = Property(wim, i, "DISPLAYNAME");
            w.DirCount = ParseLong(Property(wim, i, "DIRCOUNT"), -1);
            w.FileCount = ParseLong(Property(wim, i, "FILECOUNT"), -1);
            w.Created = FileTime(Property(wim, i, "CREATIONTIME/HIGHPART"), Property(wim, i, "CREATIONTIME/LOWPART"));
            w.Modified = FileTime(Property(wim, i, "LASTMODIFICATIONTIME/HIGHPART"), Property(wim, i, "LASTMODIFICATIONTIME/LOWPART"));
            w.ProductName = Property(wim, i, "WINDOWS/PRODUCTNAME");
            w.EditionId = Property(wim, i, "WINDOWS/EDITIONID");
            w.InstallationType = Property(wim, i, "WINDOWS/INSTALLATIONTYPE");
            w.Arch = ArchName(Property(wim, i, "WINDOWS/ARCH"));
            string major = Property(wim, i, "WINDOWS/VERSION/MAJOR");
            if (major.Length > 0)
            {
                w.Version = major + "." + Property(wim, i, "WINDOWS/VERSION/MINOR") + "." + Property(wim, i, "WINDOWS/VERSION/BUILD");
                string sp = Property(wim, i, "WINDOWS/VERSION/SPBUILD");
                if (sp.Length > 0) w.Version += "." + sp;
            }
            w.DefaultLanguage = Property(wim, i, "WINDOWS/LANGUAGES/DEFAULT");
            // repeated elements: LANGUAGE, LANGUAGE[2], ... (wimlib path syntax)
            List<string> langs = new List<string>();
            for (int n = 1; n <= 64; n++)
            {
                string l = Property(wim, i, "WINDOWS/LANGUAGES/LANGUAGE[" + n + "]");
                if (l.Length == 0) break;
                langs.Add(l);
            }
            w.Languages = string.Join(", ", langs.ToArray());
            return w;
        }

        // the processor architecture of the image (PROCESSOR_ARCHITECTURE_*)
        static string ArchName(string value)
        {
            switch (value.Trim())
            {
                case "": return "";
                case "0": return "x86";
                case "5": return "ARM";
                case "6": return "IA64";
                case "9": return "x64";
                case "12": return "ARM64";
                default: return value;
            }
        }

        // FILETIME of the XML data: HIGHPART / LOWPART as hex ("0x01DA...")
        static DateTime FileTime(string high, string low)
        {
            uint h, l;
            if (!ParseHex(high, out h) || !ParseHex(low, out l)) return DateTime.MinValue;
            long ft = ((long)h << 32) | l;
            if (ft <= 0) return DateTime.MinValue;
            try { return DateTime.FromFileTimeUtc(ft).ToLocalTime(); }
            catch (Exception) { return DateTime.MinValue; }
        }

        static bool ParseHex(string s, out uint v)
        {
            s = s.Trim();
            if (s.StartsWith("0x", StringComparison.OrdinalIgnoreCase)) s = s.Substring(2);
            return uint.TryParse(s, System.Globalization.NumberStyles.HexNumber, null, out v);
        }

        // ---------- checks, made by the GUI and again by the helper ----------
        // Each returns null or the key of the reason in lang\*.ini.

        static string Root(string path)
        {
            try { return Path.GetPathRoot(Path.GetFullPath(path)).ToUpperInvariant(); }
            catch (Exception) { return ""; }
        }

        static string WithSlash(string path) { return path.TrimEnd('\\') + "\\"; }

        // the drive of the running Windows (system drive or Windows folder)
        public static bool IsWindowsDrive(char letter)
        {
            string r = char.ToUpperInvariant(letter) + ":\\";
            return r == Root(Environment.GetEnvironmentVariable("SystemDrive") + "\\")
                || r == Root(Environment.GetFolderPath(Environment.SpecialFolder.Windows));
        }

        public static string CaptureProblem(string source, string wimFile)
        {
            if (string.IsNullOrEmpty(wimFile) || !wimFile.EndsWith(".wim", StringComparison.OrdinalIgnoreCase)) return "bk.err_wim_name";
            if (string.IsNullOrEmpty(source) || !Directory.Exists(source)) return "bk.err_source_missing";
            // the running Windows: the whole Windows drive or the Windows folder
            // (a folder elsewhere on that drive is fine)
            string src = WithSlash(Path.GetFullPath(source));
            string root = Root(source);
            if (root.Length == 3 && IsWindowsDrive(root[0]) && string.Equals(src, root, StringComparison.OrdinalIgnoreCase)) return "bk.err_source_system";
            if (src.StartsWith(WithSlash(Environment.GetFolderPath(Environment.SpecialFolder.Windows)), StringComparison.OrdinalIgnoreCase)) return "bk.err_source_system";
            if (Path.GetFullPath(wimFile).StartsWith(src, StringComparison.OrdinalIgnoreCase)) return "bk.err_wim_in_source";
            return null;
        }

        // the parts of a split WIM: <dir>\<stem>.swm, <stem>2.swm, <stem>3.swm, ...
        // (the names wimlib_split gives); a chosen later part (<stem>2.swm)
        // leads to the first one when that exists
        public static List<string> SwmParts(string anyPart)
        {
            List<string> parts = new List<string>();
            if (string.IsNullOrEmpty(anyPart)) return parts;
            string full = SafeFull(anyPart);
            if (full.Length == 0) return parts;
            string dir = Path.GetDirectoryName(full), ext = Path.GetExtension(full);
            string stem = Path.GetFileNameWithoutExtension(full);
            string bare = stem.TrimEnd('0', '1', '2', '3', '4', '5', '6', '7', '8', '9');
            if (bare.Length > 0 && bare.Length < stem.Length && File.Exists(Path.Combine(dir, bare + ext))) stem = bare;
            string first = Path.Combine(dir, stem + ext);
            if (!File.Exists(first)) return parts;
            parts.Add(first);
            for (int n = 2; n < 10000; n++)
            {
                string f = Path.Combine(dir, stem + n + ext);
                if (!File.Exists(f)) break;
                parts.Add(f);
            }
            return parts;
        }

        // split: an existing source, a .swm target
        public static string SplitProblem(string sourceWim, string swmFile)
        {
            if (string.IsNullOrEmpty(sourceWim) || !File.Exists(sourceWim)) return "ex.err_source_missing";
            if (string.IsNullOrEmpty(swmFile) || !swmFile.EndsWith(".swm", StringComparison.OrdinalIgnoreCase) || SafeFull(swmFile).Length == 0) return "sj.err_swm_name";
            return null;
        }

        // join: the parts, a .wim target that is none of them
        public static string JoinProblem(List<string> parts, string wimFile)
        {
            if (parts == null || parts.Count == 0) return "sj.err_no_parts";
            if (string.IsNullOrEmpty(wimFile) || !wimFile.EndsWith(".wim", StringComparison.OrdinalIgnoreCase)) return "sj.err_wim_name";
            string b = SafeFull(wimFile);
            if (b.Length == 0) return "sj.err_wim_name";
            foreach (string p in parts)
                if (string.Equals(SafeFull(p), b, StringComparison.OrdinalIgnoreCase)) return "ex.err_same_file";
            return null;
        }

        // export: an existing source file, a .wim / .esd target that is not the source
        public static string ExportProblem(string sourceWim, string wimFile)
        {
            if (string.IsNullOrEmpty(sourceWim) || !File.Exists(sourceWim)) return "ex.err_source_missing";
            if (string.IsNullOrEmpty(wimFile)
                || !(wimFile.EndsWith(".wim", StringComparison.OrdinalIgnoreCase) || wimFile.EndsWith(".esd", StringComparison.OrdinalIgnoreCase)))
                return "ex.err_wim_name";
            string a = SafeFull(sourceWim), b = SafeFull(wimFile);
            if (b.Length == 0) return "ex.err_wim_name";
            if (string.Equals(a, b, StringComparison.OrdinalIgnoreCase)) return "ex.err_same_file";
            return null;
        }

        public static string TargetProblem(char letter, string wimFile, string buildRoot)
        {
            letter = char.ToUpperInvariant(letter);
            string r = letter + ":\\";
            DriveInfo d;
            try { d = new DriveInfo(r); }
            catch (Exception) { return "bk.err_target_type"; }
            if (d.DriveType != DriveType.Fixed && d.DriveType != DriveType.Removable) return "bk.err_target_type";
            if (IsWindowsDrive(letter)) return "bk.err_target_system";
            if (Root(wimFile) == r) return "bk.err_target_wim";
            if (Root(buildRoot) == r) return "bk.err_target_build";
            return null;
        }

        static bool Inside(string path, string dir)
        {
            return WithSlash(path).StartsWith(WithSlash(dir), StringComparison.OrdinalIgnoreCase);
        }

        // a folder chosen as the restore target (no format, the image goes into it)
        public static string FolderTargetProblem(string folder, string wimFile, string buildRoot)
        {
            string f;
            try { f = Path.GetFullPath(folder); }
            catch (Exception) { return "bk.err_folder_invalid"; }
            string r = Root(f);
            if (r.Length != 3) return "bk.err_folder_invalid";
            if (string.Equals(WithSlash(f), r, StringComparison.OrdinalIgnoreCase)) return "bk.err_folder_root";
            try
            {
                DriveInfo d = new DriveInfo(r);
                if (d.DriveType != DriveType.Fixed && d.DriveType != DriveType.Removable) return "bk.err_folder_type";
            }
            catch (Exception) { return "bk.err_folder_type"; }
            string windows = Environment.GetFolderPath(Environment.SpecialFolder.Windows);
            if (windows.Length > 0 && (Inside(f, windows) || Inside(windows, f))) return "bk.err_folder_system";
            if (!string.IsNullOrEmpty(wimFile) && Inside(SafeFull(wimFile), f)) return "bk.err_folder_wim";
            if (!string.IsNullOrEmpty(buildRoot))
            {
                string b = SafeFull(buildRoot);
                if (b.Length > 0 && (Inside(f, b) || Inside(b, f))) return "bk.err_folder_build";
            }
            if (File.Exists(f)) return "bk.err_folder_invalid";
            if (Directory.Exists(f) && Directory.GetFileSystemEntries(f).Length > 0) return "bk.err_folder_not_empty";
            return null;
        }

        static string SafeFull(string path)
        {
            try { return Path.GetFullPath(path); }
            catch (Exception) { return ""; }
        }

        // NTFS volume label: letters, digits, space, - and _, at most 32
        public static string CleanLabel(string label)
        {
            StringBuilder sb = new StringBuilder();
            foreach (char c in label.Trim())
                if ((char.IsLetterOrDigit(c) || c == ' ' || c == '-' || c == '_') && sb.Length < 32) sb.Append(c);
            return sb.Length > 0 ? sb.ToString() : "Windows";
        }

        // ============================================================
        //  elevated helper: Z_wimlib.exe --wim-helper <work folder>
        // ============================================================

        static string statusFile, cancelFile, errorFile;
        static ProgressFunc progressFunc;     // kept alive while wimlib holds it
        static int lastPct = -2, lastCancelCheck;
        static DateTime lastLine = DateTime.MinValue;
        static bool cancelled;

        public static int RunHelper(string[] args)
        {
            if (args.Length < 2) return 87;
            string work = args[1];
            statusFile = Path.Combine(work, "status.txt");
            cancelFile = Path.Combine(work, "cancel");
            errorFile = Path.Combine(work, "wimlib_errors.txt");
            try { File.WriteAllText(statusFile, "", new UTF8Encoding(false)); }
            catch (Exception) { return 5; }
            try
            {
                Dictionary<string, string> job = ReadJob(Path.Combine(work, "job.ini"));
                string mode = Get(job, "mode");
                // export, split and join run without administrator rights: no privileges to acquire
                string err = Load(Get(job, "tools"), mode == "capture" || mode == "apply");
                if (err != null) return Finish(2, err);
                wimlib_set_error_file_by_name(errorFile);
                progressFunc = OnProgress;
                if (mode == "capture") return Capture(job);
                if (mode == "export") return Export(job);
                if (mode == "split") return Split(job);
                if (mode == "join") return Join(job);
                if (mode == "verify") return Verify(job);
                if (mode == "apply") return Apply(job);
                return Finish(87, "Unknown mode: " + mode);
            }
            catch (Exception ex) { return Finish(1, ex.Message); }
        }

        static Dictionary<string, string> ReadJob(string file)
        {
            Dictionary<string, string> job = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            foreach (string line in File.ReadAllLines(file, Encoding.UTF8))
            {
                int eq = line.IndexOf('=');
                if (eq > 0) job[line.Substring(0, eq).Trim()] = line.Substring(eq + 1).Trim();
            }
            return job;
        }

        static string Get(Dictionary<string, string> job, string key)
        {
            string v;
            return job.TryGetValue(key, out v) ? v : "";
        }

        static int Capture(Dictionary<string, string> job)
        {
            string source = Get(job, "source"), wimFile = Get(job, "wim");
            string name = Get(job, "name"), desc = Get(job, "description");
            int ctype = (int)ParseLong(Get(job, "compression"), CompressionLzx);
            // compressor threads; 0 = wimlib's default (one per logical processor)
            uint threads = (uint)Math.Max(0, Math.Min(64, ParseLong(Get(job, "threads"), 0)));
            bool append = Get(job, "append") == "1" && File.Exists(wimFile);
            string why = CaptureProblem(source, wimFile);
            if (why != null) return Finish(87, "Refused: " + why + " (" + source + " -> " + wimFile + ")");
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(wimFile)));

            IntPtr wim;
            int rc;
            if (append)
            {
                Status("Opening " + wimFile);
                rc = wimlib_open_wim_with_progress(wimFile, OpenWriteAccess, out wim, progressFunc, IntPtr.Zero);
                if (rc != 0) return Failed(rc, "Open " + wimFile);
            }
            else
            {
                rc = wimlib_create_new_wim(ctype, out wim);
                if (rc != 0) return Failed(rc, "Create WIM");
                wimlib_register_progress_function(wim, progressFunc, IntPtr.Zero);
            }

            // a new file is written next to the old one first: a failed or
            // cancelled backup leaves the old file as it was
            string tmp = append ? null : wimFile + ".tmp";
            string step;
            int image = 0;
            try
            {
                step = "Capture " + source;
                Status("Scanning " + source + " (Windows capture configuration)");
                Progress(-1, "scan 0 0 0", true);
                rc = wimlib_add_image(wim, source, name.Length > 0 ? name : null, null, AddWinConfig | AddVerbose);
                if (rc == 0)
                {
                    image = ImageCount(wim);
                    if (desc.Length > 0) wimlib_set_image_property(wim, image, "DESCRIPTION", desc);
                    if (append)
                    {
                        step = "Write image " + image + " to " + wimFile;
                        Status("Adding image " + image + " to " + wimFile);
                        rc = wimlib_overwrite(wim, 0, threads);
                    }
                    else
                    {
                        step = "Write " + tmp;
                        if (File.Exists(tmp)) File.Delete(tmp);
                        Status("Writing " + tmp);
                        rc = wimlib_write(wim, tmp, AllImages, ctype == CompressionLzms ? WriteSolid : 0, threads);
                    }
                }
            }
            finally { wimlib_free(wim); }
            if (rc != 0)
            {
                if (tmp != null) try { File.Delete(tmp); } catch (Exception) { }
                return Failed(rc, step);
            }
            if (tmp != null)
            {
                if (File.Exists(wimFile)) File.Delete(wimFile);
                File.Move(tmp, wimFile);
            }
            if (Get(job, "verify") != "1") return Finish(0, "OK image " + image);
            // the backup is written: a cancelled verification does not undo it
            string vstep;
            rc = VerifyCore(wimFile, out vstep);
            if (rc == ErrAborted) return Finish(0, "OK image " + image + " (verification cancelled)");
            if (rc == ErrPartsMissing) return Finish(2, "The backup was written, but its verification failed: " + vstep);
            if (rc != 0) return Failed(rc, "The backup was written, but its verification failed: " + vstep);
            return Finish(0, "OK image " + image + ", verified");
        }

        // ---------- verify ----------

        // the integrity table (when the file has one) and then all the data:
        // wimlib_verify_wim reads every blob and checks its SHA-1 hash, and
        // the metadata of every image. A split WIM needs all of its parts.
        static int VerifyCore(string wimFile, out string step)
        {
            step = "Open " + wimFile;
            if (!File.Exists(wimFile)) { step = "Not found: " + wimFile; return ErrPartsMissing; }
            IntPtr wim;
            int total, found;
            Status("Opening " + wimFile + " (with the integrity check)");
            Progress(-1, "vopen", true);
            int rc = OpenRead(wimFile, OpenCheckIntegrity, progressFunc, out wim, out total, out found);
            if (rc == ErrPartsMissing) { step = OpenText(rc, wimFile, total, found); return rc; }
            if (rc != 0) { step = "Open " + wimFile + " (integrity table)"; return rc; }
            try
            {
                if (total > 1) Status("Split WIM: " + total + " parts");
                WimFileInfo f = new WimFileInfo();
                IntPtr info = Marshal.AllocHGlobal(256);
                try
                {
                    for (int n = 0; n < 256; n++) Marshal.WriteByte(info, n, 0);
                    if (wimlib_get_wim_info(wim, info) == 0) f.Integrity = (Marshal.ReadInt32(info, 48) & 0x1) != 0;
                }
                finally { Marshal.FreeHGlobal(info); }
                Status(f.Integrity ? "The integrity table is correct." : "The file has no integrity table (the data is checked by its SHA-1 hashes).");
                step = "Verify " + wimFile;
                Status("Verifying the metadata of the images and all the data");
                Progress(-1, "verify 0 0", true);
                rc = wimlib_verify_wim(wim, 0);
            }
            finally { wimlib_free(wim); }
            return rc;
        }

        static int Verify(Dictionary<string, string> job)
        {
            string wimFile = Get(job, "wim"), step;
            int rc = VerifyCore(wimFile, out step);
            // a missing file or part (not a wimlib code)
            if (rc == ErrPartsMissing) return Finish(2, step);
            if (rc != 0) return Failed(rc, step);
            return Finish(0, "OK verified " + wimFile);
        }

        // index = 0: all images (with their own names); else one image, with
        // the given name / description (empty = those of the source image)
        static int Export(Dictionary<string, string> job)
        {
            string source = Get(job, "source"), wimFile = Get(job, "wim");
            string name = Get(job, "name"), desc = Get(job, "description");
            int index = (int)ParseLong(Get(job, "index"), 0);
            int ctype = (int)ParseLong(Get(job, "compression"), CompressionLzx);
            uint threads = (uint)Math.Max(0, Math.Min(64, ParseLong(Get(job, "threads"), 0)));
            bool append = Get(job, "append") == "1" && File.Exists(wimFile);
            string why = ExportProblem(source, wimFile);
            if (why != null) return Finish(87, "Refused: " + why + " (" + source + " -> " + wimFile + ")");
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(wimFile)));

            IntPtr src;
            Status("Opening " + source);
            int total, found;
            int rc = OpenRead(source, progressFunc, out src, out total, out found);
            if (rc == ErrPartsMissing) return Finish(2, OpenText(rc, source, total, found));
            if (rc != 0) return Failed(rc, "Open " + source);
            if (total > 1) Status("Split WIM: " + total + " parts");
            // a new file is written next to the old one first (as for a backup)
            string tmp = append ? null : wimFile + ".tmp";
            string step;
            int first = 0, last = 0;
            try
            {
                int count = ImageCount(src);
                if (index < 0 || index > count)
                    return Finish(87, "Image " + index + " is not in " + source + " (" + count + " images).");
                IntPtr dest;
                if (append)
                {
                    Status("Opening " + wimFile);
                    rc = wimlib_open_wim_with_progress(wimFile, OpenWriteAccess, out dest, progressFunc, IntPtr.Zero);
                    if (rc != 0) return Failed(rc, "Open " + wimFile);
                }
                else
                {
                    rc = wimlib_create_new_wim(ctype, out dest);
                    if (rc != 0) return Failed(rc, "Create WIM");
                    wimlib_register_progress_function(dest, progressFunc, IntPtr.Zero);
                }
                // the destination is written and freed before the source is freed
                try
                {
                    first = ImageCount(dest) + 1;
                    // the boot image of the source stays bootable (all images: the
                    // one marked in the source), unless the target has its own
                    int srcBoot = BootIndex(src);
                    int flags = srcBoot > 0 && (index == 0 || index == srcBoot) && BootIndex(dest) == 0 ? ExportBoot : 0;
                    step = "Export " + (index == 0 ? "all images" : "image " + index) + " of " + source;
                    Status(step);
                    Progress(-1, "export", true);
                    rc = wimlib_export_image(src, index == 0 ? AllImages : index, dest,
                        index != 0 && name.Length > 0 ? name : null,
                        index != 0 && desc.Length > 0 ? desc : null, flags);
                    if (rc == 0)
                    {
                        last = ImageCount(dest);
                        if (append)
                        {
                            step = "Write images " + first + "-" + last + " to " + wimFile;
                            Status("Adding image(s) " + first + "-" + last + " to " + wimFile);
                            rc = wimlib_overwrite(dest, 0, threads);
                        }
                        else
                        {
                            step = "Write " + tmp;
                            if (File.Exists(tmp)) File.Delete(tmp);
                            Status("Writing " + tmp);
                            rc = wimlib_write(dest, tmp, AllImages, ctype == CompressionLzms ? WriteSolid : 0, threads);
                        }
                    }
                }
                finally { wimlib_free(dest); }
            }
            finally { wimlib_free(src); }
            if (rc != 0)
            {
                if (tmp != null) try { File.Delete(tmp); } catch (Exception) { }
                return Failed(rc, step);
            }
            if (tmp != null)
            {
                if (File.Exists(wimFile)) File.Delete(wimFile);
                File.Move(tmp, wimFile);
            }
            return Finish(0, "OK images " + first + "-" + last);
        }

        // split parts: wimlib's progress for one part -> the whole file
        static bool splitting;
        static long splitDoneBefore, splitTotal;
        static int splitPart, splitParts;

        static int Split(Dictionary<string, string> job)
        {
            string source = Get(job, "source"), swm = Get(job, "swm");
            long partMb = ParseLong(Get(job, "part_mb"), 4000);
            string why = SplitProblem(source, swm);
            if (why != null) return Finish(87, "Refused: " + why + " (" + source + " -> " + swm + ")");
            if (partMb < 1) return Finish(87, "Part size " + partMb + " MB");
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(swm)));
            // old parts of the same name (the GUI asked) would mix with the new ones
            foreach (string old in SwmParts(swm)) { Status("Deleting the old part " + old); File.Delete(old); }

            IntPtr wim;
            Status("Opening " + source);
            int rc = wimlib_open_wim_with_progress(source, 0, out wim, progressFunc, IntPtr.Zero);
            if (rc != 0) return Failed(rc, "Open " + source);
            try
            {
                Status("Splitting into parts of " + partMb + " MB: " + swm);
                splitting = true;
                Progress(-1, "split 0 0 0 0", true);
                rc = wimlib_split(wim, swm, (ulong)partMb * MiB, 0);
            }
            finally { splitting = false; wimlib_free(wim); }
            if (rc != 0)
            {
                // a half written set of parts is of no use
                foreach (string part in SwmParts(swm)) try { File.Delete(part); } catch (Exception) { }
                return Failed(rc, "Split " + source);
            }
            List<string> made = SwmParts(swm);
            foreach (string part in made) Status("  " + part + "  (" + new FileInfo(part).Length / MiB + " MB)");
            return Finish(0, "OK " + made.Count + " parts");
        }

        static int Join(Dictionary<string, string> job)
        {
            string wimFile = Get(job, "wim");
            List<string> parts = new List<string>();
            foreach (string part in Get(job, "parts").Split('|'))
                if (part.Trim().Length > 0) parts.Add(part.Trim());
            string why = JoinProblem(parts, wimFile);
            if (why != null) return Finish(87, "Refused: " + why + " (" + wimFile + ")");
            foreach (string part in parts)
                if (!File.Exists(part)) return Finish(2, "Not found: " + part);
            Directory.CreateDirectory(Path.GetDirectoryName(Path.GetFullPath(wimFile)));
            // written next to an old file first: a failed join leaves it as it was
            string tmp = wimFile + ".tmp";
            if (File.Exists(tmp)) File.Delete(tmp);
            Status("Joining " + parts.Count + " parts into " + tmp);
            Progress(-1, "join", true);
            int rc = wimlib_join_with_progress(parts.ToArray(), (uint)parts.Count, tmp, 0, 0, progressFunc, IntPtr.Zero);
            if (rc != 0)
            {
                try { File.Delete(tmp); } catch (Exception) { }
                return Failed(rc, "Join " + parts[0]);
            }
            if (File.Exists(wimFile)) File.Delete(wimFile);
            File.Move(tmp, wimFile);
            return Finish(0, "OK " + wimFile);
        }

        static int Apply(Dictionary<string, string> job)
        {
            string wimFile = Get(job, "wim"), target = Get(job, "target");
            string label = CleanLabel(Get(job, "label"));
            int index = (int)ParseLong(Get(job, "index"), 1);
            string folder = Get(job, "folder");
            if (folder.Length > 0) return ApplyToFolder(wimFile, index, folder, Get(job, "root"));
            if (target.Length == 0) return Finish(87, "No target volume.");
            char letter = char.ToUpperInvariant(target[0]);
            string why = TargetProblem(letter, wimFile, Get(job, "root"));
            if (why != null) return Finish(87, "Refused: " + why + " (" + letter + ":)");
            if (!File.Exists(wimFile)) return Finish(2, "Not found: " + wimFile);

            IntPtr wim;
            int total, found;
            int rc = OpenRead(wimFile, progressFunc, out wim, out total, out found);
            if (rc == ErrPartsMissing) return Finish(2, OpenText(rc, wimFile, total, found));
            if (rc != 0) return Failed(rc, "Open " + wimFile);
            if (total > 1) Status("Split WIM: " + total + " parts");
            try
            {
                int count = ImageCount(wim);
                if (index < 1 || index > count)
                    return Finish(87, "Image " + index + " is not in " + wimFile + " (" + count + " images).");
                long need = ParseLong(Property(wim, index, "TOTALBYTES"), 0);

                Progress(-1, "format", true);
                Status("Formatting " + letter + ": (NTFS, label \"" + label + "\")");
                int frc = FormatVolume(letter, label);
                if (frc != 0) return Finish(frc, "Format " + letter + ": failed (code " + frc + ").");
                DriveInfo d = WaitReady(letter);
                if (d == null) return Finish(21, letter + ": is not ready after the format.");
                if (need > 0 && d.TotalFreeSpace < need)
                    return Finish(112, string.Format("{0}: has {1} MB free, image {2} needs {3} MB.", letter, d.TotalFreeSpace / MiB, index, need / MiB));

                Status("Applying image " + index + " (" + Property(wim, index, "NAME") + ") to " + letter + ":\\");
                rc = wimlib_extract_image(wim, index, letter + ":\\", 0);
                if (rc != 0) return Failed(rc, "Apply image " + index + " to " + letter + ":");
            }
            finally { wimlib_free(wim); }
            string fw = Get(job, "boot");
            if (fw.Length == 0) return Finish(0, "OK " + letter + ":");
            int brc = WriteBootFiles(job, letter, fw);
            if (brc != 0) return Finish(brc, "The image was applied to " + letter + ":, but the boot files were not written (code " + brc + ").");
            return Finish(0, "OK " + letter + ": + boot files (" + fw + ")");
        }

        // ---------- boot files (bcdboot) ----------

        // boot = UEFI | BIOS | ALL; sys_disk, sys_part, sys_offset name the
        // system partition. A partition without a drive letter gets one for
        // bcdboot; that letter is removed again afterwards.
        static int WriteBootFiles(Dictionary<string, string> job, char letter, string fw)
        {
            if (fw != "UEFI" && fw != "BIOS" && fw != "ALL") return Finish(87, "Unknown firmware: " + fw);
            Progress(-1, "boot", true);
            string windows = letter + ":\\Windows";
            if (!Directory.Exists(Path.Combine(windows, "System32"))) { Status("No Windows in the image: " + windows + " is missing."); return 3; }

            int disk = (int)ParseLong(Get(job, "sys_disk"), -1), part = (int)ParseLong(Get(job, "sys_part"), -1);
            long offset = ParseLong(Get(job, "sys_offset"), -1);
            string err;
            List<DiskPartition> parts = QueryPartitions(out err);
            if (parts == null) { Status("Partitions not read: " + err); return 1; }
            DiskPartition sys = null;
            foreach (DiskPartition p in parts)
                if (p.Disk == disk && p.Number == part && p.Offset == offset) sys = p;
            if (sys == null) { Status("The system partition (disk " + disk + ", partition " + part + ") was not found; the disks have changed."); return 1168; }
            string why = SystemPartitionProblem(sys, fw);
            if (why != null) { Status("Refused: " + why + " (disk " + disk + ", partition " + part + ")"); return 87; }

            char sysLetter = sys.Letter;
            bool assigned = false;
            int rc;
            try
            {
                if (sysLetter == '\0')
                {
                    sysLetter = FreeLetter();
                    if (sysLetter == '\0') { Status("No free drive letter for the system partition."); return 1; }
                    Status("Drive letter " + sysLetter + ": for disk " + disk + ", partition " + part);
                    string sel = "-DiskNumber " + disk + " -PartitionNumber " + part;
                    rc = RunPs("Add-PartitionAccessPath " + sel + " -AccessPath '" + sysLetter + ":\\' -ErrorAction Stop", true);
                    if (rc != 0)
                        rc = RunPs("Set-Partition " + sel + " -NewDriveLetter " + sysLetter + " -ErrorAction Stop", true);
                    if (rc != 0) { Status("The drive letter was not assigned."); return rc; }
                    assigned = true;
                    if (!WaitFolder(sysLetter + ":\\")) { Status(sysLetter + ": is not ready."); return 21; }
                }
                // BIOS starts the active partition of an MBR disk
                if (fw != "UEFI" && !sys.Gpt && !sys.Active)
                {
                    Status("Disk " + disk + ": partition " + part + " is made the active partition.");
                    rc = RunPs("Set-Partition -DiskNumber " + disk + " -PartitionNumber " + part + " -IsActive $true -ErrorAction Stop", true);
                    if (rc != 0) { Status("The partition was not made active."); return rc; }
                }
                string args = windows + " /s " + sysLetter + ": /f " + fw;
                string bootLang = Get(job, "boot_lang").ToLowerInvariant();
                if (IsLocale(bootLang)) args += " /l " + bootLang;
                Status("bcdboot " + args);
                rc = RunTool(BcdbootPath(), args);
                if (rc != 0) Status("bcdboot ended with code " + rc + ".");
                return rc;
            }
            finally
            {
                if (assigned)
                {
                    if (RunPs("Remove-PartitionAccessPath -DiskNumber " + disk + " -PartitionNumber " + part + " -AccessPath '" + sysLetter + ":\\' -ErrorAction Stop", true) == 0)
                        Status("Drive letter " + sysLetter + ": removed again.");
                }
            }
        }

        // a locale for bcdboot /l, e.g. ko-kr, en-us, zh-tw, sr-latn-rs
        public static bool IsLocale(string s)
        {
            return !string.IsNullOrEmpty(s) && System.Text.RegularExpressions.Regex.IsMatch(s, "^[a-z]{2,3}(-[a-z]{2,4}){1,2}$");
        }

        // null, or the key of the reason in lang\*.ini
        public static string SystemPartitionProblem(DiskPartition p, string fw)
        {
            if (p.IsSystem) return "bk.err_sys_running";
            if (p.IsMsr) return "bk.err_sys_msr";
            if (fw != "BIOS" && !string.Equals(p.FileSystem, "FAT32", StringComparison.OrdinalIgnoreCase)) return "bk.err_sys_fat32";
            if (fw == "BIOS" && p.FileSystem.Length == 0) return "bk.err_sys_no_fs";
            return null;
        }

        static char FreeLetter()
        {
            List<char> used = new List<char>();
            foreach (DriveInfo d in DriveInfo.GetDrives()) used.Add(char.ToUpperInvariant(d.Name[0]));
            foreach (char c in "STUVWXYZRQPONMLKJIHGFED")
                if (!used.Contains(c)) return c;
            return '\0';
        }

        static bool WaitFolder(string dir)
        {
            for (int n = 0; n < 20; n++)
            {
                if (Directory.Exists(dir)) return true;
                System.Threading.Thread.Sleep(500);
            }
            return false;
        }

        static string BcdbootPath()
        {
            string native = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.Windows), @"Sysnative\bcdboot.exe");
            return !Environment.Is64BitProcess && File.Exists(native) ? native : Path.Combine(Environment.SystemDirectory, "bcdboot.exe");
        }

        // a program of Windows; its output goes to status.txt
        static int RunTool(string exe, string args)
        {
            ProcessStartInfo psi = new ProcessStartInfo(exe, args);
            psi.UseShellExecute = false;
            psi.CreateNoWindow = true;
            psi.RedirectStandardError = true;
            psi.RedirectStandardOutput = true;
            using (Process p = Process.Start(psi))
            {
                string output = p.StandardOutput.ReadToEnd() + p.StandardError.ReadToEnd();
                p.WaitForExit();
                foreach (string l in output.Replace("\r\n", "\n").Split('\n'))
                    if (l.Trim().Length > 0) Status("  " + l.TrimEnd());
                return p.ExitCode;
            }
        }

        // PowerShell (encoded, no quoting problems); errors go to status.txt when log is set
        static int RunPs(string script, bool log)
        {
            string output, error;
            int rc = RunPs(script, out output, out error);
            if (log)
                foreach (string l in error.Replace("\r\n", "\n").Split('\n'))
                    if (l.Trim().Length > 0) Status("  " + l.TrimEnd());
            return rc;
        }

        static int RunPs(string script, out string output, out string error)
        {
            string ps = Path.Combine(Environment.SystemDirectory, @"WindowsPowerShell\v1.0\powershell.exe");
            string encoded = Convert.ToBase64String(Encoding.Unicode.GetBytes("$ProgressPreference='SilentlyContinue'\n" + script));
            ProcessStartInfo psi = new ProcessStartInfo(ps, "-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " + encoded);
            psi.UseShellExecute = false;
            psi.CreateNoWindow = true;
            psi.RedirectStandardError = true;
            psi.RedirectStandardOutput = true;
            psi.StandardOutputEncoding = Encoding.UTF8;
            using (Process p = Process.Start(psi))
            {
                System.Threading.Tasks.Task<string> err = p.StandardError.ReadToEndAsync();
                output = p.StandardOutput.ReadToEnd();
                p.WaitForExit();
                error = err.Result;
                if (error.StartsWith("#< CLIXML")) error = ClixmlErrors(error);
                return p.ExitCode;
            }
        }

        // stderr as CLIXML: only the error lines (not the progress records)
        static string ClixmlErrors(string xml)
        {
            StringBuilder sb = new StringBuilder();
            foreach (System.Text.RegularExpressions.Match m in
                System.Text.RegularExpressions.Regex.Matches(xml, "<S S=\"Error\">(.*?)</S>"))
                sb.Append(System.Net.WebUtility.HtmlDecode(m.Groups[1].Value).Replace("_x000D_", "\r").Replace("_x000A_", "\n"));
            return sb.ToString();
        }

        // every partition of the local disks (no administrator rights needed)
        public static List<DiskPartition> QueryPartitions(out string error)
        {
            const string script =
                "$ErrorActionPreference='SilentlyContinue'\n" +
                "[Console]::OutputEncoding=[Text.Encoding]::UTF8\n" +
                "foreach($d in Get-Disk){ foreach($p in @(Get-Partition -DiskNumber $d.Number)){ $v=Get-Volume -Partition $p\n" +
                " '{0}|{1}|{2}|{3}|{4}|{5}|{6}|{7}|{8}|{9}|{10}' -f $p.DiskNumber,$p.PartitionNumber,([string]$p.DriveLetter).Trim([char]0)," +
                "$p.GptType,$p.MbrType,$p.IsActive,$p.IsSystem,$p.Size,$p.Offset,$v.FileSystem,$d.PartitionStyle } }";
            string output;
            try { RunPs(script, out output, out error); }
            catch (Exception ex) { error = ex.Message; return null; }
            List<DiskPartition> list = new List<DiskPartition>();
            foreach (string line in output.Replace("\r\n", "\n").Split('\n'))
            {
                string[] f = line.Trim().Split('|');
                if (f.Length < 11) continue;
                DiskPartition p = new DiskPartition();
                p.Disk = (int)ParseLong(f[0], -1);
                p.Number = (int)ParseLong(f[1], -1);
                p.Letter = f[2].Length > 0 && char.IsLetter(f[2][0]) ? char.ToUpperInvariant(f[2][0]) : '\0';
                p.GptType = f[3];
                p.MbrType = (int)ParseLong(f[4], 0);
                p.Active = string.Equals(f[5], "True", StringComparison.OrdinalIgnoreCase);
                p.IsSystem = string.Equals(f[6], "True", StringComparison.OrdinalIgnoreCase);
                p.Size = ParseLong(f[7], 0);
                p.Offset = ParseLong(f[8], -1);
                p.FileSystem = f[9];
                p.Style = f[10].ToUpperInvariant();
                if (p.Disk >= 0 && p.Number >= 0) list.Add(p);
            }
            if (list.Count == 0) { if (error.Length == 0) error = "Get-Partition returned nothing."; return null; }
            error = null;
            return list;
        }

        // apply one image into an empty (or new) folder; nothing is formatted
        static int ApplyToFolder(string wimFile, int index, string folder, string buildRoot)
        {
            string why = FolderTargetProblem(folder, wimFile, buildRoot);
            if (why != null) return Finish(87, "Refused: " + why + " (" + folder + ")");
            if (!File.Exists(wimFile)) return Finish(2, "Not found: " + wimFile);
            string dir = Path.GetFullPath(folder);

            IntPtr wim;
            int total, found;
            int rc = OpenRead(wimFile, progressFunc, out wim, out total, out found);
            if (rc == ErrPartsMissing) return Finish(2, OpenText(rc, wimFile, total, found));
            if (rc != 0) return Failed(rc, "Open " + wimFile);
            if (total > 1) Status("Split WIM: " + total + " parts");
            try
            {
                int count = ImageCount(wim);
                if (index < 1 || index > count)
                    return Finish(87, "Image " + index + " is not in " + wimFile + " (" + count + " images).");
                long need = ParseLong(Property(wim, index, "TOTALBYTES"), 0);
                Directory.CreateDirectory(dir);
                DriveInfo d = new DriveInfo(Root(dir));
                if (need > 0 && d.TotalFreeSpace < need)
                    return Finish(112, string.Format("{0} has {1} MB free, image {2} needs {3} MB.", d.Name, d.TotalFreeSpace / MiB, index, need / MiB));

                Status("Applying image " + index + " (" + Property(wim, index, "NAME") + ") to " + dir);
                rc = wimlib_extract_image(wim, index, dir, 0);
                if (rc != 0) return Failed(rc, "Apply image " + index + " to " + dir);
            }
            finally { wimlib_free(wim); }
            return Finish(0, "OK " + dir);
        }

        // Format-Volume of Windows (Storage module), NTFS, quick format
        static int FormatVolume(char letter, string label)
        {
            string ps = Path.Combine(Environment.SystemDirectory, @"WindowsPowerShell\v1.0\powershell.exe");
            string cmd = "Format-Volume -DriveLetter " + letter + " -FileSystem NTFS -NewFileSystemLabel '" + label
                + "' -Force -Confirm:$false -ErrorAction Stop | Out-Null";
            ProcessStartInfo psi = new ProcessStartInfo(ps, "-NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"" + cmd + "\"");
            psi.UseShellExecute = false;
            psi.CreateNoWindow = true;
            psi.RedirectStandardError = true;
            psi.RedirectStandardOutput = true;
            using (Process p = Process.Start(psi))
            {
                p.StandardOutput.ReadToEnd();
                string err = p.StandardError.ReadToEnd();
                p.WaitForExit();
                foreach (string l in err.Replace("\r\n", "\n").Split('\n'))
                    if (l.Trim().Length > 0) Status("  " + l.TrimEnd());
                return p.ExitCode;
            }
        }

        static DriveInfo WaitReady(char letter)
        {
            for (int n = 0; n < 40; n++)
            {
                try
                {
                    DriveInfo d = new DriveInfo(letter + ":\\");
                    if (d.IsReady && d.DriveFormat == "NTFS") return d;
                }
                catch (Exception) { }
                System.Threading.Thread.Sleep(500);
            }
            return null;
        }

        // wimlib progress -> "P <percent> <phase> ..." lines (at most about
        // one per second, or when the percent changes); also the cancel check
        static int OnProgress(int msg, IntPtr info, IntPtr ctx)
        {
            try
            {
                switch (msg)
                {
                    case MsgScanDentry:
                        // scan: source, cur_path, status (+pad), union, then
                        // num_dirs_scanned (32), num_nondirs_scanned (40), num_bytes_scanned (48)
                        Progress(-1, "scan " + Marshal.ReadInt64(info, 32) + " " + Marshal.ReadInt64(info, 40) + " " + Marshal.ReadInt64(info, 48), false);
                        break;
                    case MsgWriteStreams:
                        // write_streams: total_bytes (0), total_streams (8), completed_bytes (16)
                        if (splitting)
                        {
                            // of the current part only (wimlib 1.13.4+): add the parts before
                            long done = splitDoneBefore + Marshal.ReadInt64(info, 16);
                            SplitProgress(done);
                        }
                        else Bytes("write", Marshal.ReadInt64(info, 16), Marshal.ReadInt64(info, 0));
                        break;
                    case MsgSplitBeginPart:
                    case MsgSplitEndPart:
                        // split: total_bytes (0), completed_bytes (8), cur_part_number (16), total_parts (20)
                        splitTotal = Marshal.ReadInt64(info, 0);
                        splitDoneBefore = Marshal.ReadInt64(info, 8);
                        splitPart = Marshal.ReadInt32(info, 16);
                        splitParts = Marshal.ReadInt32(info, 20);
                        SplitProgress(splitDoneBefore);
                        break;
                    case MsgExtractStreams:
                        // extract: image, flags, 4 pointers, total_bytes (40), completed_bytes (48)
                        Bytes("apply", Marshal.ReadInt64(info, 48), Marshal.ReadInt64(info, 40));
                        break;
                    case MsgVerifyIntegrity:
                        // integrity: total_bytes (0), completed_bytes (8)
                        Bytes("integrity", Marshal.ReadInt64(info, 8), Marshal.ReadInt64(info, 0));
                        break;
                    case MsgBeginVerifyImage:
                        // verify_image: wimfile, total_images (8), current_image (12)
                        Progress(-1, "vimage " + Marshal.ReadInt32(info, 12) + " " + Marshal.ReadInt32(info, 8), true);
                        break;
                    case MsgVerifyStreams:
                        // verify_streams: wimfile, total_streams (8), total_bytes (16),
                        // completed_streams (24), completed_bytes (32)
                        Bytes("verify", Marshal.ReadInt64(info, 32), Marshal.ReadInt64(info, 16));
                        break;
                    case MsgExtractFileStructure: Progress(-1, "structure", false); break;
                    case MsgExtractMetadata:
                    case MsgWriteMetadataBegin: Progress(-1, "metadata", false); break;
                }
                int now = Environment.TickCount;
                if (!cancelled && unchecked(now - lastCancelCheck) > 300)
                {
                    lastCancelCheck = now;
                    cancelled = File.Exists(cancelFile);
                }
            }
            catch (Exception) { }
            return cancelled ? StatusAbort : StatusContinue;
        }

        static void SplitProgress(long done)
        {
            int pct = splitTotal > 0 ? (int)Math.Min(100, done * 100 / splitTotal) : -1;
            Progress(pct, "split " + done + " " + splitTotal + " " + splitPart + " " + splitParts, false);
        }

        static void Bytes(string phase, long done, long total)
        {
            int pct = total > 0 ? (int)Math.Min(100, done * 100 / total) : -1;
            Progress(pct, phase + " " + done + " " + total, false);
        }

        static void Progress(int pct, string text, bool force)
        {
            if (!force && pct == lastPct && (DateTime.Now - lastLine).TotalSeconds < 1) return;
            lastPct = pct;
            lastLine = DateTime.Now;
            Status("P " + pct + " " + text);
        }

        static void Status(string line)
        {
            using (FileStream fs = new FileStream(statusFile, FileMode.Append, FileAccess.Write, FileShare.ReadWrite))
            using (StreamWriter w = new StreamWriter(fs, new UTF8Encoding(false)))
                w.WriteLine(line);
        }

        static int Finish(int code, string text)
        {
            try { Status("EXIT " + code + " " + text); } catch (Exception) { }
            return code;
        }

        // a wimlib error: its messages from wimlib_errors.txt, then EXIT
        static int Failed(int rc, string step)
        {
            try
            {
                if (File.Exists(errorFile))
                {
                    string[] lines = File.ReadAllLines(errorFile);
                    for (int n = Math.Max(0, lines.Length - 20); n < lines.Length; n++)
                        if (lines[n].Trim().Length > 0) Status("  " + lines[n].TrimEnd());
                }
            }
            catch (Exception) { }
            if (rc == ErrAborted) return Finish(ErrAborted, "Cancelled: " + step);
            return Finish(rc, step + ": " + ErrorText(rc));
        }
    }
}
