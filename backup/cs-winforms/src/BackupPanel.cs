// ============================================================
//  7z_wimlib - BackupPanel.cs (taken from the DVT GUI)
//  The backup / restore window, with wimlib
//  (<exe folder>\lib\libwim-15.dll, downloaded when missing, see
//  Libs.cs and WimLib.cs):
//   - Backup: saves a volume or a folder as a WIM image (new file or a
//     new image of an existing file). The running Windows drive cannot
//     be saved. The default file is <folder of the last backup>\backup.wim.
//   - Restore: formats a volume (NTFS) and applies one image of a WIM
//     file to it, after two confirmations. Not offered: the Windows
//     drive, the drive of 7z_wimlib.exe and the drive of the WIM.
//   - Export: copies one image (or all) of a WIM / ESD file into a new
//     WIM file or into an existing one (other compression, ESD -> WIM).
//     Runs in the helper too, but without administrator rights.
//   - Split / join: a WIM file as split parts (.swm) of a chosen size
//     (e.g. for FAT32), and the parts joined into one WIM file again.
//     Run in the helper without administrator rights, like the export.
//   - Info: the header of a WIM file and the details of its images
//     (read here, nothing is changed); an image can be taken over to
//     the restore, the export or the browse tab.
//   - Browse: the files of an image, with 7-Zip (WimBrowser.cs).
//  Backup and restore run in the elevated helper (7z_wimlib.exe --wim-helper) with
//  the work folder %TEMP%\DVT_wim_job\<time> (job.ini, status.txt,
//  wimlib_errors.txt); this page shows its progress, the log goes to
//  its own window (LogWindow.cs, hidden until [Show log]). The work
//  folder is deleted after a successful job and kept after a failed or
//  cancelled one (its path is shown in the error message).
// ============================================================
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Windows.Forms;

namespace Dvt
{
    public partial class BackupPanel : UserControl
    {
        readonly string rootDir;
        readonly Func<Dictionary<string, string>> getPaths;
        readonly Func<string> getImageFolder;
        readonly Action<string> setImageFolder;

        // backup
        readonly ComboBox cboSource, cboComp;
        readonly NumericUpDown numThreads;    // 0 = automatic (one per logical processor)
        readonly TextBox txtWim, txtName, txtDesc;
        readonly RadioButton rbReplace, rbAppend;
        readonly Button btnCapture;
        // restore
        readonly TextBox txtRWim, txtLabel;
        readonly ComboBox cboImage, cboTarget, cboSysPart;
        readonly CheckBox chkUefi, chkBios, chkNoBoot;
        readonly CheckBox chkVerify;          // backup: verify the WIM file after writing it
        readonly CheckBox chkSnapshot;        // backup: read the source from a VSS snapshot
        readonly Button btnIVerify;
        bool jobVerify, jobIntegrity;
        readonly ComboBox cboBootLang;        // bcdboot /l: language of the boot manager
        readonly Button btnApply, btnPartRefresh;
        List<DiskPartition> partitions;       // null = not read (yet)
        bool partsLoading, bootSync;
        string bootTargetKey = "", bootFw = "";   // the target / firmware the system partition was chosen for
        string jobBoot = "";
        // export
        readonly TextBox txtXSrc, txtXWim, txtXName, txtXDesc;
        readonly ComboBox cboXImage, cboXComp;
        readonly NumericUpDown numXThreads;
        readonly RadioButton rbXReplace, rbXAppend;
        readonly Button btnExport;
        // split / join
        readonly TextBox txtSSrc, txtSSwm, txtJSwm, txtJWim;
        readonly NumericUpDown numSPart;      // MiB per part
        readonly Label lblJParts;
        readonly Button btnSplit, btnJoin;
        // info
        readonly TextBox txtIWim;
        readonly Label lblISummary;
        readonly ListView lvInfo;
        readonly Button btnIRestore, btnIExport, btnIBrowse;
        // browse (7-Zip)
        readonly WimBrowser browser;
        TabPage tabBrowse;
        WimFileInfo info;                     // the file shown on the info tab (null = none)
        string infoWim = "";
        readonly DarkTabControl tabs;
        TabPage tabApply, tabExport;
        // job
        readonly Button btnCancel;
        readonly ComboBox cboAfter;           // 0 = nothing, 1 = shut down, 2 = restart
        readonly FlatProgress progress;
        readonly Label lblProgress;          // state of the job / status text (footer, left)
        readonly Label lblLast;              // the last line of the log (footer)
        readonly TableLayoutPanel foot;
        readonly Label[] pv = new Label[5];  // backup preview: source, target, method, rights, verify
        readonly LogWindow logWin;           // hidden until [Show log]
        readonly Timer timer;

        Process helper;
        string workDir, helperStatusFile, jobMode, jobWim, jobTarget;
        int helperLinesShown, jobImage;
        DateTime helperStarted;
        string phaseText = "";

        class PartItem
        {
            public DiskPartition Part;
            public string Text = "";
            public override string ToString() { return Text; }
        }

        class Item
        {
            public string Path = "", Text = "";
            public char Letter;
            public long Size = -1;
            public WimImage Image;
            public override string ToString() { return Text; }
        }

        // imageFolder: the image folder of the seeder page (DL_NAME)
        public BackupPanel(string root, Func<Dictionary<string, string>> paths, Func<string> imageFolder, Action<string> imageFolderSaved)
        {
            rootDir = root;
            getPaths = paths;
            getImageFolder = imageFolder;
            setImageFolder = imageFolderSaved;
            Dock = DockStyle.Fill;

            // the log has its own window: the tabs get the height of the window
            logWin = new LogWindow();
            // tabs, a thin progress line, the footer (state, last log line, ...)
            TableLayoutPanel lay = Ui.NewRows(0, 4, 38);
            tabs = new DarkTabControl();

            // ---- tab: backup ----
            TableLayoutPanel b = Grid(7);
            b.ColumnStyles[0] = new ColumnStyle(SizeType.Absolute, Ui.S(110));
            cboSource = Combo(290);
            Button btnFolder = Ui.NewButton(L.T("bk.btn_folder"), 110);
            Button btnSrcRefresh = Ui.NewButton(L.T("adk.btn_refresh"), 90);
            AddRow(b, 0, L.T("bk.row_source"), cboSource, btnFolder, btnSrcRefresh);
            txtWim = NewText(400);
            Button btnWim = Ui.NewButton(L.T("qbt.btn_browse"), 100);
            AddRow(b, 1, L.T("bk.row_wim"), txtWim, btnWim);
            txtName = NewText(200);
            txtDesc = NewText(260);
            Label lblDesc = Ui.NewFlowLabel(L.T("bk.row_desc"));
            lblDesc.Margin = new Padding(Ui.S(8), Ui.S(8), Ui.S(4), 0);
            AddRow(b, 2, L.T("bk.row_name"), txtName, lblDesc, txtDesc);
            cboComp = Combo(280);
            cboComp.Items.AddRange(new object[] { L.T("bk.comp_lzx"), L.T("bk.comp_xpress"), L.T("bk.comp_lzms") });
            cboComp.SelectedIndex = 0;
            Label lblThreads = Ui.NewFlowLabel(L.T("bk.row_threads"));
            lblThreads.Margin = new Padding(Ui.S(8), Ui.S(8), Ui.S(4), 0);
            numThreads = Threads();
            AddRow(b, 3, L.T("bk.row_comp"), cboComp, lblThreads, numThreads);
            rbReplace = Radio(L.T("bk.existing_replace"), true);
            rbAppend = Radio(L.T("bk.existing_append"), false);
            AddRow(b, 4, L.T("bk.row_existing"), rbReplace, rbAppend);
            chkSnapshot = Check(L.T("bk.snapshot"));
            chkSnapshot.Checked = Program.GetSetting("backup.snapshot", "0") == "1";
            AddRow(b, 5, L.T("bk.row_snapshot"), chkSnapshot);
            chkVerify = Check(L.T("vf.after_backup"));
            AddRow(b, 6, L.T("vf.row"), chkVerify);
            // right: what the backup will do, the hint and [Start backup]
            btnCapture = Ui.NewButton(L.T("bk.btn_capture"), 160);
            Ui.Primary(btnCapture, Palette.Backup);
            btnCapture.Dock = DockStyle.Fill;
            btnCapture.Margin = new Padding(0, Ui.S(8), 0, 0);
            Label capHint = Ui.NewFlowLabel(L.F("bk.capture_hint", SystemDrive()));
            capHint.ForeColor = Color.DimGray;
            capHint.MaximumSize = new Size(Ui.S(262), 0);
            capHint.Margin = new Padding(0, Ui.S(4), 0, 0);
            TableLayoutPanel bt = new TableLayoutPanel();
            bt.ColumnCount = 2;
            bt.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            bt.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, Ui.S(300)));
            bt.RowCount = 1;
            bt.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            b.Dock = DockStyle.Fill;
            bt.Controls.Add(b, 0, 0);
            bt.Controls.Add(Preview(capHint, btnCapture), 1, 0);
            TabPage tabCapture = Ui.NewTabPage(L.T("bk.tab_capture"), bt, true);
            tabs.TabPages.Add(tabCapture);

            // ---- tab: disk (clear, GPT / MBR, partitions): BackupPanel.Disk.cs ----
            tabDisk = Ui.NewTabPage(L.T("dk.tab"), BuildDiskTab(), true);
            tabs.TabPages.Add(tabDisk);

            // ---- tab: restore ----
            TableLayoutPanel r = Grid(8);
            txtRWim = NewText(420);
            Button btnRWim = Ui.NewButton(L.T("qbt.btn_browse"), 100);
            Button btnRead = Ui.NewButton(L.T("bk.btn_read"), 120);
            AddRow(r, 0, L.T("bk.row_wim"), txtRWim, btnRWim, btnRead);
            cboImage = Combo(520);
            AddRow(r, 1, L.T("bk.row_image"), cboImage);
            cboTarget = Combo(420);
            Button btnTgtFolder = Ui.NewButton(L.T("bk.btn_folder"), 110);
            Button btnTgtRefresh = Ui.NewButton(L.T("adk.btn_refresh"), 90);
            AddRow(r, 2, L.T("bk.row_target"), cboTarget, btnTgtFolder, btnTgtRefresh);
            txtLabel = NewText(200);
            txtLabel.Text = "Windows";
            AddRow(r, 3, L.T("bk.row_label"), txtLabel);
            chkUefi = Check(L.T("bk.boot_uefi"));
            chkBios = Check(L.T("bk.boot_bios"));
            chkNoBoot = Check(L.T("bk.boot_none"));
            chkNoBoot.Checked = true;             // until the target disk is known
            Label lblBootLang = Ui.NewFlowLabel(L.T("bk.boot_lang"));
            lblBootLang.Margin = new Padding(Ui.S(16), Ui.S(8), Ui.S(4), 0);
            // other locales can be typed; EditCombo: readable while disabled
            cboBootLang = new EditCombo();
            cboBootLang.Width = Ui.S(90);
            cboBootLang.Margin = new Padding(0, Ui.S(5), Ui.S(6), 0);
            cboBootLang.Items.AddRange(new object[] { "ko-kr", "en-us", "ja-jp", "zh-cn", "zh-tw", "de-de", "fr-fr", "es-es", "ru-ru" });
            // the last language used (7z_wimlib.ini: restore.boot_lang)
            string savedLang = Program.GetSetting("restore.boot_lang", "ko-kr").ToLowerInvariant();
            cboBootLang.Text = WimLib.IsLocale(savedLang) ? savedLang : "ko-kr";
            AddRow(r, 4, L.T("bk.row_boot"), chkUefi, chkBios, chkNoBoot, lblBootLang, cboBootLang);
            cboSysPart = Combo(520);
            btnPartRefresh = Ui.NewButton(L.T("adk.btn_refresh"), 90);
            AddRow(r, 5, L.T("bk.row_syspart"), cboSysPart, btnPartRefresh);
            btnApply = Ui.NewButton(L.T("bk.btn_apply"), 160);
            Ui.Primary(btnApply, Ui.ColorDanger);
            AddRow(r, 6, "", btnApply);
            Label applyHint = Ui.NewFlowLabel(L.T("bk.apply_hint"));
            applyHint.ForeColor = Color.DimGray;
            applyHint.MaximumSize = new Size(Ui.S(760), 0);
            r.Controls.Add(applyHint, 0, 7);
            r.SetColumnSpan(applyHint, 2);
            tabApply = Ui.NewTabPage(L.T("bk.tab_apply"), r, true);
            tabs.TabPages.Add(tabApply);

            // ---- tab: export ----
            TableLayoutPanel x = Grid(7);
            txtXSrc = NewText(420);
            Button btnXSrc = Ui.NewButton(L.T("qbt.btn_browse"), 100);
            Button btnXRead = Ui.NewButton(L.T("bk.btn_read"), 120);
            AddRow(x, 0, L.T("ex.row_source"), txtXSrc, btnXSrc, btnXRead);
            cboXImage = Combo(520);
            AddRow(x, 1, L.T("bk.row_image"), cboXImage);
            txtXWim = NewText(420);
            Button btnXWim = Ui.NewButton(L.T("qbt.btn_browse"), 100);
            AddRow(x, 2, L.T("ex.row_target"), txtXWim, btnXWim);
            txtXName = NewText(200);
            txtXDesc = NewText(260);
            Label lblXDesc = Ui.NewFlowLabel(L.T("bk.row_desc"));
            lblXDesc.Margin = new Padding(Ui.S(8), Ui.S(8), Ui.S(4), 0);
            AddRow(x, 3, L.T("bk.row_name"), txtXName, lblXDesc, txtXDesc);
            cboXComp = Combo(330);
            cboXComp.Items.AddRange(new object[] { L.T("bk.comp_lzx"), L.T("bk.comp_xpress"), L.T("bk.comp_lzms") });
            cboXComp.SelectedIndex = 0;
            Label lblXThreads = Ui.NewFlowLabel(L.T("bk.row_threads"));
            lblXThreads.Margin = new Padding(Ui.S(8), Ui.S(8), Ui.S(4), 0);
            numXThreads = Threads();
            AddRow(x, 4, L.T("bk.row_comp"), cboXComp, lblXThreads, numXThreads, ThreadsHint());
            rbXReplace = Radio(L.T("bk.existing_replace"), true);
            rbXAppend = Radio(L.T("ex.existing_append"), false);
            AddRow(x, 5, L.T("bk.row_existing"), rbXReplace, rbXAppend);
            btnExport = Ui.NewButton(L.T("ex.btn_export"), 160);
            Ui.Primary(btnExport, Palette.Backup);
            Label exHint = Ui.NewFlowLabel(L.T("ex.hint"));
            exHint.ForeColor = Color.DimGray;
            exHint.MaximumSize = new Size(Ui.S(560), 0);
            exHint.Margin = new Padding(Ui.S(8), Ui.S(4), 0, 0);
            AddRow(x, 6, "", btnExport, exHint);
            tabExport = Ui.NewTabPage(L.T("ex.tab"), x, true);
            tabs.TabPages.Add(tabExport);

            // ---- tab: split / join ----
            TableLayoutPanel sj = Ui.NewRows(0, 0);
            sj.RowStyles[0] = new RowStyle(SizeType.Percent, 50f);
            sj.RowStyles[1] = new RowStyle(SizeType.Percent, 50f);
            TableLayoutPanel sg = Grid(3);
            txtSSrc = NewText(420);
            Button btnSSrc = Ui.NewButton(L.T("qbt.btn_browse"), 100);
            AddRow(sg, 0, L.T("ex.row_source"), txtSSrc, btnSSrc);
            txtSSwm = NewText(420);
            Button btnSSwm = Ui.NewButton(L.T("qbt.btn_browse"), 100);
            AddRow(sg, 1, L.T("sj.row_swm"), txtSSwm, btnSSwm);
            numSPart = new NumericUpDown();
            numSPart.Minimum = 10;
            numSPart.Maximum = 1048576;
            numSPart.Increment = 100;
            numSPart.Value = 4000;
            numSPart.ThousandsSeparator = true;
            numSPart.Width = Ui.S(90);
            numSPart.Margin = new Padding(0, Ui.S(5), Ui.S(4), 0);
            Label lblMb = Ui.NewFlowLabel(L.T("sj.part_hint"));
            lblMb.ForeColor = Color.DimGray;
            lblMb.Margin = new Padding(0, Ui.S(8), Ui.S(12), 0);
            btnSplit = Ui.NewButton(L.T("sj.btn_split"), 140);
            Ui.Primary(btnSplit, Palette.Backup);
            AddRow(sg, 2, L.T("sj.row_part"), numSPart, lblMb, btnSplit);
            sj.Controls.Add(Group(L.T("sj.group_split"), sg), 0, 0);

            TableLayoutPanel jg = Grid(3);
            txtJSwm = NewText(420);
            Button btnJSwm = Ui.NewButton(L.T("qbt.btn_browse"), 100);
            lblJParts = Ui.NewFlowLabel("");
            lblJParts.ForeColor = Color.DimGray;
            AddRow(jg, 0, L.T("sj.row_first"), txtJSwm, btnJSwm, lblJParts);
            txtJWim = NewText(420);
            Button btnJWim = Ui.NewButton(L.T("qbt.btn_browse"), 100);
            AddRow(jg, 1, L.T("sj.row_join_wim"), txtJWim, btnJWim);
            btnJoin = Ui.NewButton(L.T("sj.btn_join"), 140);
            Ui.Primary(btnJoin, Palette.Backup);
            Label joinHint = Ui.NewFlowLabel(L.T("sj.join_hint"));
            joinHint.ForeColor = Color.DimGray;
            joinHint.MaximumSize = new Size(Ui.S(560), 0);
            joinHint.Margin = new Padding(Ui.S(8), Ui.S(4), 0, 0);
            AddRow(jg, 2, "", btnJoin, joinHint);
            sj.Controls.Add(Group(L.T("sj.group_join"), jg), 0, 1);
            tabs.TabPages.Add(Ui.NewTabPage(L.T("sj.tab"), sj, true));

            // ---- tab: info ----
            TableLayoutPanel n = new TableLayoutPanel();
            n.Dock = DockStyle.Fill;
            n.ColumnCount = 2;
            n.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, Ui.S(150)));
            n.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            n.RowCount = 4;
            n.RowStyles.Add(new RowStyle(SizeType.Absolute, Ui.S(34)));
            n.RowStyles.Add(new RowStyle(SizeType.Absolute, Ui.S(40)));
            n.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            n.RowStyles.Add(new RowStyle(SizeType.Absolute, Ui.S(36)));
            txtIWim = NewText(420);
            Button btnIWim = Ui.NewButton(L.T("qbt.btn_browse"), 100);
            Button btnIRead = Ui.NewButton(L.T("in.btn_read"), 120);
            AddRow(n, 0, L.T("bk.row_wim"), txtIWim, btnIWim, btnIRead);
            lblISummary = Ui.NewLabel(L.T("in.none"));
            lblISummary.TextAlign = ContentAlignment.MiddleLeft;
            lblISummary.AutoEllipsis = true;
            n.Controls.Add(lblISummary, 0, 1);
            n.SetColumnSpan(lblISummary, 2);
            lvInfo = new ListView();
            lvInfo.Dock = DockStyle.Fill;
            lvInfo.View = View.Details;
            lvInfo.FullRowSelect = true;
            lvInfo.HideSelection = false;
            lvInfo.MultiSelect = false;
            lvInfo.GridLines = true;
            lvInfo.Font = Ui.FontUI;
            foreach (string[] c in new string[][] {
                new[] { "in.col_index", "36" }, new[] { "in.col_name", "190" }, new[] { "in.col_edition", "130" },
                new[] { "in.col_arch", "76" }, new[] { "in.col_version", "110" }, new[] { "in.col_lang", "90" },
                new[] { "in.col_size", "76" }, new[] { "in.col_files", "120" }, new[] { "in.col_created", "130" },
                new[] { "in.col_modified", "130" }, new[] { "in.col_desc", "220" } })
                lvInfo.Columns.Add(L.T(c[0]), Ui.S(int.Parse(c[1])));
            n.Controls.Add(lvInfo, 0, 2);
            n.SetColumnSpan(lvInfo, 2);
            FlowLayoutPanel ib = Ui.NewFlow();
            ib.Margin = new Padding(0, Ui.S(4), 0, 0);
            btnIRestore = Ui.NewButton(L.T("in.btn_restore"), 150);
            btnIExport = Ui.NewButton(L.T("in.btn_export"), 150);
            btnIBrowse = Ui.NewButton(L.T("in.btn_browse"), 150);
            Button btnICopy = Ui.NewButton(L.T("in.btn_copy"), 120);
            btnIVerify = Ui.NewButton(L.T("vf.btn"), 120);
            btnIRestore.Enabled = btnIExport.Enabled = btnIBrowse.Enabled = btnIVerify.Enabled = false;
            ib.Controls.AddRange(new Control[] { btnIRestore, btnIExport, btnIBrowse, btnICopy, btnIVerify });
            n.Controls.Add(ib, 0, 3);
            n.SetColumnSpan(ib, 2);
            tabs.TabPages.Add(Ui.NewTabPage(L.T("in.tab"), n, true));

            // ---- tab: browse (7-Zip) ----
            browser = new WimBrowser(root, Log, delegate(string wim) { return Images(wim, false); });
            tabBrowse = Ui.NewTabPage(L.T("br.tab"), browser, true);
            tabs.TabPages.Add(tabBrowse);
            // the order shown: restore, disk, backup, export, split / join, info, browse
            // (built in another order: the restore tab uses controls of the backup tab)
            List<TabPage> order = new List<TabPage>();
            foreach (TabPage tp in tabs.TabPages) order.Add(tp);
            order.Remove(tabApply);
            order.Remove(tabCapture);
            order.Insert(0, tabApply);
            order.Insert(order.IndexOf(tabDisk) + 1, tabCapture);
            tabs.TabPages.Clear();
            tabs.TabPages.AddRange(order.ToArray());
            tabs.SelectedIndex = 0;
            Panel tabHost = new Panel();
            tabHost.Dock = DockStyle.Fill;
            tabHost.Margin = new Padding(0);
            tabHost.Padding = new Padding(Ui.S(8), Ui.S(2), Ui.S(8), 0);
            tabHost.Controls.Add(tabs);
            lay.Controls.Add(tabHost, 0, 0);

            // ---- progress line ----
            progress = new FlatProgress();
            progress.Dock = DockStyle.Fill;
            progress.Margin = new Padding(0);
            lay.Controls.Add(progress, 0, 1);

            // ---- footer: state | last log line | [Show log] | after done | [Cancel] | (language) ----
            foot = new TableLayoutPanel();
            foot.Dock = DockStyle.Fill;
            foot.Margin = new Padding(0);
            foot.Padding = new Padding(Ui.S(12), 0, Ui.S(8), 0);
            foot.BackColor = Theme.Footer;
            foot.ColumnCount = 7;
            foot.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            foot.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            for (int k = 0; k < 5; k++) foot.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
            foot.RowCount = 1;
            foot.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            lblProgress = new Label();
            lblProgress.AutoSize = true;
            lblProgress.Anchor = AnchorStyles.Left;
            lblProgress.Margin = new Padding(0, 0, Ui.S(12), 0);
            lblLast = new Label();
            lblLast.Anchor = AnchorStyles.Left | AnchorStyles.Right;   // one line, "..." at the end
            lblLast.Height = Ui.S(18);
            lblLast.AutoEllipsis = true;
            lblLast.TextAlign = ContentAlignment.MiddleLeft;
            lblLast.Font = Ui.FontMono;
            lblLast.ForeColor = Color.DimGray;
            lblLast.Cursor = Cursors.Hand;
            lblLast.Click += delegate { logWin.ShowNear(FindForm()); };
            LinkLabel lnkLog = new LinkLabel();
            lnkLog.Text = L.T("lg.btn_show");
            lnkLog.AutoSize = true;
            lnkLog.Anchor = AnchorStyles.Left;
            lnkLog.Margin = new Padding(Ui.S(10), 0, Ui.S(14), 0);
            lnkLog.LinkBehavior = LinkBehavior.HoverUnderline;
            lnkLog.LinkClicked += delegate { logWin.ShowNear(FindForm()); };
            // after a successful job: nothing / shut down / restart (can be changed while it runs)
            Label lblAfter = new Label();
            lblAfter.Text = L.T("bk.after_done");
            lblAfter.AutoSize = true;
            lblAfter.Anchor = AnchorStyles.Left;
            lblAfter.ForeColor = Color.DimGray;
            cboAfter = Combo(130);
            cboAfter.Anchor = AnchorStyles.Left;
            cboAfter.Margin = new Padding(Ui.S(4), 0, Ui.S(8), 0);
            cboAfter.Items.AddRange(new object[] { L.T("bk.after_nothing"), L.T("bk.after_shutdown"), L.T("bk.after_restart") });
            cboAfter.SelectedIndex = 0;
            btnCancel = Ui.NewButton(L.T("bk.btn_cancel"), 80);
            btnCancel.Height = Ui.S(26);
            btnCancel.Anchor = AnchorStyles.Left;
            btnCancel.Margin = new Padding(0, 0, Ui.S(8), 0);
            btnCancel.Enabled = false;
            foot.Controls.Add(lblProgress, 0, 0);
            foot.Controls.Add(lblLast, 1, 0);
            foot.Controls.Add(lnkLog, 2, 0);
            foot.Controls.Add(lblAfter, 3, 0);
            foot.Controls.Add(cboAfter, 4, 0);
            foot.Controls.Add(btnCancel, 5, 0);
            lay.Controls.Add(foot, 0, 2);
            // the note on administrator rights, lib\ and the log files: tooltip and first log line
            ToolTip tip = new ToolTip();
            tip.SetToolTip(lblLast, L.T("bk.note"));
            Controls.Add(lay);

            timer = new Timer();
            timer.Interval = 500;
            timer.Tick += delegate { PollHelper(); };

            foreach (Control c in new Control[] { cboSource, cboComp })
                ((ComboBox)c).SelectedIndexChanged += delegate { UpdatePreview(); };
            txtWim.TextChanged += delegate { UpdatePreview(); };
            numThreads.ValueChanged += delegate { UpdatePreview(); };
            rbAppend.CheckedChanged += delegate { UpdatePreview(); };
            chkVerify.CheckedChanged += delegate { UpdatePreview(); };
            // the running Windows drive is a source only with a snapshot
            chkSnapshot.CheckedChanged += delegate
            {
                Program.SetSetting("backup.snapshot", chkSnapshot.Checked ? "1" : "0");
                FillSources();
                UpdatePreview();
            };
            UpdatePreview();
            Log(L.T("bk.note"));

            btnFolder.Click += delegate { ChooseSourceFolder(); };
            btnSrcRefresh.Click += delegate { FillSources(); };
            btnWim.Click += delegate { ChooseWimToSave(); };
            btnCapture.Click += delegate { StartCapture(); };
            btnRWim.Click += delegate { ChooseWimToRead(); };
            btnRead.Click += delegate { ReadImages(true); };
            btnTgtFolder.Click += delegate { ChooseTargetFolder(); };
            btnTgtRefresh.Click += delegate { FillTargets(); };
            // the volume label and the boot files: only when a volume is formatted
            cboTarget.SelectedIndexChanged += delegate
            {
                Item t = cboTarget.SelectedItem as Item;
                txtLabel.Enabled = t == null || t.Letter != '\0';
                UpdateBoot();
            };
            // None excludes UEFI / BIOS (UEFI + BIOS together = both boot types)
            chkUefi.CheckedChanged += delegate { BootBoxChanged(chkUefi); };
            chkBios.CheckedChanged += delegate { BootBoxChanged(chkBios); };
            chkNoBoot.CheckedChanged += delegate { BootBoxChanged(chkNoBoot); };
            btnPartRefresh.Click += delegate { LoadPartitions(); };
            btnApply.Click += delegate { StartApply(); };
            btnSSrc.Click += delegate { ChooseSplitSource(); };
            btnSSwm.Click += delegate { ChooseSplitTarget(); };
            btnSplit.Click += delegate { StartSplit(); };
            btnJSwm.Click += delegate { ChooseJoinParts(); };
            btnJWim.Click += delegate { ChooseJoinTarget(); };
            btnJoin.Click += delegate { StartJoin(); };
            txtJSwm.Leave += delegate { ShowJoinParts(); };
            btnXSrc.Click += delegate { ChooseExportSource(); };
            btnXRead.Click += delegate { ReadExportImages(true); };
            btnXWim.Click += delegate { ChooseExportTarget(); };
            btnExport.Click += delegate { StartExport(); };
            // name / description: only for one image (all images keep their own)
            cboXImage.SelectedIndexChanged += delegate
            {
                Item it = cboXImage.SelectedItem as Item;
                txtXName.Enabled = txtXDesc.Enabled = it != null && it.Image != null;
            };
            btnIWim.Click += delegate { ChooseInfoWim(); };
            btnIRead.Click += delegate { ReadInfo(true); };
            lvInfo.SelectedIndexChanged += delegate { btnIRestore.Enabled = btnIExport.Enabled = btnIBrowse.Enabled = SelectedInfoImage() != null; };
            btnIBrowse.Click += delegate
            {
                WimImage w = SelectedInfoImage();
                if (w == null) return;
                tabs.SelectedTab = tabBrowse;
                browser.ShowWim(infoWim, info != null && info.ImageCount > 1 ? w.Index : 0);
            };
            lvInfo.DoubleClick += delegate { ShowImageDetails(); };
            btnIRestore.Click += delegate { InfoToRestore(); };
            btnIVerify.Click += delegate { StartVerify(); };
            btnIExport.Click += delegate { InfoToExport(); };
            btnICopy.Click += delegate { CopyInfo(); };
            btnCancel.Click += delegate { CancelJob(); };

            txtName.Text = "backup_" + DateTime.Now.ToString("yyyy-MM-dd");
            // a valid boot manager language is kept for the next start
            cboBootLang.TextChanged += delegate
            {
                string l = cboBootLang.Text.Trim().ToLowerInvariant();
                if (WimLib.IsLocale(l) && l != Program.GetSetting("restore.boot_lang", "")) Program.SetSetting("restore.boot_lang", l);
            };
            FillSources();
        }

        // ---------- layout helpers ----------

        static TableLayoutPanel Grid(int rows)
        {
            TableLayoutPanel t = new TableLayoutPanel();
            t.Dock = DockStyle.Fill;
            t.ColumnCount = 2;
            t.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, Ui.S(150)));
            t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            t.RowCount = rows;
            // the last row (button and hint) gets the rest, for a hint of two lines
            for (int n = 0; n < rows; n++)
                t.RowStyles.Add(n < rows - 1 ? new RowStyle(SizeType.Absolute, Ui.S(34)) : new RowStyle(SizeType.Percent, 100f));
            return t;
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

        static ComboBox Combo(int width)
        {
            ComboBox c = new ComboBox();
            c.DropDownStyle = ComboBoxStyle.DropDownList;
            c.Width = Ui.S(width);
            c.Margin = new Padding(0, Ui.S(5), Ui.S(6), 0);
            return c;
        }

        static TextBox NewText(int width)
        {
            TextBox t = new TextBox();
            t.Width = Ui.S(width);
            t.Margin = new Padding(0, Ui.S(5), Ui.S(6), 0);
            return t;
        }

        // compressor threads, 0 = automatic (one per logical processor)
        static NumericUpDown Threads()
        {
            NumericUpDown t = new NumericUpDown();
            t.Minimum = 0;
            t.Maximum = 64;
            t.Value = 0;
            t.Width = Ui.S(60);
            t.Margin = new Padding(0, Ui.S(5), Ui.S(6), 0);
            return t;
        }

        static Label ThreadsHint()
        {
            Label l = Ui.NewFlowLabel(L.F("bk.threads_hint", Environment.ProcessorCount));
            l.ForeColor = Color.DimGray;
            l.Margin = new Padding(0, Ui.S(8), 0, 0);
            return l;
        }

        // a titled box around a grid of rows
        static GroupBox Group(string title, Control content)
        {
            GroupBox g = new DarkGroupBox();
            g.Text = title;
            g.Font = Ui.FontBold;
            g.Dock = DockStyle.Fill;
            g.Padding = new Padding(Ui.S(6), Ui.S(4), Ui.S(6), 0);
            g.Margin = new Padding(0, 0, 0, Ui.S(4));
            content.Font = Ui.FontUI;
            content.Dock = DockStyle.Fill;
            g.Controls.Add(content);
            return g;
        }

        static CheckBox Check(string text)
        {
            CheckBox c = new CheckBox();
            c.Text = text;
            c.AutoSize = true;
            c.Margin = new Padding(0, Ui.S(7), Ui.S(16), 0);
            return c;
        }

        static RadioButton Radio(string text, bool check)
        {
            RadioButton rb = new RadioButton();
            rb.Text = text;
            rb.AutoSize = true;
            rb.Checked = check;
            rb.Margin = new Padding(0, Ui.S(7), Ui.S(16), 0);
            return rb;
        }

        // ---------- state ----------

        public bool Busy { get { return helper != null; } }

        // 7-Zip extracts files on the browse tab
        public bool BrowserBusy { get { return browser.Busy; } }
        public void CancelBrowser() { browser.Cancel(); }

        string ToolDir
        {
            get
            {
                try { return getPaths()["Lib"]; }
                catch (Exception) { return Libs.Dir(rootDir); }
            }
        }

        static string SystemDrive()
        {
            string s = Environment.GetEnvironmentVariable("SystemDrive");
            return string.IsNullOrEmpty(s) ? "C:" : s;
        }

        static string Gb(long bytes) { return (bytes / 1073741824.0).ToString("0.0") + " GB"; }

        void Log(string text)
        {
            string line = DateTime.Now.ToString("HH:mm:ss") + "  " + text;
            logWin.Append(line);
            if (lblLast != null) lblLast.Text = line.Replace("\r\n", "  ");
        }

        // status text (Ui.Status) in the footer
        public void SetStatus(string text) { lblProgress.Text = text; }

        // a control at the right end of the footer (the language of MainForm)
        public void AddFooter(Control c)
        {
            c.Anchor = AnchorStyles.Left;
            c.Margin = new Padding(Ui.S(4), 0, 0, 0);
            foot.Controls.Add(c, 6, 0);
        }

        // backup tab, right: what the backup will do (UpdatePreview), the hint and the button
        Control Preview(Label hint, Button go)
        {
            TableLayoutPanel box = new TableLayoutPanel();
            box.Dock = DockStyle.Fill;
            box.Margin = new Padding(Ui.S(12), 0, 0, 0);
            box.Padding = new Padding(Ui.S(14), Ui.S(12), Ui.S(14), Ui.S(14));
            box.BackColor = Theme.Panel;
            box.Paint += delegate(object s, PaintEventArgs e)
            {
                using (Pen p = new Pen(Theme.Line)) e.Graphics.DrawRectangle(p, 0, 0, box.Width - 1, box.Height - 1);
            };
            box.ColumnCount = 2;
            box.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, Ui.S(62)));
            box.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            box.RowCount = 9;
            for (int n = 0; n < 6; n++) box.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            box.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            box.RowStyles.Add(new RowStyle(SizeType.AutoSize));
            box.RowStyles.Add(new RowStyle(SizeType.Absolute, Ui.S(50)));
            Label title = new Label();
            title.Text = L.T("pv.title");
            title.Font = Ui.FontBold;
            title.ForeColor = Color.DimGray;
            title.AutoSize = true;
            title.Margin = new Padding(0, 0, 0, Ui.S(10));
            box.Controls.Add(title, 0, 0);
            box.SetColumnSpan(title, 2);
            string[] keys = { "pv.source", "pv.target", "pv.method", "pv.rights", "pv.verify" };
            for (int i = 0; i < keys.Length; i++)
            {
                Label k = new Label();
                k.Text = L.T(keys[i]);
                k.AutoSize = true;
                k.ForeColor = Color.DimGray;
                k.Margin = new Padding(0, 0, 0, Ui.S(8));
                Label v = new Label();
                v.AutoSize = true;
                v.MaximumSize = new Size(Ui.S(176), 0);
                v.Margin = new Padding(0, 0, 0, Ui.S(8));
                if (i == 1) v.Font = Ui.FontMono;
                box.Controls.Add(k, 0, i + 1);
                box.Controls.Add(v, 1, i + 1);
                pv[i] = v;
            }
            box.Controls.Add(hint, 0, 7);
            box.SetColumnSpan(hint, 2);
            box.Controls.Add(go, 0, 8);
            box.SetColumnSpan(go, 2);
            return box;
        }

        void UpdatePreview()
        {
            if (pv[0] == null) return;
            Item src = cboSource.SelectedItem as Item;
            pv[0].Text = src != null ? src.Text : "—";
            string wim = txtWim.Text.Trim();
            // a path breaks after its backslashes (zero width spaces)
            pv[1].Text = wim.Length > 0 ? wim.Replace("\\", "\\\u200B") : "—";
            bool exists = false;
            try { exists = wim.Length > 0 && File.Exists(wim); } catch (Exception) { }
            bool append = exists && rbAppend.Checked;
            string how = L.T(!exists ? "pv.new" : append ? "pv.append" : "pv.replace");
            string comp = append ? L.T("bk.comp_existing") : new[] { "LZX", "XPRESS", "LZMS" }[Math.Max(0, Math.Min(2, cboComp.SelectedIndex))];
            int threads = (int)numThreads.Value;
            pv[2].Text = how + " · " + comp + " · " + (threads > 0 ? L.F("bk.threads_value", threads) : L.F("bk.threads_auto", Environment.ProcessorCount))
                + (chkSnapshot.Checked ? " · " + L.T("pv.snapshot") : "");
            pv[3].Text = L.T("pv.admin");
            pv[4].Text = L.T(chkVerify.Checked ? "pv.on" : "pv.off");
            pv[4].ForeColor = chkVerify.Checked ? Theme.AccentSoft : Theme.Dim;
        }

        void SetBusy(bool busy)
        {
            foreach (Control c in new Control[] { btnCapture, btnApply, btnExport, cboSource, cboTarget, cboImage, txtWim, txtRWim, btnPartRefresh, txtXSrc, txtXWim, cboXImage, btnSplit, btnJoin, txtSSrc, txtSSwm, txtJSwm, txtJWim, chkVerify, chkSnapshot })
                c.Enabled = !busy;
            btnIVerify.Enabled = !busy && infoWim.Length > 0;
            SetDiskBusy(busy);
            if (busy) { chkUefi.Enabled = chkBios.Enabled = chkNoBoot.Enabled = cboSysPart.Enabled = cboBootLang.Enabled = false; }
            else UpdateBoot();
            btnCancel.Enabled = busy;
            if (!busy) { progress.Style = ProgressBarStyle.Blocks; progress.Value = 0; }
        }

        // the default files (image folder of the seeder page) and the lists
        public void RefreshStatus()
        {
            string def = DefaultWim();
            if (txtWim.Text.Trim().Length == 0) txtWim.Text = def;
            if (txtRWim.Text.Trim().Length == 0 && File.Exists(def)) { txtRWim.Text = def; ReadImages(false); }
            if (txtXSrc.Text.Trim().Length == 0 && File.Exists(def)) { txtXSrc.Text = def; ReadExportImages(false); }
            if (txtSSrc.Text.Trim().Length == 0 && File.Exists(def)) { txtSSrc.Text = def; txtSSwm.Text = DefaultSwm(def); }
            if (txtIWim.Text.Trim().Length == 0 && File.Exists(def)) { txtIWim.Text = def; ReadInfo(false); }
            browser.SetDefault(def);
            FillSources();
            FillTargets();
        }

        const string DefaultWimName = "backup.wim";

        // <folder of the last saved WIM>\backup.wim ("" before the first backup)
        string DefaultWim()
        {
            string dir = getImageFolder();
            return dir.Length > 0 ? Path.Combine(dir, DefaultWimName) : "";
        }

        // ---------- volumes ----------

        static List<Item> Volumes(bool readyOnly)
        {
            List<Item> list = new List<Item>();
            foreach (DriveInfo d in DriveInfo.GetDrives())
            {
                if (d.DriveType != DriveType.Fixed && d.DriveType != DriveType.Removable) continue;
                Item it = new Item();
                it.Letter = char.ToUpperInvariant(d.Name[0]);
                it.Path = it.Letter + ":\\";
                string label = "", info;
                try
                {
                    if (d.IsReady)
                    {
                        label = d.VolumeLabel;
                        it.Size = d.TotalSize;
                        info = d.DriveFormat + ", " + Gb(d.TotalSize) + ", " + L.F("bk.vol_free", Gb(d.TotalFreeSpace));
                    }
                    else info = L.T("bk.vol_not_ready");
                }
                catch (Exception) { info = L.T("bk.vol_not_ready"); }
                if (readyOnly && it.Size < 0) continue;
                it.Text = L.F("bk.vol_line", it.Letter, label.Length > 0 ? label : L.T("bk.vol_no_label"), info);
                list.Add(it);
            }
            return list;
        }

        // sources: the ready volumes except the Windows drive (allowed with a
        // snapshot), plus a chosen folder
        void FillSources()
        {
            string keep = SelectedPath(cboSource);
            Item folder = null;
            foreach (object o in cboSource.Items)
                if (((Item)o).Letter == '\0') folder = (Item)o;
            cboSource.Items.Clear();
            if (folder != null) cboSource.Items.Add(folder);
            foreach (Item it in Volumes(true))
            {
                if (!WimLib.IsWindowsDrive(it.Letter)) cboSource.Items.Add(it);
                else if (chkSnapshot.Checked)
                {
                    it.Text += "  " + L.T("bk.vol_running");
                    cboSource.Items.Add(it);
                }
            }
            Select(cboSource, keep);
        }

        // targets: a chosen folder, plus every local volume that may be formatted
        void FillTargets()
        {
            string keep = SelectedPath(cboTarget);
            string wim = txtRWim.Text.Trim();
            Item folder = null;
            foreach (object o in cboTarget.Items)
                if (((Item)o).Letter == '\0') folder = (Item)o;
            cboTarget.Items.Clear();
            if (folder != null) cboTarget.Items.Add(folder);
            int volumes = 0;
            foreach (Item it in Volumes(false))
                if (WimLib.TargetProblem(it.Letter, wim, rootDir) == null) { cboTarget.Items.Add(it); volumes++; }
            Select(cboTarget, keep);
            if (volumes == 0) Log(L.T("bk.no_targets"));
            LoadPartitions();
        }

        // ---------- boot files: firmware and system partition ----------

        // Get-Partition takes a few seconds: read in the background
        void LoadPartitions()
        {
            if (partsLoading) return;
            partsLoading = true;
            btnPartRefresh.Enabled = false;
            cboSysPart.Items.Clear();
            cboSysPart.Items.Add(new PartItem { Text = L.T("bk.syspart_reading") });
            cboSysPart.SelectedIndex = 0;
            System.Threading.ThreadPool.QueueUserWorkItem(delegate
            {
                string err;
                List<DiskPartition> list = WimLib.QueryPartitions(out err);
                try
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        partsLoading = false;
                        btnPartRefresh.Enabled = !Busy;
                        partitions = list;
                        if (list == null) Log(L.F("bk.syspart_failed", err));
                        UpdateBoot();
                    });
                }
                catch (Exception) { }     // the window is closed
            });
        }

        DiskPartition TargetPartition()
        {
            Item t = cboTarget.SelectedItem as Item;
            if (t == null || t.Letter == '\0' || partitions == null) return null;
            foreach (DiskPartition p in partitions)
                if (p.Letter == t.Letter) return p;
            return null;
        }

        void BootBoxChanged(CheckBox box)
        {
            if (bootSync) return;
            bootSync = true;
            if (box == chkNoBoot && chkNoBoot.Checked) chkUefi.Checked = chkBios.Checked = false;
            else if (box != chkNoBoot && box.Checked) chkNoBoot.Checked = false;
            // nothing checked = None
            if (!chkUefi.Checked && !chkBios.Checked) chkNoBoot.Checked = true;
            bootSync = false;
            UpdateBoot();
        }

        // "" = None: no boot files
        string Firmware()
        {
            if (chkNoBoot.Checked) return "";
            if (chkUefi.Checked && chkBios.Checked) return "ALL";
            if (chkUefi.Checked) return "UEFI";
            if (chkBios.Checked) return "BIOS";
            return "";
        }

        // a new target: the firmware boxes and the system partition are
        // chosen again for its disk (refreshing the lists keeps them)
        void UpdateBoot()
        {
            if (partsLoading) return;
            Item t = cboTarget.SelectedItem as Item;
            bool volume = t != null && t.Letter != '\0' && !Busy;
            DiskPartition tp = TargetPartition();
            string key = tp != null ? tp.Disk + ":" + tp.Offset : "";
            bool newTarget = key != bootTargetKey;
            bootTargetKey = key;
            if (newTarget && tp != null)
            {
                bool esp = false;
                foreach (DiskPartition p in partitions)
                    if (p.Disk == tp.Disk && p.IsEsp && !p.IsSystem) esp = true;
                bootSync = true;
                chkUefi.Checked = esp || tp.Gpt;
                chkBios.Checked = !chkUefi.Checked;
                chkNoBoot.Checked = false;
                bootSync = false;
            }
            chkUefi.Enabled = chkBios.Enabled = chkNoBoot.Enabled = volume;
            string fw = Firmware();

            PartItem old = cboSysPart.SelectedItem as PartItem;
            cboSysPart.Items.Clear();
            if (tp != null)
            {
                // the disk of the target first, then the EFI / active partitions of other disks
                foreach (DiskPartition p in partitions)
                    if (p.Disk == tp.Disk && !p.IsMsr && !p.IsSystem) cboSysPart.Items.Add(PartLine(p, tp));
                foreach (DiskPartition p in partitions)
                    if (p.Disk != tp.Disk && (p.IsEsp || p.Active) && !p.IsSystem) cboSysPart.Items.Add(PartLine(p, tp));
            }
            int pick = -1;
            bool sameFw = fw == bootFw;
            bootFw = fw;
            if (!newTarget && sameFw && old != null && old.Part != null)
                for (int n = 0; n < cboSysPart.Items.Count; n++)
                {
                    DiskPartition p = ((PartItem)cboSysPart.Items[n]).Part;
                    if (p.Disk == old.Part.Disk && p.Offset == old.Part.Offset) pick = n;
                }
            if (pick < 0 && tp != null) pick = DefaultSysPart(tp, fw);
            if (pick < 0 && cboSysPart.Items.Count > 0)
            {
                cboSysPart.Items.Insert(0, new PartItem { Text = L.T("bk.syspart_choose") });
                pick = 0;
            }
            if (cboSysPart.Items.Count == 0)
                cboSysPart.Items.Add(new PartItem { Text = L.T(partitions == null ? "bk.syspart_unknown" : t == null || t.Letter == '\0' ? "bk.syspart_volume_only" : "bk.syspart_none") });
            cboSysPart.SelectedIndex = Math.Max(0, pick);
            cboSysPart.Enabled = volume && fw.Length > 0 && tp != null;
            cboBootLang.Enabled = volume && fw.Length > 0;
        }

        // UEFI: the EFI partition of the target disk; BIOS: its active partition, else the target
        int DefaultSysPart(DiskPartition tp, string fw)
        {
            int esp = -1, active = -1, self = -1;
            for (int n = 0; n < cboSysPart.Items.Count; n++)
            {
                DiskPartition p = ((PartItem)cboSysPart.Items[n]).Part;
                if (p.Disk != tp.Disk) continue;
                if (p.IsEsp && esp < 0) esp = n;
                if (p.Active && active < 0) active = n;
                if (p.Offset == tp.Offset) self = n;
            }
            if (fw == "BIOS") return active >= 0 ? active : self;
            return esp >= 0 ? esp : -1;
        }

        static PartItem PartLine(DiskPartition p, DiskPartition target)
        {
            string kind = p.Offset == target.Offset && p.Disk == target.Disk ? L.T("bk.syspart_target")
                : p.IsEsp ? L.T("bk.syspart_esp") : p.Active ? L.T("bk.syspart_active") : "";
            string size = p.Size >= 1073741824 ? Gb(p.Size) : (p.Size / 1048576) + " MB";
            PartItem it = new PartItem();
            it.Part = p;
            it.Text = L.F("bk.syspart_line", p.Disk, p.Number, kind.Length > 0 ? kind : "-",
                p.FileSystem.Length > 0 ? p.FileSystem : "RAW", size,
                p.Letter != '\0' ? p.Letter + ":" : L.T("bk.syspart_no_letter"));
            return it;
        }

        void ChooseTargetFolder()
        {
            using (FolderBrowserDialog dlg = new FolderBrowserDialog())
            {
                dlg.Description = L.T("bk.select_target");
                dlg.ShowNewFolderButton = true;
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                string why = WimLib.FolderTargetProblem(dlg.SelectedPath, txtRWim.Text.Trim(), rootDir);
                if (why != null) { Ui.Info(L.F(why, dlg.SelectedPath), MessageBoxIcon.Warning); return; }
                Item it = new Item();
                it.Path = dlg.SelectedPath;
                it.Text = L.F("bk.target_folder", dlg.SelectedPath);
                for (int n = cboTarget.Items.Count - 1; n >= 0; n--)
                    if (((Item)cboTarget.Items[n]).Letter == '\0') cboTarget.Items.RemoveAt(n);
                cboTarget.Items.Insert(0, it);
                cboTarget.SelectedIndex = 0;
            }
        }

        static long FreeSpace(string path)
        {
            try { return new DriveInfo(Path.GetPathRoot(Path.GetFullPath(path))).TotalFreeSpace; }
            catch (Exception) { return -1; }
        }

        static string SelectedPath(ComboBox c)
        {
            Item it = c.SelectedItem as Item;
            return it != null ? it.Path : "";
        }

        static void Select(ComboBox c, string path)
        {
            for (int n = 0; n < c.Items.Count; n++)
                if (string.Equals(((Item)c.Items[n]).Path, path, StringComparison.OrdinalIgnoreCase)) { c.SelectedIndex = n; return; }
            if (c.Items.Count > 0 && path.Length == 0) c.SelectedIndex = -1;
        }

        void ChooseSourceFolder()
        {
            using (FolderBrowserDialog dlg = new FolderBrowserDialog())
            {
                dlg.Description = L.T("bk.select_source");
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                Item it = new Item();
                it.Path = dlg.SelectedPath;
                it.Text = L.F("bk.source_folder", dlg.SelectedPath);
                for (int n = cboSource.Items.Count - 1; n >= 0; n--)
                    if (((Item)cboSource.Items[n]).Letter == '\0') cboSource.Items.RemoveAt(n);
                cboSource.Items.Insert(0, it);
                cboSource.SelectedIndex = 0;
            }
        }

        void ChooseWimToSave()
        {
            using (SaveFileDialog dlg = new SaveFileDialog())
            {
                dlg.Filter = "WIM (*.wim)|*.wim";
                dlg.OverwritePrompt = false;
                string cur = txtWim.Text.Trim();
                if (cur.Length == 0) cur = DefaultWim();
                try
                {
                    if (cur.Length > 0 && Directory.Exists(Path.GetDirectoryName(cur))) dlg.InitialDirectory = Path.GetDirectoryName(cur);
                }
                catch (Exception) { }
                dlg.FileName = cur.Length > 0 ? Path.GetFileName(cur) : DefaultWimName;
                if (dlg.ShowDialog(this) == DialogResult.OK) txtWim.Text = dlg.FileName;
            }
        }

        void ChooseWimToRead()
        {
            using (OpenFileDialog dlg = new OpenFileDialog())
            {
                // a split WIM: any of its parts (the others are found in its folder)
                dlg.Filter = "WIM (*.wim;*.esd;*.swm)|*.wim;*.esd;*.swm|*.*|*.*";
                string cur = txtRWim.Text.Trim();
                try
                {
                    if (cur.Length > 0 && Directory.Exists(Path.GetDirectoryName(cur))) dlg.InitialDirectory = Path.GetDirectoryName(cur);
                }
                catch (Exception) { }
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                txtRWim.Text = dlg.FileName;
                ReadImages(true);
            }
        }

        // ---------- images (in this process, no administrator rights) ----------

        // null + message when libwim-15.dll cannot be used
        bool LoadWimlib()
        {
            // lib\libwim-15.dll: downloaded when missing
            string err = Libs.EnsureWimlib(FindForm(), rootDir);
            if (err == null) err = WimLib.Load(ToolDir, false);
            if (err == null) return true;
            Log(err);
            Ui.Info(L.F("bk.no_wimlib", err, ToolDir), MessageBoxIcon.Warning);
            return false;
        }

        List<WimImage> Images(string wim, bool show)
        {
            if (!LoadWimlib()) return null;
            string err;
            List<WimImage> list = WimLib.ReadImages(wim, out err);
            if (list == null)
            {
                Log(L.F("bk.read_failed", wim, err).Replace("\r\n", " "));
                if (show) Ui.Info(L.F("bk.read_failed", wim, err), MessageBoxIcon.Warning);
            }
            return list;
        }

        void ReadImages(bool show)
        {
            cboImage.Items.Clear();
            string wim = txtRWim.Text.Trim();
            FillTargets();
            if (wim.Length == 0 || !File.Exists(wim))
            {
                if (show) Ui.Info(L.F("msg.not_found", wim.Length == 0 ? "-" : wim), MessageBoxIcon.Warning);
                return;
            }
            List<WimImage> list = Images(wim, show);
            if (list == null) return;
            foreach (WimImage w in list)
            {
                Item it = new Item();
                it.Image = w;
                it.Path = w.Index.ToString();
                it.Text = L.F("bk.image_line", w.Index, w.Name.Length > 0 ? w.Name : L.T("bk.no_name"),
                    w.TotalBytes >= 0 ? Gb(w.TotalBytes) : "?")
                    + (w.Description.Length > 0 ? "  - " + w.Description : "");
                cboImage.Items.Add(it);
            }
            if (cboImage.Items.Count > 0) cboImage.SelectedIndex = 0;
            Log(L.F("bk.log_read", wim, list.Count));
        }

        // ---------- backup ----------

        void StartCapture()
        {
            if (Busy) return;
            Item src = cboSource.SelectedItem as Item;
            string source = src != null ? src.Path : "";
            string wim = txtWim.Text.Trim();
            string name = txtName.Text.Trim();
            bool snapshot = chkSnapshot.Checked;
            string why = WimLib.CaptureProblem(source, wim, snapshot);
            if (why != null)
            {
                Ui.Info(L.F(why, source.Length == 0 ? "-" : source, wim), MessageBoxIcon.Warning);
                return;
            }
            // VSS cannot be used from a 32-bit process on 64-bit Windows
            if (snapshot && Environment.Is64BitOperatingSystem && !Environment.Is64BitProcess)
            {
                Ui.Info(L.T("bk.err_snapshot_32bit"), MessageBoxIcon.Warning);
                return;
            }
            if (!LoadWimlib()) return;

            bool exists = File.Exists(wim);
            bool append = exists && rbAppend.Checked;
            int[] types = { WimLib.CompressionLzx, WimLib.CompressionXpress, WimLib.CompressionLzms };
            string what = L.T("bk.confirm_new");
            int newIndex = 1;
            if (append)
            {
                List<WimImage> old = Images(wim, true);
                if (old == null) return;
                foreach (WimImage w in old)
                    if (name.Length > 0 && string.Equals(w.Name, name, StringComparison.OrdinalIgnoreCase))
                    {
                        Ui.Info(L.F("bk.err_name_used", name), MessageBoxIcon.Warning);
                        return;
                    }
                newIndex = old.Count + 1;
                what = L.F("bk.confirm_append", newIndex);
            }
            else if (exists) what = L.T("bk.confirm_replace");
            if (snapshot) what += "\r\n" + L.T("bk.confirm_snapshot");
            int threads = (int)numThreads.Value;
            string threadText = threads > 0 ? L.F("bk.threads_value", threads) : L.F("bk.threads_auto", Environment.ProcessorCount);
            if (!Ui.YesNo(L.F("bk.confirm_capture", source, wim, name.Length > 0 ? name : L.T("bk.no_name"),
                    (append ? L.T("bk.comp_existing") : cboComp.SelectedItem) + ", " + threadText, what), MessageBoxIcon.Question))
                return;

            Dictionary<string, string> job = new Dictionary<string, string>();
            job["mode"] = "capture";
            job["source"] = source;
            job["wim"] = wim;
            job["name"] = name.Replace("\r", " ").Replace("\n", " ");
            job["description"] = txtDesc.Text.Trim().Replace("\r", " ").Replace("\n", " ");
            job["compression"] = types[Math.Max(0, cboComp.SelectedIndex)].ToString();
            job["threads"] = threads.ToString();
            job["append"] = append ? "1" : "0";
            job["verify"] = chkVerify.Checked ? "1" : "0";
            job["snapshot"] = snapshot ? "1" : "0";
            jobVerify = chkVerify.Checked;
            jobWim = wim;
            jobImage = newIndex;
            jobTarget = "";
            Log(L.F("bk.log_start_capture", source, wim) + "  (" + threadText + ")");
            StartHelper(job);
        }

        // ---------- export ----------

        static string InitialDir(string file)
        {
            try
            {
                if (file.Length > 0 && Directory.Exists(Path.GetDirectoryName(file))) return Path.GetDirectoryName(file);
            }
            catch (Exception) { }
            return null;
        }

        void ChooseExportSource()
        {
            using (OpenFileDialog dlg = new OpenFileDialog())
            {
                dlg.Filter = "WIM (*.wim;*.esd;*.swm)|*.wim;*.esd;*.swm|*.*|*.*";
                string dir = InitialDir(txtXSrc.Text.Trim());
                if (dir != null) dlg.InitialDirectory = dir;
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                txtXSrc.Text = dlg.FileName;
                ReadExportImages(true);
            }
        }

        void ChooseExportTarget()
        {
            using (SaveFileDialog dlg = new SaveFileDialog())
            {
                dlg.Filter = "WIM (*.wim)|*.wim|ESD (*.esd)|*.esd";
                dlg.OverwritePrompt = false;
                string cur = txtXWim.Text.Trim();
                if (cur.Length == 0) cur = DefaultExportWim(txtXSrc.Text.Trim());
                string dir = InitialDir(cur);
                if (dir != null) dlg.InitialDirectory = dir;
                if (cur.EndsWith(".esd", StringComparison.OrdinalIgnoreCase)) dlg.FilterIndex = 2;
                dlg.FileName = cur.Length > 0 ? Path.GetFileName(cur) : "export.wim";
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                txtXWim.Text = dlg.FileName;
                // an .esd file is written solid (LZMS)
                if (dlg.FileName.EndsWith(".esd", StringComparison.OrdinalIgnoreCase)) cboXComp.SelectedIndex = 2;
            }
        }

        // <folder of the source>\<name of the source>_export.wim
        static string DefaultExportWim(string source)
        {
            try
            {
                if (source.Length == 0) return "";
                return Path.Combine(Path.GetDirectoryName(Path.GetFullPath(source)), Path.GetFileNameWithoutExtension(source) + "_export.wim");
            }
            catch (Exception) { return ""; }
        }

        static string ImageText(WimImage w)
        {
            return L.F("bk.image_line", w.Index, w.Name.Length > 0 ? w.Name : L.T("bk.no_name"), w.TotalBytes >= 0 ? Gb(w.TotalBytes) : "?")
                + (w.Description.Length > 0 ? "  - " + w.Description : "");
        }

        void ReadExportImages(bool show)
        {
            cboXImage.Items.Clear();
            string wim = txtXSrc.Text.Trim();
            if (wim.Length == 0 || !File.Exists(wim))
            {
                if (show) Ui.Info(L.F("msg.not_found", wim.Length == 0 ? "-" : wim), MessageBoxIcon.Warning);
                return;
            }
            List<WimImage> list = Images(wim, show);
            if (list == null) return;
            Item all = new Item();
            all.Path = "0";
            all.Text = L.F("ex.all_images", list.Count);
            cboXImage.Items.Add(all);
            foreach (WimImage w in list)
            {
                Item it = new Item();
                it.Image = w;
                it.Path = w.Index.ToString();
                it.Text = ImageText(w);
                cboXImage.Items.Add(it);
            }
            // one image: that image; several: all of them
            cboXImage.SelectedIndex = list.Count == 1 ? 1 : 0;
            if (txtXWim.Text.Trim().Length == 0) txtXWim.Text = DefaultExportWim(wim);
            Log(L.F("bk.log_read", wim, list.Count));
        }

        void StartExport()
        {
            if (Busy) return;
            string source = txtXSrc.Text.Trim(), wim = txtXWim.Text.Trim();
            string why = WimLib.ExportProblem(source, wim);
            if (why != null) { Ui.Info(L.F(why, source.Length == 0 ? "-" : source, wim), MessageBoxIcon.Warning); return; }
            Item img = cboXImage.SelectedItem as Item;
            if (img == null) { Ui.Info(L.T("bk.err_no_image"), MessageBoxIcon.Warning); return; }
            if (!LoadWimlib()) return;

            bool one = img.Image != null;
            string name = one ? txtXName.Text.Trim() : "";
            string desc = one ? txtXDesc.Text.Trim() : "";
            bool exists = File.Exists(wim);
            bool append = exists && rbXAppend.Checked;
            string what = L.T("bk.confirm_new");
            if (append)
            {
                // the names of the new images must not be in the target yet
                List<WimImage> old = Images(wim, true);
                if (old == null) return;
                List<WimImage> src = Images(source, true);
                if (src == null) return;
                List<string> names = new List<string>();
                if (one) names.Add(name.Length > 0 ? name : img.Image.Name);
                else foreach (WimImage w in src) names.Add(w.Name);
                foreach (WimImage w in old)
                    foreach (string nm in names)
                        if (nm.Length > 0 && string.Equals(w.Name, nm, StringComparison.OrdinalIgnoreCase))
                        {
                            Ui.Info(L.F(one ? "bk.err_name_used" : "ex.err_names_used", nm), MessageBoxIcon.Warning);
                            return;
                        }
                what = L.F("ex.confirm_append", old.Count + 1);
            }
            else if (exists) what = L.T("bk.confirm_replace");
            int[] types = { WimLib.CompressionLzx, WimLib.CompressionXpress, WimLib.CompressionLzms };
            int threads = (int)numXThreads.Value;
            string threadText = threads > 0 ? L.F("bk.threads_value", threads) : L.F("bk.threads_auto", Environment.ProcessorCount);
            string imageText = img.Text + (one && name.Length > 0 ? "  ->  " + name : "");
            if (!Ui.YesNo(L.F("ex.confirm", source, imageText, wim,
                    (append ? L.T("bk.comp_existing") : cboXComp.SelectedItem) + ", " + threadText, what), MessageBoxIcon.Question))
                return;

            Dictionary<string, string> job = new Dictionary<string, string>();
            job["mode"] = "export";
            job["source"] = source;
            job["index"] = one ? img.Image.Index.ToString() : "0";
            job["wim"] = wim;
            job["name"] = name.Replace("\r", " ").Replace("\n", " ");
            job["description"] = desc.Replace("\r", " ").Replace("\n", " ");
            job["compression"] = types[Math.Max(0, cboXComp.SelectedIndex)].ToString();
            job["threads"] = threads.ToString();
            job["append"] = append ? "1" : "0";
            jobWim = wim;
            jobImage = one ? img.Image.Index : 0;
            jobTarget = "";
            Log(L.F("ex.log_start", one ? img.Image.Index.ToString() : L.T("ex.all"), source, wim) + "  (" + threadText + ")");
            StartHelper(job);
        }

        string ExportDone(string elapsed)
        {
            string msg = L.F("ex.done", jobWim) + "\r\n\r\n" + elapsed;
            // the other tabs show the new content of the file
            if (string.Equals(txtRWim.Text.Trim(), jobWim, StringComparison.OrdinalIgnoreCase)) ReadImages(false);
            if (string.Equals(txtXSrc.Text.Trim(), jobWim, StringComparison.OrdinalIgnoreCase)) ReadExportImages(false);
            if (string.Equals(txtIWim.Text.Trim(), jobWim, StringComparison.OrdinalIgnoreCase) || txtIWim.Text.Trim().Length == 0)
            {
                txtIWim.Text = jobWim;
                ReadInfo(false);
            }
            return msg;
        }

        // ---------- split / join ----------

        // <folder of the WIM>\<name of the WIM>.swm
        static string DefaultSwm(string wim)
        {
            try { return Path.Combine(Path.GetDirectoryName(Path.GetFullPath(wim)), Path.GetFileNameWithoutExtension(wim) + ".swm"); }
            catch (Exception) { return ""; }
        }

        void ChooseSplitSource()
        {
            using (OpenFileDialog dlg = new OpenFileDialog())
            {
                dlg.Filter = "WIM (*.wim)|*.wim|*.*|*.*";
                string dir = InitialDir(txtSSrc.Text.Trim());
                if (dir != null) dlg.InitialDirectory = dir;
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                txtSSrc.Text = dlg.FileName;
                txtSSwm.Text = DefaultSwm(dlg.FileName);
            }
        }

        void ChooseSplitTarget()
        {
            using (SaveFileDialog dlg = new SaveFileDialog())
            {
                dlg.Filter = "SWM (*.swm)|*.swm";
                dlg.OverwritePrompt = false;
                string cur = txtSSwm.Text.Trim();
                if (cur.Length == 0) cur = DefaultSwm(txtSSrc.Text.Trim());
                string dir = InitialDir(cur);
                if (dir != null) dlg.InitialDirectory = dir;
                dlg.FileName = cur.Length > 0 ? Path.GetFileName(cur) : "install.swm";
                if (dlg.ShowDialog(this) == DialogResult.OK) txtSSwm.Text = dlg.FileName;
            }
        }

        void StartSplit()
        {
            if (Busy) return;
            string source = txtSSrc.Text.Trim(), swm = txtSSwm.Text.Trim();
            string why = WimLib.SplitProblem(source, swm);
            if (why != null) { Ui.Info(L.F(why, source.Length == 0 ? "-" : source, swm), MessageBoxIcon.Warning); return; }
            if (!LoadWimlib()) return;
            // wimlib cannot split solid resources (ESD, LZMS solid)
            string err;
            WimFileInfo f = WimLib.ReadInfo(source, out err);
            if (f == null) { Ui.Info(L.F("bk.read_failed", source, err), MessageBoxIcon.Warning); return; }
            if (f.Solid) { Ui.Info(L.F("sj.err_solid", source), MessageBoxIcon.Warning); return; }
            if (f.TotalParts > 1) { Ui.Info(L.F("sj.err_already_split", source), MessageBoxIcon.Warning); return; }
            long partMb = (long)numSPart.Value;
            long size = f.FileBytes > 0 ? f.FileBytes : f.TotalBytes;
            long estimate = Math.Max(1, (size + partMb * 1048576 - 1) / (partMb * 1048576));
            List<string> old = WimLib.SwmParts(swm);
            string what = old.Count > 0 ? "\r\n\r\n" + L.F("sj.confirm_old_parts", old.Count, Path.GetDirectoryName(Path.GetFullPath(swm))) : "";
            if (!Ui.YesNo(L.F("sj.confirm_split", source, swm, partMb.ToString("N0"), estimate, what), old.Count > 0 ? MessageBoxIcon.Warning : MessageBoxIcon.Question))
                return;

            Dictionary<string, string> job = new Dictionary<string, string>();
            job["mode"] = "split";
            job["source"] = source;
            job["swm"] = swm;
            job["part_mb"] = partMb.ToString();
            jobWim = swm;
            jobImage = 0;
            jobTarget = "";
            Log(L.F("sj.log_split", source, swm, partMb.ToString("N0")));
            StartHelper(job);
        }

        string SplitDone(string elapsed)
        {
            List<string> parts = WimLib.SwmParts(jobWim);
            StringBuilder sb = new StringBuilder();
            foreach (string part in parts)
            {
                long len = -1;
                try { len = new FileInfo(part).Length; } catch (Exception) { }
                sb.AppendLine("  " + Path.GetFileName(part) + "  (" + (len >= 0 ? (len / 1048576.0).ToString("N0") + " MB" : "?") + ")");
            }
            // the join box shows the new parts
            txtJSwm.Text = jobWim;
            ShowJoinParts();
            return L.F("sj.split_done", parts.Count, Path.GetDirectoryName(Path.GetFullPath(jobWim)), sb.ToString().TrimEnd()) + "\r\n\r\n" + elapsed;
        }

        void ChooseJoinParts()
        {
            using (OpenFileDialog dlg = new OpenFileDialog())
            {
                dlg.Filter = "SWM (*.swm)|*.swm|*.*|*.*";
                string dir = InitialDir(txtJSwm.Text.Trim());
                if (dir != null) dlg.InitialDirectory = dir;
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                List<string> parts = WimLib.SwmParts(dlg.FileName);
                txtJSwm.Text = parts.Count > 0 ? parts[0] : dlg.FileName;
                txtJWim.Text = "";
                ShowJoinParts();
            }
        }

        // "<n> parts found" next to the first part; the default target
        void ShowJoinParts()
        {
            string first = txtJSwm.Text.Trim();
            List<string> parts = WimLib.SwmParts(first);
            lblJParts.Text = first.Length == 0 ? "" : parts.Count == 0 ? L.T("sj.parts_none") : L.F("sj.parts_found", parts.Count);
            if (parts.Count > 0 && txtJWim.Text.Trim().Length == 0)
            {
                string wim = Path.Combine(Path.GetDirectoryName(parts[0]), Path.GetFileNameWithoutExtension(parts[0]) + ".wim");
                if (File.Exists(wim)) wim = Path.Combine(Path.GetDirectoryName(parts[0]), Path.GetFileNameWithoutExtension(parts[0]) + "_joined.wim");
                txtJWim.Text = wim;
            }
        }

        void ChooseJoinTarget()
        {
            using (SaveFileDialog dlg = new SaveFileDialog())
            {
                dlg.Filter = "WIM (*.wim)|*.wim";
                dlg.OverwritePrompt = false;
                string cur = txtJWim.Text.Trim();
                string dir = InitialDir(cur.Length > 0 ? cur : txtJSwm.Text.Trim());
                if (dir != null) dlg.InitialDirectory = dir;
                dlg.FileName = cur.Length > 0 ? Path.GetFileName(cur) : "joined.wim";
                if (dlg.ShowDialog(this) == DialogResult.OK) txtJWim.Text = dlg.FileName;
            }
        }

        void StartJoin()
        {
            if (Busy) return;
            ShowJoinParts();
            string first = txtJSwm.Text.Trim(), wim = txtJWim.Text.Trim();
            List<string> parts = WimLib.SwmParts(first);
            string why = WimLib.JoinProblem(parts, wim);
            if (why != null) { Ui.Info(L.F(why, first.Length == 0 ? "-" : first, wim), MessageBoxIcon.Warning); return; }
            if (!LoadWimlib()) return;
            // the first part tells how many parts there are
            string err;
            WimFileInfo f = WimLib.ReadInfo(parts[0], out err);
            if (f == null) { Ui.Info(L.F("bk.read_failed", parts[0], err), MessageBoxIcon.Warning); return; }
            if (f.TotalParts <= 1) { Ui.Info(L.F("sj.err_not_split", parts[0]), MessageBoxIcon.Warning); return; }
            if (f.PartNumber != 1) { Ui.Info(L.F("sj.err_not_first", parts[0], f.PartNumber), MessageBoxIcon.Warning); return; }
            if (parts.Count != f.TotalParts) { Ui.Info(L.F("sj.err_part_count", parts[0], f.TotalParts, parts.Count), MessageBoxIcon.Warning); return; }
            long total = 0;
            foreach (string part in parts) try { total += new FileInfo(part).Length; } catch (Exception) { }
            string what = File.Exists(wim) ? L.T("bk.confirm_replace") : L.T("bk.confirm_new");
            if (!Ui.YesNo(L.F("sj.confirm_join", parts[0], parts.Count, Gb(total), wim, what), MessageBoxIcon.Question)) return;

            Dictionary<string, string> job = new Dictionary<string, string>();
            job["mode"] = "join";
            job["parts"] = string.Join("|", parts.ToArray());
            job["wim"] = wim;
            jobWim = wim;
            jobImage = 0;
            jobTarget = "";
            Log(L.F("sj.log_join", parts.Count, parts[0], wim));
            StartHelper(job);
        }

        string JoinDone(string elapsed)
        {
            // the other tabs show the joined file when they showed it before
            if (string.Equals(txtIWim.Text.Trim(), jobWim, StringComparison.OrdinalIgnoreCase) || txtIWim.Text.Trim().Length == 0)
            {
                txtIWim.Text = jobWim;
                ReadInfo(false);
            }
            if (string.Equals(txtRWim.Text.Trim(), jobWim, StringComparison.OrdinalIgnoreCase)) ReadImages(false);
            return L.F("sj.join_done", jobWim) + "\r\n\r\n" + elapsed;
        }

        // ---------- verify (helper without administrator rights; nothing is written) ----------

        void StartVerify()
        {
            if (Busy || infoWim.Length == 0) return;
            if (!File.Exists(infoWim)) { Ui.Info(L.F("msg.not_found", infoWim), MessageBoxIcon.Warning); return; }
            if (!LoadWimlib()) return;
            Dictionary<string, string> job = new Dictionary<string, string>();
            job["mode"] = "verify";
            job["wim"] = infoWim;
            jobWim = infoWim;
            jobImage = 0;
            jobTarget = "";
            jobIntegrity = info != null && info.Integrity;
            Log(L.F("vf.log_start", infoWim));
            StartHelper(job);
        }

        // ---------- info (in this process, nothing is changed) ----------

        void ChooseInfoWim()
        {
            using (OpenFileDialog dlg = new OpenFileDialog())
            {
                dlg.Filter = "WIM (*.wim;*.esd;*.swm)|*.wim;*.esd;*.swm|*.*|*.*";
                string dir = InitialDir(txtIWim.Text.Trim());
                if (dir != null) dlg.InitialDirectory = dir;
                if (dlg.ShowDialog(this) != DialogResult.OK) return;
                txtIWim.Text = dlg.FileName;
                ReadInfo(true);
            }
        }

        static string CompressionName(int c)
        {
            switch (c)
            {
                case 0: return L.T("in.comp_none");
                case 1: return "XPRESS";
                case 2: return "LZX";
                case 3: return "LZMS";
                default: return c.ToString();
            }
        }

        static string SizeText(long bytes)
        {
            if (bytes < 0) return "?";
            if (bytes >= 1073741824) return Gb(bytes);
            return (bytes / 1048576.0).ToString("0.0") + " MB";
        }

        static string When(DateTime t) { return t == DateTime.MinValue ? "" : t.ToString("yyyy-MM-dd HH:mm"); }

        // the edition of an image: FLAGS (e.g. Professional), else EDITIONID
        static string Edition(WimImage w) { return w.Flags.Length > 0 ? w.Flags : w.EditionId; }

        void ReadInfo(bool show)
        {
            lvInfo.Items.Clear();
            info = null;
            infoWim = "";
            btnIRestore.Enabled = btnIExport.Enabled = btnIBrowse.Enabled = btnIVerify.Enabled = false;
            lblISummary.Text = L.T("in.none");
            string wim = txtIWim.Text.Trim();
            if (wim.Length == 0 || !File.Exists(wim))
            {
                if (show) Ui.Info(L.F("msg.not_found", wim.Length == 0 ? "-" : wim), MessageBoxIcon.Warning);
                return;
            }
            if (!LoadWimlib()) return;
            string err;
            WimFileInfo f;
            Cursor old = Cursor.Current;
            Cursor.Current = Cursors.WaitCursor;
            try { f = WimLib.ReadInfo(wim, out err); }
            finally { Cursor.Current = old; }
            if (f == null)
            {
                Log(L.F("bk.read_failed", wim, err).Replace("\r\n", " "));
                if (show) Ui.Info(L.F("bk.read_failed", wim, err), MessageBoxIcon.Warning);
                return;
            }
            info = f;
            infoWim = wim;
            btnIVerify.Enabled = !Busy;
            lblISummary.Text = Summary(f);
            foreach (WimImage w in f.Images)
            {
                ListViewItem li = new ListViewItem(w.Index.ToString());
                li.SubItems.Add(w.Name.Length > 0 ? w.Name : L.T("bk.no_name"));
                li.SubItems.Add(Edition(w));
                li.SubItems.Add(w.Arch);
                li.SubItems.Add(w.Version);
                li.SubItems.Add(w.DefaultLanguage.Length > 0 ? w.DefaultLanguage : w.Languages);
                li.SubItems.Add(SizeText(w.TotalBytes));
                li.SubItems.Add(w.FileCount >= 0 ? L.F("in.files_value", w.FileCount.ToString("N0"), Math.Max(0, w.DirCount).ToString("N0")) : "");
                li.SubItems.Add(When(w.Created));
                li.SubItems.Add(When(w.Modified));
                li.SubItems.Add(w.Description);
                li.Tag = w;
                if (w.Index == f.BootIndex) li.Font = Ui.FontBold;
                lvInfo.Items.Add(li);
            }
            if (lvInfo.Items.Count > 0) lvInfo.Items[0].Selected = true;
            Log(L.F("bk.log_read", wim, f.ImageCount));
        }

        // two lines: images, size, compression; version, parts, boot image, flags, GUID
        static string Summary(WimFileInfo f)
        {
            string comp = CompressionName(f.Compression) + (f.Solid ? " " + L.T("in.solid") : "");
            if (f.ChunkSize > 0) comp += ", " + L.F("in.chunk", f.ChunkSize / 1024);
            List<string> flags = new List<string>();
            if (f.Integrity) flags.Add(L.T("in.flag_integrity"));
            if (f.Pipable) flags.Add(L.T("in.flag_pipable"));
            if (f.ReadOnlyFlag) flags.Add(L.T("in.flag_readonly"));
            if (f.Spanned || f.TotalParts > 1) flags.Add(L.F("in.flag_part", f.PartNumber, f.TotalParts));
            return L.F("in.summary1", f.ImageCount, SizeText(f.FileBytes >= 0 ? f.FileBytes : f.TotalBytes), comp,
                    f.BootIndex > 0 ? f.BootIndex.ToString() : L.T("in.boot_none"))
                + "\r\n" + L.F("in.summary2", "0x" + f.Version.ToString("X"), f.Guid,
                    flags.Count > 0 ? string.Join(", ", flags.ToArray()) : "-");
        }

        WimImage SelectedInfoImage()
        {
            return lvInfo.SelectedItems.Count > 0 ? lvInfo.SelectedItems[0].Tag as WimImage : null;
        }

        // all properties of one image, in a message
        string ImageDetails(WimImage w)
        {
            StringBuilder sb = new StringBuilder();
            Action<string, string> add = delegate(string key, string value)
            {
                if (!string.IsNullOrEmpty(value)) sb.AppendLine(L.T(key) + ": " + value);
            };
            add("in.col_index", w.Index.ToString());
            add("in.col_name", w.Name);
            add("in.col_desc", w.Description);
            add("in.d_display", w.DisplayName);
            add("in.d_product", w.ProductName);
            add("in.col_edition", Edition(w));
            add("in.d_install", w.InstallationType);
            add("in.col_arch", w.Arch);
            add("in.col_version", w.Version);
            add("in.d_default_lang", w.DefaultLanguage);
            add("in.col_lang", w.Languages);
            add("in.col_size", w.TotalBytes >= 0 ? SizeText(w.TotalBytes) + "  (" + w.TotalBytes.ToString("N0") + " bytes)" : "");
            add("in.d_files", w.FileCount >= 0 ? w.FileCount.ToString("N0") : "");
            add("in.d_dirs", w.DirCount >= 0 ? w.DirCount.ToString("N0") : "");
            add("in.col_created", When(w.Created));
            add("in.col_modified", When(w.Modified));
            return sb.ToString();
        }

        void ShowImageDetails()
        {
            WimImage w = SelectedInfoImage();
            if (w != null) Ui.Info(ImageDetails(w));
        }

        // the whole info (file and every image) to the clipboard
        void CopyInfo()
        {
            if (info == null) { Ui.Info(L.T("in.none"), MessageBoxIcon.Warning); return; }
            StringBuilder sb = new StringBuilder();
            sb.AppendLine(infoWim);
            sb.AppendLine(Summary(info));
            foreach (WimImage w in info.Images)
            {
                sb.AppendLine();
                sb.Append(ImageDetails(w));
            }
            try
            {
                Clipboard.SetText(sb.ToString());
                Ui.Status(L.T("in.copied"));
            }
            catch (Exception ex) { Ui.Info(ex.Message, MessageBoxIcon.Warning); }
        }

        static void SelectImage(ComboBox c, int index)
        {
            for (int n = 0; n < c.Items.Count; n++)
            {
                Item it = c.Items[n] as Item;
                if (it != null && it.Image != null && it.Image.Index == index) { c.SelectedIndex = n; return; }
            }
        }

        void InfoToRestore()
        {
            WimImage w = SelectedInfoImage();
            if (w == null || Busy) return;
            txtRWim.Text = infoWim;
            ReadImages(true);
            SelectImage(cboImage, w.Index);
            tabs.SelectedTab = tabApply;
        }

        void InfoToExport()
        {
            WimImage w = SelectedInfoImage();
            if (w == null || Busy) return;
            if (!string.Equals(txtXSrc.Text.Trim(), infoWim, StringComparison.OrdinalIgnoreCase)) txtXWim.Text = "";
            txtXSrc.Text = infoWim;
            ReadExportImages(true);
            SelectImage(cboXImage, w.Index);
            tabs.SelectedTab = tabExport;
        }

        // ---------- restore ----------

        void StartApply()
        {
            if (Busy) return;
            string wim = txtRWim.Text.Trim();
            Item img = cboImage.SelectedItem as Item;
            Item tgt = cboTarget.SelectedItem as Item;
            if (wim.Length == 0 || !File.Exists(wim)) { Ui.Info(L.F("msg.not_found", wim.Length == 0 ? "-" : wim), MessageBoxIcon.Warning); return; }
            if (img == null) { Ui.Info(L.T("bk.err_no_image"), MessageBoxIcon.Warning); return; }
            if (tgt == null) { Ui.Info(L.T("bk.err_no_target"), MessageBoxIcon.Warning); return; }
            if (tgt.Letter == '\0') { StartApplyToFolder(wim, img, tgt.Path); return; }
            string why = WimLib.TargetProblem(tgt.Letter, wim, rootDir);
            if (why != null) { Ui.Info(L.F(why, tgt.Letter), MessageBoxIcon.Warning); FillTargets(); return; }
            if (img.Image.TotalBytes > 0 && tgt.Size > 0 && tgt.Size < img.Image.TotalBytes)
            {
                Ui.Info(L.F("bk.err_too_small", Gb(img.Image.TotalBytes), tgt.Letter, Gb(tgt.Size)), MessageBoxIcon.Warning);
                return;
            }
            // boot files: firmware and system partition
            string fw = Firmware();
            DiskPartition sys = null;
            if (fw.Length > 0)
            {
                PartItem pi = cboSysPart.SelectedItem as PartItem;
                sys = pi != null ? pi.Part : null;
                if (sys == null) { Ui.Info(L.T("bk.err_no_syspart"), MessageBoxIcon.Warning); return; }
                DiskPartition check = sys;
                if (sys.Letter == tgt.Letter)
                {
                    // the target itself: NTFS after the format
                    check = new DiskPartition { Disk = sys.Disk, Number = sys.Number, Letter = sys.Letter, GptType = sys.GptType, MbrType = sys.MbrType, Style = sys.Style, FileSystem = "NTFS" };
                }
                string sysWhy = WimLib.SystemPartitionProblem(check, fw);
                if (sysWhy != null) { Ui.Info(L.F(sysWhy, sys.Disk, sys.Number, fw), MessageBoxIcon.Warning); return; }
            }
            string bootLang = cboBootLang.Text.Trim().ToLowerInvariant();
            if (sys != null && !WimLib.IsLocale(bootLang)) { Ui.Info(L.F("bk.err_boot_lang", bootLang.Length > 0 ? bootLang : "-"), MessageBoxIcon.Warning); return; }
            if (!LoadWimlib()) return;

            string windows = Directory.Exists(tgt.Letter + ":\\Windows") ? "\r\n\r\n" + L.F("bk.confirm_apply_windows", tgt.Letter) : "";
            windows += "\r\n\r\n" + (sys != null
                ? L.F("bk.confirm_boot", FirmwareText(fw) + ", " + bootLang, ((PartItem)cboSysPart.SelectedItem).Text)
                : L.T("bk.confirm_no_boot"));
            if (!Ui.YesNo(L.F("bk.confirm_apply", tgt.Letter, tgt.Text, img.Text, wim, windows), MessageBoxIcon.Warning)) return;
            if (!ConfirmTyped(L.F("bk.confirm_type", tgt.Letter), tgt.Letter.ToString())) return;

            Dictionary<string, string> job = new Dictionary<string, string>();
            job["mode"] = "apply";
            job["wim"] = wim;
            job["index"] = img.Image.Index.ToString();
            job["target"] = tgt.Letter.ToString();
            job["label"] = WimLib.CleanLabel(txtLabel.Text);
            if (sys != null)
            {
                job["boot"] = fw;
                job["boot_lang"] = bootLang;
                job["sys_disk"] = sys.Disk.ToString();
                job["sys_part"] = sys.Number.ToString();
                job["sys_offset"] = sys.Offset.ToString();
            }
            jobBoot = sys != null ? fw : "";
            jobWim = wim;
            jobImage = img.Image.Index;
            jobTarget = tgt.Letter.ToString();
            Log(L.F("bk.log_start_apply", img.Image.Index, wim, tgt.Letter));
            StartHelper(job);
        }

        // folder target: no format; the folder has to be empty (or new)
        void StartApplyToFolder(string wim, Item img, string folder)
        {
            string why = WimLib.FolderTargetProblem(folder, wim, rootDir);
            if (why != null) { Ui.Info(L.F(why, folder), MessageBoxIcon.Warning); return; }
            long free = FreeSpace(folder);
            if (img.Image.TotalBytes > 0 && free >= 0 && free < img.Image.TotalBytes)
            {
                Ui.Info(L.F("bk.err_folder_too_small", Gb(img.Image.TotalBytes), folder, Gb(free)), MessageBoxIcon.Warning);
                return;
            }
            if (!LoadWimlib()) return;
            if (!Ui.YesNo(L.F("bk.confirm_apply_folder", folder, img.Text, wim), MessageBoxIcon.Question)) return;

            Dictionary<string, string> job = new Dictionary<string, string>();
            job["mode"] = "apply";
            job["wim"] = wim;
            job["index"] = img.Image.Index.ToString();
            job["folder"] = folder;
            jobBoot = "";
            jobWim = wim;
            jobImage = img.Image.Index;
            jobTarget = folder;
            Log(L.F("bk.log_start_apply_folder", img.Image.Index, wim, folder));
            StartHelper(job);
        }

        static string FirmwareText(string fw)
        {
            return fw == "ALL" ? "UEFI + BIOS" : fw == "BIOS" ? "BIOS" : "UEFI";
        }

        // the restore job writes into a folder (jobTarget is a path, not a letter)
        bool JobToFolder { get { return jobTarget != null && jobTarget.Length > 1; } }

        // second confirmation: the drive letter (or the disk number) has to be typed
        bool ConfirmTyped(string text, string expected, string titleKey = "bk.confirm_type_title")
        {
            using (Form f = new Form())
            {
                f.Text = L.T(titleKey);
                f.Font = Ui.FontUI;
                f.FormBorderStyle = FormBorderStyle.FixedDialog;
                f.StartPosition = FormStartPosition.CenterParent;
                f.MinimizeBox = false;
                f.MaximizeBox = false;
                f.ShowInTaskbar = false;
                f.ClientSize = new Size(Ui.S(420), Ui.S(140));
                Label l = new Label();
                l.Text = text;
                l.SetBounds(Ui.S(14), Ui.S(12), Ui.S(392), Ui.S(44));
                TextBox t = new TextBox();
                t.CharacterCasing = CharacterCasing.Upper;
                t.MaxLength = Math.Max(1, expected.Length);
                t.SetBounds(Ui.S(14), Ui.S(60), Ui.S(60), Ui.S(24));
                Button ok = new Button();
                ok.Text = "OK";
                ok.DialogResult = DialogResult.OK;
                ok.Enabled = false;
                ok.SetBounds(Ui.S(226), Ui.S(100), Ui.S(86), Ui.S(28));
                Button cancel = new Button();
                cancel.Text = L.T("bk.btn_cancel");
                cancel.DialogResult = DialogResult.Cancel;
                cancel.SetBounds(Ui.S(320), Ui.S(100), Ui.S(86), Ui.S(28));
                t.TextChanged += delegate { ok.Enabled = string.Equals(t.Text.Trim(), expected, StringComparison.OrdinalIgnoreCase); };
                f.Controls.AddRange(new Control[] { l, t, ok, cancel });
                f.AcceptButton = ok;
                f.CancelButton = cancel;
                Theme.Apply(f);
                return f.ShowDialog(this) == DialogResult.OK && ok.Enabled;
            }
        }

        // ---------- elevated helper ----------

        void StartHelper(Dictionary<string, string> job)
        {
            job["tools"] = ToolDir;
            job["root"] = rootDir;
            jobMode = job["mode"];
            workDir = Path.Combine(Path.Combine(Path.GetTempPath(), "DVT_wim_job"), DateTime.Now.ToString("yyyyMMdd-HHmmss"));
            try
            {
                Directory.CreateDirectory(workDir);
                StringBuilder sb = new StringBuilder();
                foreach (KeyValuePair<string, string> kv in job) sb.AppendLine(kv.Key + " = " + kv.Value);
                File.WriteAllText(Path.Combine(workDir, "job.ini"), sb.ToString(), new UTF8Encoding(false));
            }
            catch (Exception ex) { Ui.Info(ex.Message, MessageBoxIcon.Error); return; }
            helperStatusFile = Path.Combine(workDir, "status.txt");
            Log(L.F("bk.log_work", Ui.ShortPath(workDir)));

            // the C++ helper (cppuild.bat) does the job when it is next to this exe
            string helperExe = Application.ExecutablePath;
            string native = Path.Combine(rootDir, "7z_wimlib_native.exe");
            if (File.Exists(native))
            {
                helperExe = native;
                Log(L.F("bk.log_native_helper", Path.GetFileName(native)));
            }
            ProcessStartInfo psi = new ProcessStartInfo(helperExe, "--wim-helper \"" + workDir + "\"");
            psi.UseShellExecute = true;
            // backup / restore / disk need administrator rights; export, split and join do not
            bool elevate = jobMode == "capture" || jobMode == "apply" || jobMode == "disk";
            if (elevate) psi.Verb = "runas";
            psi.WorkingDirectory = workDir;
            try { helper = Process.Start(psi); }
            catch (Win32Exception ex)
            {
                helper = null;
                Log(ex.NativeErrorCode == 1223 ? L.T("adk.log_uac_cancelled") : ex.Message);
                lblProgress.Text = L.T("bk.state_cancelled");
                return;
            }
            if (helper == null) return;
            helperLinesShown = 0;
            helperStarted = DateTime.Now;
            phaseText = L.T(elevate ? "bk.phase_start" : "ex.phase_start");
            SetBusy(true);
            progress.Style = ProgressBarStyle.Marquee;
            ShowProgress();
            timer.Start();
        }

        // stops the job at the next progress message of wimlib
        public void CancelJob()
        {
            if (!Busy) return;
            try { File.WriteAllText(Path.Combine(workDir, "cancel"), "cancel", Encoding.ASCII); } catch (Exception) { }
            btnCancel.Enabled = false;
            Log(L.T("bk.log_cancelling"));
        }

        void ShowProgress()
        {
            TimeSpan t = DateTime.Now - helperStarted;
            lblProgress.Text = L.F("bk.state_running", phaseText, (int)t.TotalMinutes, t.Seconds);
        }

        // "P <percent> <phase> <numbers>" of the helper -> progress bar and text
        void OnProgressLine(string line)
        {
            string[] p = line.Split(' ');
            if (p.Length < 3) return;
            int pct;
            if (!int.TryParse(p[1], out pct)) return;
            long a = p.Length > 3 ? Number(p[3]) : 0, b = p.Length > 4 ? Number(p[4]) : 0, c = p.Length > 5 ? Number(p[5]) : 0, d = p.Length > 6 ? Number(p[6]) : 0;
            switch (p[2])
            {
                case "scan": phaseText = L.F("bk.phase_scan", a.ToString("N0"), b.ToString("N0"), Gb(c)); break;
                case "write": phaseText = L.F("bk.phase_write", Math.Max(0, pct), Gb(a), Gb(b)); break;
                case "apply": phaseText = L.F("bk.phase_apply", Math.Max(0, pct), Gb(a), Gb(b)); break;
                case "format": phaseText = L.T("bk.phase_format"); break;
                case "boot": phaseText = L.T("bk.phase_boot"); break;
                case "snapshot": phaseText = L.T("bk.phase_snapshot"); break;
                case "disk": phaseText = L.T("dk.phase"); break;
                case "structure": phaseText = L.T("bk.phase_structure"); break;
                case "metadata": phaseText = L.T("bk.phase_metadata"); break;
                case "export": phaseText = L.T("ex.phase_export"); break;
                case "split": phaseText = c > 0 ? L.F("sj.phase_split", Math.Max(0, pct), Gb(a), Gb(b), c, d) : L.T("sj.phase_split_start"); break;
                case "join": phaseText = L.T("sj.phase_join"); break;
                case "vopen": phaseText = L.T("vf.phase_open"); break;
                case "integrity": phaseText = L.F("vf.phase_integrity", Math.Max(0, pct), Gb(a), Gb(b)); break;
                case "vimage": phaseText = L.F("vf.phase_image", a, b); break;
                case "verify": phaseText = b > 0 ? L.F("vf.phase_verify", Math.Max(0, pct), Gb(a), Gb(b)) : L.T("vf.phase_open"); break;
            }
            if (pct >= 0)
            {
                progress.Style = ProgressBarStyle.Blocks;
                progress.Value = Math.Min(100, pct);
            }
            else progress.Style = ProgressBarStyle.Marquee;
        }

        static long Number(string s)
        {
            long v;
            return long.TryParse(s, out v) ? v : 0;
        }

        string ReadHelperLines()
        {
            string exit = null;
            try
            {
                if (helperStatusFile == null || !File.Exists(helperStatusFile)) return null;
                string[] lines;
                using (FileStream fs = new FileStream(helperStatusFile, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
                using (StreamReader r = new StreamReader(fs))
                    lines = r.ReadToEnd().Replace("\r\n", "\n").Split(new[] { '\n' }, StringSplitOptions.RemoveEmptyEntries);
                for (; helperLinesShown < lines.Length; helperLinesShown++)
                {
                    string l = lines[helperLinesShown];
                    if (l.StartsWith("P ")) OnProgressLine(l);
                    else if (l.StartsWith("LETTER ") && l.Length > 7) jobNewLetter = char.ToUpperInvariant(l[7]);
                    else
                    {
                        if (l.StartsWith("EXIT ")) exit = l.Substring(5);
                        Log(l);
                    }
                }
            }
            catch (Exception) { }
            return exit;
        }

        void PollHelper()
        {
            if (helper == null) { timer.Stop(); return; }
            string exit = ReadHelperLines();
            bool exited;
            try { exited = helper.HasExited; } catch (Exception) { exited = true; }
            if (!exited) { ShowProgress(); return; }

            timer.Stop();
            int code = -1;
            try { code = helper.ExitCode; } catch (Exception) { }
            helper.Dispose();
            helper = null;
            string last = ReadHelperLines();
            if (last != null) exit = last;
            SetBusy(false);
            string elapsed = L.F("bk.elapsed", Duration(DateTime.Now - helperStarted));
            Log(elapsed);

            if (code == 0)
            {
                DeleteWorkDir();
                lblProgress.Text = L.T("bk.state_done") + "  (" + elapsed + ")";
                if (jobMode == "disk") { Ui.Status(lblProgress.Text); DiskDone(elapsed); return; }
                string msg;
                if (jobMode == "capture") msg = CaptureDone(elapsed);
                else if (jobMode == "export") msg = ExportDone(elapsed);
                else if (jobMode == "split") msg = SplitDone(elapsed);
                else if (jobMode == "join") msg = JoinDone(elapsed);
                else if (jobMode == "verify") msg = L.F("vf.done", jobWim, L.T(jobIntegrity ? "vf.with_table" : "vf.no_table")) + "\r\n\r\n" + elapsed;
                else
                {
                    msg = L.F(JobToFolder ? "bk.apply_done_folder" : jobBoot.Length > 0 ? "bk.apply_done_boot" : "bk.apply_done",
                        jobImage, jobTarget, FirmwareText(jobBoot)) + "\r\n\r\n" + elapsed;
                    FillTargets();
                }
                Ui.Status(lblProgress.Text);
                AfterDone(msg);
                return;
            }
            else if (code == WimLib.ErrAborted)
            {
                lblProgress.Text = L.T("bk.state_cancelled");
                string more = jobMode == "split" ? "\r\n\r\n" + L.T("sj.cancelled_split")
                    : jobMode != "apply" ? "" : "\r\n\r\n" + L.F(JobToFolder ? "bk.cancelled_apply_folder" : "bk.cancelled_apply", jobTarget);
                Ui.Info(L.T("bk.cancelled") + more, MessageBoxIcon.Warning);
            }
            else
            {
                lblProgress.Text = L.T("bk.state_failed");
                // a verification that found damage (or a backup whose verification did)
                string head = jobMode == "verify" || (jobMode == "capture" && exit != null && exit.Contains("verification failed"))
                    ? L.F("vf.failed", jobWim) + "\r\n\r\n" : "";
                Ui.Info(head + L.F("bk.failed", code, exit ?? "-", Ui.ShortPath(workDir)), MessageBoxIcon.Error);
            }
            if (jobMode == "disk") { LoadDisks(); FillTargets(); }
            Ui.Status(lblProgress.Text);
        }

        // the new image; a file in the image folder of the seeder (DL_NAME\WIM_NAME)
        // becomes the image folder of the seeder page
        // after a successful job: the work folder (and DVT_wim_job when it is empty)
        void DeleteWorkDir()
        {
            try
            {
                if (workDir == null || !Directory.Exists(workDir)) return;
                Directory.Delete(workDir, true);
                Log(L.F("bk.log_work_deleted", Ui.ShortPath(workDir)));
                string parent = Path.GetDirectoryName(workDir);
                if (Directory.GetFileSystemEntries(parent).Length == 0) Directory.Delete(parent);
            }
            catch (Exception ex) { Log(L.F("bk.log_work_not_deleted", Ui.ShortPath(workDir), ex.Message)); }
        }

        // "1시간 2분 3초" / "1 h 2 min 3 s"
        static string Duration(TimeSpan t)
        {
            int h = (int)t.TotalHours;
            if (h > 0) return L.F("bk.duration_h", h, t.Minutes, t.Seconds);
            if (t.Minutes > 0) return L.F("bk.duration_m", t.Minutes, t.Seconds);
            return L.F("bk.duration_s", Math.Max(0, t.Seconds));
        }

        // ---------- after a successful job: nothing / shut down / restart ----------

        const int AfterSeconds = 60;

        void AfterDone(string msg)
        {
            int action = cboAfter.SelectedIndex;
            if (action != 1 && action != 2) { Ui.Info(msg); return; }
            string what = L.T(action == 1 ? "bk.after_shutdown" : "bk.after_restart");
            Log(L.F("bk.log_after", what, AfterSeconds));
            if (!Countdown(msg, action == 1 ? "bk.after_countdown_shutdown" : "bk.after_countdown_restart"))
            {
                Log(L.F("bk.log_after_cancelled", what));
                return;
            }
            // no /f: programs with unsaved work may still ask before Windows goes down
            string args = (action == 1 ? "/s" : "/r") + " /t 0 /d p:0:0";
            try
            {
                ProcessStartInfo psi = new ProcessStartInfo(Path.Combine(Environment.SystemDirectory, "shutdown.exe"), args);
                psi.UseShellExecute = false;
                psi.CreateNoWindow = true;
                Process.Start(psi).Dispose();
                Log("shutdown " + args);
            }
            catch (Exception ex)
            {
                Log(ex.Message);
                Ui.Info(L.F("bk.after_failed", what, ex.Message), MessageBoxIcon.Error);
            }
        }

        // the done message with a countdown; true = go on (time up or [Now]), false = cancelled
        bool Countdown(string msg, string countdownKey)
        {
            using (Form f = new Form())
            using (Timer t = new Timer())
            {
                f.Text = L.T("bk.after_title");
                f.Font = Ui.FontUI;
                f.FormBorderStyle = FormBorderStyle.FixedDialog;
                f.StartPosition = FormStartPosition.CenterParent;
                f.MinimizeBox = false;
                f.MaximizeBox = false;
                f.ShowInTaskbar = true;
                f.TopMost = true;
                f.ClientSize = new Size(Ui.S(520), Ui.S(260));
                Label l = new Label();
                l.Text = msg;
                l.SetBounds(Ui.S(14), Ui.S(12), Ui.S(492), Ui.S(150));
                Label c = new Label();
                c.Font = Ui.FontBold;
                c.ForeColor = Ui.ColorDanger;
                c.SetBounds(Ui.S(14), Ui.S(170), Ui.S(492), Ui.S(40));
                Button now = new Button();
                now.Text = L.T("bk.after_now");
                now.DialogResult = DialogResult.OK;
                now.SetBounds(Ui.S(300), Ui.S(220), Ui.S(100), Ui.S(28));
                Button cancel = new Button();
                cancel.Text = L.T("bk.btn_cancel");
                cancel.DialogResult = DialogResult.Cancel;
                cancel.SetBounds(Ui.S(406), Ui.S(220), Ui.S(100), Ui.S(28));
                f.Controls.AddRange(new Control[] { l, c, now, cancel });
                f.AcceptButton = cancel;          // Enter must not shut down
                f.CancelButton = cancel;

                DateTime end = DateTime.Now.AddSeconds(AfterSeconds);
                Action show = delegate
                {
                    int left = Math.Max(0, (int)Math.Ceiling((end - DateTime.Now).TotalSeconds));
                    c.Text = L.F(countdownKey, left);
                    if (left == 0) { t.Stop(); f.DialogResult = DialogResult.OK; }
                };
                t.Interval = 250;
                t.Tick += delegate { show(); };
                show();
                t.Start();
                f.Shown += delegate { cancel.Focus(); };
                Theme.Apply(f);
                return f.ShowDialog(this) == DialogResult.OK;
            }
        }

        string CaptureDone(string elapsed)
        {
            string msg = L.F("bk.capture_done", jobWim, jobImage) + (jobVerify ? "\r\n" + L.T("vf.capture_verified") : "") + "\r\n\r\n" + elapsed;
            // the folder of the new WIM file is the default folder next time
            try { setImageFolder(Path.GetDirectoryName(Path.GetFullPath(jobWim))); }
            catch (Exception) { }
            if (string.Equals(txtRWim.Text.Trim(), jobWim, StringComparison.OrdinalIgnoreCase) || txtRWim.Text.Trim().Length == 0)
            {
                txtRWim.Text = jobWim;
                ReadImages(false);
            }
            if (string.Equals(txtXSrc.Text.Trim(), jobWim, StringComparison.OrdinalIgnoreCase)) ReadExportImages(false);
            if (string.Equals(txtIWim.Text.Trim(), jobWim, StringComparison.OrdinalIgnoreCase)) ReadInfo(false);
            return msg;
        }
    }
}
