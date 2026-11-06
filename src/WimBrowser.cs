// ============================================================
//  7z_wimlib - WimBrowser.cs
//  The browse tab: the files of a WIM / ESD file, read with 7-Zip
//  (lib\7z.exe, downloaded when missing, see Libs.cs).
//   - 7z l -ba lists one image (a WIM with several images: "<n>\*",
//     the image chosen in the list; one image: the whole file). The
//     folders are shown as a tree, the content of a folder as a list.
//   - Extract: the selected files / folders go into a chosen folder
//     (7z x into a hidden work folder there, then moved up, so that
//     the path inside the image is not repeated).
//   - Open: a file is extracted to %TEMP%\7z_wimlib_view and opened
//     with its program (a program file: its folder is shown instead).
//   - 7-Zip: the WIM file opened in 7-Zip's own file manager.
//  Nothing is written to the WIM file.
// ============================================================
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Windows.Forms;

namespace Dvt
{
    public class WimBrowser : UserControl
    {
        readonly string rootDir;
        readonly Action<string> log;
        readonly Func<string, List<WimImage>> readImages;   // wimlib, null = not readable

        readonly TextBox txtWim;
        readonly ComboBox cboImage;
        readonly Button btnRead, btnUp, btnExtract, btnOpenFile, btnFm, btnCancel;
        readonly Label lblPath, lblState;
        readonly FlatProgress bar;
        readonly TreeView tree;
        readonly ListView list;
        readonly ImageList icons;
        readonly Dictionary<string, int> iconIndex = new Dictionary<string, int>(StringComparer.OrdinalIgnoreCase);

        string wimFile = "";                  // the file of the tree shown
        Node top, current;
        List<Row> rows = new List<Row>();
        int sortColumn;
        bool sortDown;
        Process proc;                         // 7z.exe that runs (listing or extracting)
        bool listing, cancelRequested;

        class Node
        {
            public string Name = "", Path = "";   // Path: inside the archive ("" = root, "2\Windows")
            public Node Parent;
            public readonly Dictionary<string, Node> Dirs = new Dictionary<string, Node>(StringComparer.OrdinalIgnoreCase);
            public readonly List<Row> Files = new List<Row>();
            public string Modified = "", Attr = "";
        }

        class Row
        {
            public string Name = "", Path = "", Modified = "", Attr = "";
            public long Size = -1;
            public Node Dir;                  // a folder
        }

        class ImageItem
        {
            public string Prefix = "", Text = "";
            public override string ToString() { return Text; }
        }

        public WimBrowser(string root, Action<string> logLine, Func<string, List<WimImage>> images)
        {
            rootDir = root;
            log = logLine;
            readImages = images;
            Dock = DockStyle.Fill;

            TableLayoutPanel t = new TableLayoutPanel();
            t.Dock = DockStyle.Fill;
            t.ColumnCount = 2;
            t.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, Ui.S(150)));
            t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            t.RowCount = 4;
            t.RowStyles.Add(new RowStyle(SizeType.Absolute, Ui.S(34)));
            t.RowStyles.Add(new RowStyle(SizeType.Absolute, Ui.S(34)));
            t.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            t.RowStyles.Add(new RowStyle(SizeType.Absolute, Ui.S(38)));

            txtWim = new TextBox();
            txtWim.Width = Ui.S(420);
            txtWim.Margin = new Padding(0, Ui.S(5), Ui.S(6), 0);
            Button btnBrowse = Ui.NewButton(L.T("qbt.btn_browse"), 100);
            btnRead = Ui.NewButton(L.T("br.btn_read"), 100);
            btnFm = Ui.NewButton(L.T("br.btn_7zfm"), 130);
            AddRow(t, 0, L.T("bk.row_wim"), txtWim, btnBrowse, btnRead, btnFm);

            cboImage = new ComboBox();
            cboImage.DropDownStyle = ComboBoxStyle.DropDownList;
            cboImage.Width = Ui.S(420);
            cboImage.Margin = new Padding(0, Ui.S(5), Ui.S(6), 0);
            btnUp = Ui.NewButton(L.T("br.btn_up"), 80);
            lblPath = Ui.NewFlowLabel("");
            lblPath.AutoEllipsis = true;
            lblPath.ForeColor = Color.DimGray;
            AddRow(t, 1, L.T("bk.row_image"), cboImage, btnUp, lblPath);

            icons = new ImageList();
            icons.ColorDepth = ColorDepth.Depth32Bit;
            icons.ImageSize = new Size(Ui.S(16), Ui.S(16));

            SplitContainer split = new SplitContainer();
            split.Dock = DockStyle.Fill;
            split.FixedPanel = FixedPanel.Panel1;
            split.Panel1MinSize = Ui.S(120);
            // the real width is known only when the tab is first shown
            bool splitSet = false;
            split.SizeChanged += delegate
            {
                if (splitSet || split.Width < Ui.S(500)) return;
                splitSet = true;
                split.SplitterDistance = Math.Max(Ui.S(200), split.Width / 3);
            };
            tree = new TreeView();
            tree.Dock = DockStyle.Fill;
            tree.HideSelection = false;
            tree.ImageList = icons;
            tree.PathSeparator = "\\";
            split.Panel1.Controls.Add(tree);
            list = new ListView();
            list.Dock = DockStyle.Fill;
            list.View = View.Details;
            list.FullRowSelect = true;
            list.HideSelection = false;
            list.VirtualMode = true;
            list.SmallImageList = icons;
            list.Columns.Add(L.T("br.col_name"), Ui.S(300));
            list.Columns.Add(L.T("br.col_size"), Ui.S(100), HorizontalAlignment.Right);
            list.Columns.Add(L.T("br.col_modified"), Ui.S(140));
            list.Columns.Add(L.T("br.col_attr"), Ui.S(60));
            split.Panel2.Controls.Add(list);
            t.Controls.Add(split, 0, 2);
            t.SetColumnSpan(split, 2);

            FlowLayoutPanel f = Ui.NewFlow();
            f.Margin = new Padding(0, Ui.S(4), 0, 0);
            btnExtract = Ui.NewButton(L.T("br.btn_extract"), 150);
            Ui.Primary(btnExtract, Palette.Backup);
            btnOpenFile = Ui.NewButton(L.T("br.btn_open"), 110);
            bar = new FlatProgress();
            bar.Width = Ui.S(200);
            bar.Height = Ui.S(4);
            bar.Margin = new Padding(Ui.S(8), Ui.S(15), Ui.S(8), 0);
            btnCancel = Ui.NewButton(L.T("bk.btn_cancel"), 80);
            btnCancel.Enabled = false;
            lblState = Ui.NewFlowLabel(L.T("br.none"));
            f.Controls.AddRange(new Control[] { btnExtract, btnOpenFile, bar, btnCancel, lblState });
            t.Controls.Add(f, 0, 3);
            t.SetColumnSpan(f, 2);
            Controls.Add(t);

            btnBrowse.Click += delegate { ChooseWim(); };
            btnRead.Click += delegate { OpenWim(true); };
            txtWim.KeyDown += delegate(object s, KeyEventArgs e) { if (e.KeyCode == Keys.Enter) { e.SuppressKeyPress = true; OpenWim(true); } };
            btnFm.Click += delegate { OpenIn7zFm(); };
            cboImage.SelectedIndexChanged += delegate { if (!listing) ListImage(); };
            btnUp.Click += delegate { if (current != null && current.Parent != null) ShowFolder(current.Parent, true); };
            tree.BeforeExpand += delegate(object s, TreeViewCancelEventArgs e) { FillTreeNode(e.Node); };
            tree.AfterSelect += delegate(object s, TreeViewEventArgs e)
            {
                Node n = e.Node.Tag as Node;
                if (n != null && n != current) ShowFolder(n, false);
            };
            list.RetrieveVirtualItem += OnRetrieveItem;
            list.SelectedIndexChanged += delegate { UpdateButtons(); };
            list.VirtualItemsSelectionRangeChanged += delegate { UpdateButtons(); };
            list.DoubleClick += delegate { OpenSelected(); };
            list.KeyDown += delegate(object s, KeyEventArgs e)
            {
                if (e.KeyCode == Keys.Enter) OpenSelected();
                else if (e.KeyCode == Keys.Back && current != null && current.Parent != null) ShowFolder(current.Parent, true);
            };
            list.ColumnClick += delegate(object s, ColumnClickEventArgs e)
            {
                sortDown = e.Column == sortColumn ? !sortDown : false;
                sortColumn = e.Column;
                if (current != null) ShowFolder(current, false);
            };
            btnExtract.Click += delegate { ExtractSelected(); };
            btnOpenFile.Click += delegate { OpenSelected(); };
            btnCancel.Click += delegate { Cancel(); };
            UpdateButtons();
            CleanViewFolder();
        }

        static void AddRow(TableLayoutPanel t, int row, string label, params Control[] controls)
        {
            Label l = Ui.NewLabel(label);
            l.Font = Ui.FontBold;
            l.ForeColor = Color.DimGray;
            // at the top of its row (the last row of a grid takes the rest of the height)
            l.Dock = DockStyle.Top;
            l.Height = Ui.S(34);
            t.Controls.Add(l, 0, row);
            FlowLayoutPanel f = Ui.NewFlow();
            f.Controls.AddRange(controls);
            t.Controls.Add(f, 1, row);
        }

        // ---------- state ----------

        // an extraction runs (a listing is only read and can be dropped)
        public bool Busy { get { return proc != null && !listing; } }

        public string WimPath { get { return txtWim.Text.Trim(); } }

        public void Cancel()
        {
            Process p = proc;
            if (p == null) return;
            cancelRequested = true;
            try { if (!p.HasExited) p.Kill(); } catch (Exception) { }
        }

        void UpdateButtons()
        {
            bool running = proc != null;
            bool any = list.SelectedIndices.Count > 0;
            btnRead.Enabled = txtWim.Enabled = !running;
            cboImage.Enabled = !running && cboImage.Items.Count > 1;
            btnUp.Enabled = !running && current != null && current.Parent != null;
            btnExtract.Enabled = !running && top != null && any;
            btnOpenFile.Enabled = !running && top != null && any;
            btnCancel.Enabled = running;
            tree.Enabled = list.Enabled = !running || !listing;
        }

        void SetState(string text) { lblState.Text = text; }

        bool Ensure7zip()
        {
            string err = Libs.Ensure7zip(FindForm(), rootDir);
            if (err == null) return true;
            log(L.F("br.no_7zip", err).Replace("\r\n", " "));
            Ui.Info(L.F("br.no_7zip", err), MessageBoxIcon.Warning);
            return false;
        }

        // ---------- open a WIM file ----------

        void ChooseWim()
        {
            using (OpenFileDialog dlg = new OpenFileDialog())
            {
                dlg.Filter = "WIM (*.wim;*.esd;*.swm)|*.wim;*.esd;*.swm|*.*|*.*";
                try
                {
                    string cur = WimPath;
                    if (cur.Length > 0 && Directory.Exists(Path.GetDirectoryName(cur))) dlg.InitialDirectory = Path.GetDirectoryName(cur);
                }
                catch (Exception) { }
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                txtWim.Text = dlg.FileName;
                OpenWim(true);
            }
        }

        // another tab hands a file (and an image) over
        public void ShowWim(string wim, int image)
        {
            if (proc != null) return;
            txtWim.Text = wim;
            OpenWim(true);
            if (image > 0)
                for (int n = 0; n < cboImage.Items.Count; n++)
                    if (((ImageItem)cboImage.Items[n]).Prefix == image.ToString()) { cboImage.SelectedIndex = n; break; }
        }

        public void SetDefault(string wim)
        {
            if (WimPath.Length == 0 && File.Exists(wim)) txtWim.Text = wim;
        }

        // the images (wimlib), then the listing of the first one
        void OpenWim(bool show)
        {
            if (proc != null) return;
            string wim = WimPath;
            if (wim.Length == 0 || !File.Exists(wim))
            {
                if (show) Ui.Info(L.F("msg.not_found", wim.Length == 0 ? "-" : wim), MessageBoxIcon.Warning);
                return;
            }
            if (!Ensure7zip()) return;
            List<WimImage> images = readImages(wim);
            listing = true;               // no listing from the combo while it is filled
            cboImage.Items.Clear();
            if (images != null && images.Count > 1)
            {
                foreach (WimImage w in images)
                    cboImage.Items.Add(new ImageItem
                    {
                        Prefix = w.Index.ToString(),
                        Text = L.F("bk.image_line", w.Index, w.Name.Length > 0 ? w.Name : L.T("bk.no_name"),
                            w.TotalBytes >= 0 ? (w.TotalBytes / 1073741824.0).ToString("0.0") + " GB" : "?")
                    });
            }
            else
            {
                // one image (or wimlib could not tell): the whole file
                string name = images != null && images.Count == 1 && images[0].Name.Length > 0 ? images[0].Name : Path.GetFileName(wim);
                cboImage.Items.Add(new ImageItem { Prefix = "", Text = images != null && images.Count == 1 ? "1: " + name : name });
            }
            cboImage.SelectedIndex = 0;
            listing = false;
            ListImage();
        }

        // 7z l of the chosen image, read in the background
        void ListImage()
        {
            ImageItem img = cboImage.SelectedItem as ImageItem;
            string wim = WimPath;
            if (img == null || proc != null || !File.Exists(wim)) return;
            string args = "l -ba -sccUTF-8 \"" + wim + "\"" + (img.Prefix.Length > 0 ? " \"" + img.Prefix + "\\*\"" : "");
            ProcessStartInfo psi = SevenZipStart(args);
            psi.StandardOutputEncoding = Encoding.UTF8;
            listing = true;
            top = current = null;
            rows = new List<Row>();
            list.VirtualListSize = 0;
            tree.Nodes.Clear();
            lblPath.Text = "";
            bar.Style = ProgressBarStyle.Marquee;
            SetState(L.T("br.listing"));
            try { proc = Process.Start(psi); }
            catch (Exception ex) { listing = false; bar.Style = ProgressBarStyle.Blocks; SetState(ex.Message); return; }
            UpdateButtons();
            Process p = proc;
            string prefix = img.Prefix, rootName = img.Text;
            DateTime started = DateTime.Now;
            Thread th = new Thread(delegate()
            {
                Node root = new Node();
                root.Name = rootName;
                root.Path = prefix;
                int count = 0;
                StringBuilder err = new StringBuilder();
                Thread errReader = new Thread(delegate() { try { err.Append(p.StandardError.ReadToEnd()); } catch (Exception) { } });
                errReader.Start();
                try
                {
                    string line;
                    while ((line = p.StandardOutput.ReadLine()) != null)
                        if (AddLine(root, prefix, line)) count++;
                }
                catch (Exception) { }
                p.WaitForExit();
                errReader.Join();
                int code = p.ExitCode;
                Post(delegate { ListDone(p, root, count, code, err.ToString(), wim, DateTime.Now - started); });
            });
            th.IsBackground = true;
            th.Start();
        }

        // "2024-04-01 16:39:55 D.... <size 12> <packed 12>  name"
        static readonly Regex LineRx = new Regex(@"^(.{19}) (.{5}) (.{12}) (.{12})  (.+)$", RegexOptions.Compiled);

        static bool AddLine(Node root, string prefix, string line)
        {
            Match m = LineRx.Match(line);
            if (!m.Success) return false;
            string path = m.Groups[5].Value;
            // alternate data streams (file:stream) are not shown
            if (path.IndexOf(':') >= 0) return false;
            string rel = path;
            if (prefix.Length > 0)
            {
                if (!path.StartsWith(prefix + "\\", StringComparison.OrdinalIgnoreCase)) return false;
                rel = path.Substring(prefix.Length + 1);
            }
            if (rel.Length == 0) return false;
            string attr = m.Groups[2].Value.Replace(".", "");
            bool dir = m.Groups[2].Value[0] == 'D';
            string modified = m.Groups[1].Value.Trim();
            string[] parts = rel.Split('\\');
            Node n = root;
            for (int i = 0; i < parts.Length - 1; i++) n = Child(n, parts[i]);
            string name = parts[parts.Length - 1];
            if (dir)
            {
                Node d = Child(n, name);
                d.Modified = modified;
                d.Attr = attr;
            }
            else
            {
                long size;
                n.Files.Add(new Row
                {
                    Name = name,
                    Path = path,
                    Size = long.TryParse(m.Groups[3].Value.Trim(), out size) ? size : -1,
                    Modified = modified,
                    Attr = attr
                });
            }
            return true;
        }

        static Node Child(Node parent, string name)
        {
            Node c;
            if (parent.Dirs.TryGetValue(name, out c)) return c;
            c = new Node();
            c.Name = name;
            c.Path = parent.Path.Length > 0 ? parent.Path + "\\" + name : name;
            c.Parent = parent;
            parent.Dirs[name] = c;
            return c;
        }

        void ListDone(Process p, Node root, int count, int code, string err, string wim, TimeSpan took)
        {
            if (proc == p) proc = null;
            try { p.Dispose(); } catch (Exception) { }
            listing = false;
            bar.Style = ProgressBarStyle.Blocks;
            bar.Value = 0;
            if (code != 0 && count == 0)
            {
                SetState(L.T("br.list_failed_short"));
                string msg = L.F("br.list_failed", wim, code, err.Trim().Length > 0 ? err.Trim() : "-");
                log(msg.Replace("\r\n", " "));
                Ui.Info(msg, MessageBoxIcon.Warning);
                UpdateButtons();
                return;
            }
            wimFile = wim;
            top = root;
            tree.Nodes.Clear();
            TreeNode tn = NewTreeNode(root);
            tn.Text = root.Name;
            tree.Nodes.Add(tn);
            tn.Expand();
            ShowFolder(root, true);
            SetState(L.F("br.listed", count.ToString("N0"), Math.Max(0.1, took.TotalSeconds).ToString("0.0")));
            log(L.F("br.log_listed", wim, count));
            UpdateButtons();
        }

        // ---------- tree ----------

        TreeNode NewTreeNode(Node n)
        {
            TreeNode tn = new TreeNode(n.Name);
            tn.Tag = n;
            tn.ImageIndex = tn.SelectedImageIndex = IconFor("", true);
            if (n.Dirs.Count > 0) tn.Nodes.Add(new TreeNode("..."));   // filled when expanded
            return tn;
        }

        void FillTreeNode(TreeNode tn)
        {
            Node n = tn.Tag as Node;
            if (n == null || (tn.Nodes.Count > 0 && tn.Nodes[0].Tag != null)) return;
            tree.BeginUpdate();
            tn.Nodes.Clear();
            List<Node> dirs = new List<Node>(n.Dirs.Values);
            dirs.Sort(delegate(Node a, Node b) { return StringComparer.OrdinalIgnoreCase.Compare(a.Name, b.Name); });
            foreach (Node d in dirs) tn.Nodes.Add(NewTreeNode(d));
            tree.EndUpdate();
        }

        // the tree node of a folder (the path to it is expanded)
        TreeNode FindTreeNode(Node n)
        {
            List<Node> chain = new List<Node>();
            for (Node x = n; x != null; x = x.Parent) chain.Insert(0, x);
            if (tree.Nodes.Count == 0) return null;
            TreeNode tn = tree.Nodes[0];
            for (int i = 1; i < chain.Count; i++)
            {
                FillTreeNode(tn);
                tn.Expand();
                TreeNode next = null;
                foreach (TreeNode c in tn.Nodes)
                    if (c.Tag == chain[i]) { next = c; break; }
                if (next == null) return tn;
                tn = next;
            }
            return tn;
        }

        // ---------- list ----------

        void ShowFolder(Node n, bool syncTree)
        {
            current = n;
            List<Row> r = new List<Row>();
            List<Row> dirs = new List<Row>();
            foreach (Node d in n.Dirs.Values)
                dirs.Add(new Row { Name = d.Name, Path = d.Path, Modified = d.Modified, Attr = d.Attr, Dir = d });
            Comparison<Row> cmp = delegate(Row a, Row b)
            {
                int c;
                switch (sortColumn)
                {
                    case 1: c = a.Size.CompareTo(b.Size); break;
                    case 2: c = string.CompareOrdinal(a.Modified, b.Modified); break;
                    case 3: c = string.CompareOrdinal(a.Attr, b.Attr); break;
                    default: c = 0; break;
                }
                if (c == 0) c = StringComparer.OrdinalIgnoreCase.Compare(a.Name, b.Name);
                return sortDown ? -c : c;
            };
            dirs.Sort(cmp);
            List<Row> files = new List<Row>(n.Files);
            files.Sort(cmp);
            r.AddRange(dirs);
            r.AddRange(files);
            rows = r;
            list.SelectedIndices.Clear();
            list.VirtualListSize = rows.Count;
            list.Invalidate();
            if (rows.Count > 0) list.EnsureVisible(0);
            string rel = n.Path;
            ImageItem img = cboImage.SelectedItem as ImageItem;
            if (img != null && img.Prefix.Length > 0) rel = rel.Length > img.Prefix.Length ? rel.Substring(img.Prefix.Length + 1) : "";
            lblPath.Text = "\\" + rel + "   " + L.F("br.folder_count", dirs.Count, files.Count);
            if (syncTree)
            {
                TreeNode tn = FindTreeNode(n);
                if (tn != null && tree.SelectedNode != tn) tree.SelectedNode = tn;
            }
            UpdateButtons();
        }

        void OnRetrieveItem(object sender, RetrieveVirtualItemEventArgs e)
        {
            Row r = e.ItemIndex >= 0 && e.ItemIndex < rows.Count ? rows[e.ItemIndex] : null;
            if (r == null) { e.Item = new ListViewItem(""); return; }
            ListViewItem it = new ListViewItem(r.Name, IconFor(r.Name, r.Dir != null));
            it.SubItems.Add(r.Dir != null ? "" : SizeText(r.Size));
            it.SubItems.Add(r.Modified);
            it.SubItems.Add(r.Attr);
            e.Item = it;
        }

        static string SizeText(long b)
        {
            if (b < 0) return "";
            if (b < 1024) return b + " B";
            if (b < 1048576) return (b / 1024.0).ToString("0.#") + " KB";
            if (b < 1073741824) return (b / 1048576.0).ToString("0.#") + " MB";
            return (b / 1073741824.0).ToString("0.##") + " GB";
        }

        List<Row> SelectedRows()
        {
            List<Row> sel = new List<Row>();
            foreach (int i in list.SelectedIndices)
                if (i >= 0 && i < rows.Count) sel.Add(rows[i]);
            return sel;
        }

        // ---------- icons (of the shell, by extension) ----------

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct SHFILEINFO
        {
            public IntPtr hIcon;
            public int iIcon;
            public uint dwAttributes;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string szDisplayName;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 80)] public string szTypeName;
        }

        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        static extern IntPtr SHGetFileInfo(string path, uint attributes, ref SHFILEINFO info, uint size, uint flags);
        [DllImport("user32.dll")]
        static extern bool DestroyIcon(IntPtr icon);

        int IconFor(string name, bool dir)
        {
            string ext = dir ? "<dir>" : Path.GetExtension(name);
            // programs and icons have their own icon in the file: one generic icon for them
            if (!dir && (ext.Length == 0 || ext.Equals(".exe", StringComparison.OrdinalIgnoreCase) || ext.Equals(".ico", StringComparison.OrdinalIgnoreCase) || ext.Equals(".lnk", StringComparison.OrdinalIgnoreCase)))
                ext = ext.Length == 0 ? "<file>" : ext;
            int idx;
            if (iconIndex.TryGetValue(ext, out idx)) return idx;
            idx = -1;
            try
            {
                const uint SmallIcon = 0x1, UseAttributes = 0x10, GetIcon = 0x100;
                const uint AttrDir = 0x10, AttrNormal = 0x80;
                SHFILEINFO info = new SHFILEINFO();
                string probe = dir ? "folder" : "file" + (ext.StartsWith("<") ? "" : ext);
                SHGetFileInfo(probe, dir ? AttrDir : AttrNormal, ref info, (uint)Marshal.SizeOf(info), GetIcon | SmallIcon | UseAttributes);
                if (info.hIcon != IntPtr.Zero)
                {
                    using (Icon ic = System.Drawing.Icon.FromHandle(info.hIcon))
                        icons.Images.Add(ic.ToBitmap());
                    DestroyIcon(info.hIcon);
                    idx = icons.Images.Count - 1;
                }
            }
            catch (Exception) { }
            iconIndex[ext] = idx;
            return idx;
        }

        // ---------- 7z.exe ----------

        ProcessStartInfo SevenZipStart(string args)
        {
            ProcessStartInfo psi = new ProcessStartInfo(Libs.SevenZip(rootDir), args);
            psi.UseShellExecute = false;
            psi.CreateNoWindow = true;
            psi.RedirectStandardOutput = true;
            psi.RedirectStandardError = true;
            psi.WorkingDirectory = Libs.Dir(rootDir);
            return psi;
        }

        void Post(MethodInvoker a)
        {
            try { if (IsHandleCreated && !IsDisposed) BeginInvoke(a); } catch (Exception) { }
        }

        // 7z x / e with a list file; progress (-bsp1) to the bar; done(code, error text) on the GUI thread
        void RunExtract(string args, List<string> paths, string state, Action<int, string> done)
        {
            string listFile = Path.Combine(Path.GetTempPath(), "7z_wimlib_7z_" + DateTime.Now.Ticks + ".txt");
            try { File.WriteAllLines(listFile, paths.ToArray(), new UTF8Encoding(false)); }
            catch (Exception ex) { Ui.Info(ex.Message, MessageBoxIcon.Error); return; }
            ProcessStartInfo psi = SevenZipStart(args + " -y -bso0 -bsp1 -bse1 -sccUTF-8 -scsUTF-8 \"@" + listFile + "\"");
            psi.StandardOutputEncoding = Encoding.UTF8;
            cancelRequested = false;
            try { proc = Process.Start(psi); }
            catch (Exception ex) { try { File.Delete(listFile); } catch (Exception) { } Ui.Info(ex.Message, MessageBoxIcon.Error); return; }
            Process p = proc;
            bar.Style = ProgressBarStyle.Blocks;
            bar.Value = 0;
            SetState(state);
            UpdateButtons();
            Thread th = new Thread(delegate()
            {
                StringBuilder all = new StringBuilder();
                Thread errReader = new Thread(delegate() { try { p.StandardError.ReadToEnd(); } catch (Exception) { } });
                errReader.Start();
                try
                {
                    char[] buf = new char[4096];
                    int n, lastPct = -1;
                    while ((n = p.StandardOutput.Read(buf, 0, buf.Length)) > 0)
                    {
                        string chunk = new string(buf, 0, n);
                        all.Append(chunk);
                        MatchCollection ms = Regex.Matches(chunk, @"(\d{1,3})%");
                        if (ms.Count > 0)
                        {
                            int pct = int.Parse(ms[ms.Count - 1].Groups[1].Value);
                            if (pct != lastPct && pct <= 100)
                            {
                                lastPct = pct;
                                Post(delegate { bar.Value = pct; SetState(state + "  " + pct + "%"); });
                            }
                        }
                    }
                }
                catch (Exception) { }
                p.WaitForExit();
                errReader.Join();
                int code = p.ExitCode;
                try { File.Delete(listFile); } catch (Exception) { }
                // the error lines of 7-Zip (stderr is sent to stdout with -bse1)
                StringBuilder errors = new StringBuilder();
                foreach (string l in Regex.Replace(all.ToString(), @"[\b\r]", "\n").Split('\n'))
                {
                    string t = l.Trim();
                    if (t.Length > 0 && !Regex.IsMatch(t, @"^\d{1,3}%")) errors.AppendLine(t);
                }
                Post(delegate
                {
                    if (proc == p) proc = null;
                    try { p.Dispose(); } catch (Exception) { }
                    bar.Value = 0;
                    UpdateButtons();
                    done(code, errors.ToString().Trim());
                });
            });
            th.IsBackground = true;
            th.Start();
        }

        // ---------- extract ----------

        void ExtractSelected()
        {
            if (proc != null || top == null || !File.Exists(wimFile)) return;
            List<Row> sel = SelectedRows();
            if (sel.Count == 0) return;
            string dest;
            using (FolderBrowserDialog dlg = new FolderBrowserDialog())
            {
                dlg.Description = L.F("br.select_dest", sel.Count);
                dlg.ShowNewFolderButton = true;
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                dest = dlg.SelectedPath;
            }
            List<string> exist = new List<string>();
            foreach (Row r in sel)
                if (File.Exists(Path.Combine(dest, r.Name)) || Directory.Exists(Path.Combine(dest, r.Name))) exist.Add(r.Name);
            if (exist.Count > 0 && !Ui.YesNo(L.F("br.confirm_overwrite", dest, string.Join("\r\n", exist.GetRange(0, Math.Min(10, exist.Count)).ToArray())
                    + (exist.Count > 10 ? "\r\n..." : "")), MessageBoxIcon.Warning))
                return;

            // 7z keeps the path inside the image: into a work folder in the target, then moved up
            string stage = Path.Combine(dest, ".7z_wimlib_" + DateTime.Now.ToString("yyyyMMddHHmmss"));
            Node folder = current;
            List<string> paths = new List<string>();
            foreach (Row r in sel) paths.Add(r.Path);
            log(L.F("br.log_extract", sel.Count, folder.Path.Length > 0 ? folder.Path : "\\", dest));
            DateTime started = DateTime.Now;
            RunExtract("x \"" + wimFile + "\" -o\"" + stage + "\"", paths, L.T("br.extracting"), delegate(int code, string errors)
            {
                string moveErr = null;
                bool killed = cancelRequested;
                // cancelled: nothing half done is moved into the target
                if (!killed) try
                {
                    string from = folder.Path.Length > 0 ? Path.Combine(stage, folder.Path) : stage;
                    foreach (Row r in sel)
                    {
                        string src = Path.Combine(from, r.Name);
                        if (Directory.Exists(src) || File.Exists(src)) MoveMerge(src, Path.Combine(dest, r.Name));
                    }
                }
                catch (Exception ex) { moveErr = ex.Message; }
                try { if (Directory.Exists(stage)) Directory.Delete(stage, true); } catch (Exception) { }
                string took = (DateTime.Now - started).TotalSeconds.ToString("0.0");
                if (code == 0 && moveErr == null)
                {
                    SetState(L.F("br.extracted", sel.Count, took));
                    log(L.F("br.extracted", sel.Count, took) + "  -> " + dest);
                    if (Ui.YesNo(L.F("br.done_open", dest), MessageBoxIcon.Information)) Ui.OpenFolder(dest);
                    return;
                }
                SetState(L.T(killed ? "bk.state_cancelled" : "bk.state_failed"));
                string msg = moveErr ?? (killed ? L.T("br.cancelled") : L.F("br.extract_failed", code, errors.Length > 0 ? errors : "-"));
                log(msg.Replace("\r\n", " "));
                Ui.Info(msg, code == 1 ? MessageBoxIcon.Warning : MessageBoxIcon.Error);
            });
        }

        // a file or a folder to dest; an existing folder is merged, existing files replaced
        static void MoveMerge(string src, string dest)
        {
            if (File.Exists(src))
            {
                if (File.Exists(dest)) File.Delete(dest);
                File.Move(src, dest);
                return;
            }
            if (!Directory.Exists(dest)) { Directory.Move(src, dest); return; }
            foreach (string f in Directory.GetFiles(src)) MoveMerge(f, Path.Combine(dest, Path.GetFileName(f)));
            foreach (string d in Directory.GetDirectories(src)) MoveMerge(d, Path.Combine(dest, Path.GetFileName(d)));
        }

        // ---------- open ----------

        static string ViewRoot { get { return Path.Combine(Path.GetTempPath(), "7z_wimlib_view"); } }

        // files opened before (best effort: an open file stays)
        static void CleanViewFolder()
        {
            try { if (Directory.Exists(ViewRoot)) Directory.Delete(ViewRoot, true); } catch (Exception) { }
        }

        static readonly string[] Programs = { ".exe", ".com", ".bat", ".cmd", ".ps1", ".vbs", ".vbe", ".js", ".jse", ".wsf", ".wsh", ".msi", ".msc", ".scr", ".cpl", ".hta", ".lnk", ".reg", ".inf", ".sys", ".dll" };

        void OpenSelected()
        {
            if (proc != null) return;
            List<Row> sel = SelectedRows();
            if (sel.Count != 1) return;
            Row r = sel[0];
            if (r.Dir != null) { ShowFolder(r.Dir, true); return; }
            string dir = Path.Combine(ViewRoot, DateTime.Now.ToString("HHmmss-fff"));
            string file = Path.Combine(dir, r.Name);
            RunExtract("e \"" + wimFile + "\" -o\"" + dir + "\"", new List<string> { r.Path }, L.F("br.opening", r.Name), delegate(int code, string errors)
            {
                if (cancelRequested) { SetState(L.T("bk.state_cancelled")); return; }
                if (code != 0 || !File.Exists(file))
                {
                    SetState(L.T("bk.state_failed"));
                    Ui.Info(L.F("br.extract_failed", code, errors.Length > 0 ? errors : "-"), MessageBoxIcon.Error);
                    return;
                }
                SetState(r.Name);
                // a program is not started from here: its folder is shown
                if (Array.IndexOf(Programs, Path.GetExtension(file).ToLowerInvariant()) >= 0)
                {
                    Process.Start("explorer.exe", "/select,\"" + file + "\"");
                    return;
                }
                try { Process.Start(new ProcessStartInfo(file) { UseShellExecute = true }); }
                catch (Exception)
                {
                    // no program for this type: Windows asks for one
                    try { Process.Start("rundll32.exe", "shell32.dll,OpenAs_RunDLL " + file); } catch (Exception ex) { Ui.Info(ex.Message, MessageBoxIcon.Warning); }
                }
            });
        }

        void OpenIn7zFm()
        {
            string wim = WimPath;
            if (wim.Length == 0 || !File.Exists(wim)) { Ui.Info(L.F("msg.not_found", wim.Length == 0 ? "-" : wim), MessageBoxIcon.Warning); return; }
            if (!Ensure7zip()) return;
            string fm = Libs.SevenZipManager(rootDir);
            if (!File.Exists(fm)) { Ui.Info(L.F("msg.not_found", fm), MessageBoxIcon.Warning); return; }
            try { Process.Start(fm, "\"" + wim + "\""); }
            catch (Exception ex) { Ui.Info(ex.Message, MessageBoxIcon.Warning); }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) Cancel();
            base.Dispose(disposing);
        }
    }
}
