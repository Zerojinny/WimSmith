// ============================================================
//  7z_wimlib - BackupPanel.Disk.cs
//  The disk tab: clears a whole disk, initializes it as GPT or MBR and
//  creates the partitions of a layout:
//    Windows, GPT: EFI (FAT32) [+ MSR] + Windows (NTFS) [+ recovery]
//    Windows, MBR: system reserved (NTFS, active) + Windows [+ recovery]
//    Windows + data: the same, then a data partition; Windows gets a
//                  percentage of the free space, the data partition the rest
//    data        : one partition (NTFS / exFAT / FAT32) on the whole disk
//  The job runs in the elevated helper (mode = disk, DiskJob.cs). After a
//  Windows layout the restore tab gets the new partition as its target;
//  its boot type and system partition then follow from the disk.
// ============================================================
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Text;
using System.Windows.Forms;

namespace Dvt
{
    public partial class BackupPanel
    {
        ComboBox cboDisk, cboDataFs;
        Button btnDiskRefresh, btnDiskGo;
        RadioButton rbGpt, rbMbr, rbLayWin, rbLayWinData, rbLayData;
        NumericUpDown numSys, numRec, numWinPct;
        CheckBox chkRec, chkMsr;
        Label lblSysKind, lblPlan;
        TextBox txtDiskLabel, txtDataLabel;
        TabPage tabDisk;
        List<DiskInfo> disks;                 // null = not read (yet)
        bool disksLoading;
        int jobDisk = -1;
        string diskSettingsSaved;             // null = not loaded yet (nothing is saved before)
        DiskPlan jobPlan;
        char jobNewLetter;

        class DiskItem
        {
            public DiskInfo Disk;
            public string Text = "", Problem;     // Problem: key of the reason it cannot be used
            public override string ToString() { return Text; }
        }

        Control BuildDiskTab()
        {
            TableLayoutPanel g = Grid(9);
            cboDisk = Combo(560);
            btnDiskRefresh = Ui.NewButton(L.T("adk.btn_refresh"), 90);
            AddRow(g, 0, L.T("dk.row_disk"), cboDisk, btnDiskRefresh);
            rbGpt = Radio(L.T("dk.style_gpt"), true);
            rbMbr = Radio(L.T("dk.style_mbr"), false);
            AddRow(g, 1, L.T("dk.row_style"), rbGpt, rbMbr);
            rbLayWin = Radio(L.T("dk.layout_windows"), true);
            rbLayWinData = Radio(L.T("dk.layout_windows_data"), false);
            rbLayData = Radio(L.T("dk.layout_data"), false);
            AddRow(g, 2, L.T("dk.row_layout"), rbLayWin, rbLayWinData, rbLayData);
            lblSysKind = Ui.NewFlowLabel("");
            lblSysKind.Margin = new Padding(0, Ui.S(8), Ui.S(8), 0);
            lblSysKind.MinimumSize = new Size(Ui.S(200), 0);
            numSys = SizeBox(50, 4096, 260);
            Label lblSysMb = Ui.NewFlowLabel("MB");
            lblSysMb.Margin = new Padding(0, Ui.S(8), Ui.S(16), 0);
            chkMsr = Check(L.F("dk.msr_make", DiskPlan.MsrMb));
            chkMsr.Checked = true;
            AddRow(g, 3, L.T("dk.row_sys"), lblSysKind, numSys, lblSysMb, chkMsr);
            chkRec = Check(L.T("dk.recovery_make"));
            numRec = SizeBox(300, 65536, 1024);
            Label lblRecMb = Ui.NewFlowLabel("MB");
            lblRecMb.Margin = new Padding(0, Ui.S(8), Ui.S(8), 0);
            AddRow(g, 4, L.T("dk.row_recovery"), chkRec, numRec, lblRecMb);
            txtDiskLabel = NewText(200);
            txtDiskLabel.Text = "Windows";
            AddRow(g, 5, L.T("dk.row_win_label"), txtDiskLabel);
            // data partition: share of Windows (the data partition gets the rest), file system, label
            Label lblPct = Ui.NewFlowLabel(L.T("dk.win_pct"));
            lblPct.Margin = new Padding(0, Ui.S(8), Ui.S(4), 0);
            numWinPct = SizeBox(5, 95, 50);
            numWinPct.Increment = 5;
            numWinPct.Width = Ui.S(56);
            Label lblPctSign = Ui.NewFlowLabel("%");
            lblPctSign.Margin = new Padding(0, Ui.S(8), Ui.S(16), 0);
            cboDataFs = Combo(80);
            cboDataFs.Items.AddRange(new object[] { "NTFS", "exFAT", "FAT32" });
            cboDataFs.SelectedIndex = 0;
            Label lblLbl = Ui.NewFlowLabel(L.T("dk.label"));
            lblLbl.Margin = new Padding(Ui.S(8), Ui.S(8), Ui.S(4), 0);
            txtDataLabel = NewText(140);
            txtDataLabel.Text = "Data";
            AddRow(g, 6, L.T("dk.row_data"), lblPct, numWinPct, lblPctSign, cboDataFs, lblLbl, txtDataLabel);
            lblPlan = Ui.NewFlowLabel("");
            lblPlan.Margin = new Padding(0, Ui.S(8), 0, 0);
            g.RowStyles[7] = new RowStyle(SizeType.Absolute, Ui.S(112));
            AddRow(g, 7, L.T("dk.row_plan"), lblPlan);
            btnDiskGo = Ui.NewButton(L.T("dk.btn_go"), 220);
            Ui.Primary(btnDiskGo, Ui.ColorDanger);
            Label hint = Ui.NewFlowLabel(L.T("dk.hint"));
            hint.ForeColor = Color.DimGray;
            hint.MaximumSize = new Size(Ui.S(520), 0);
            hint.Margin = new Padding(Ui.S(8), Ui.S(4), 0, 0);
            AddRow(g, 8, "", btnDiskGo, hint);

            LoadDiskSettings();
            btnDiskRefresh.Click += delegate { LoadDisks(); };
            btnDiskGo.Click += delegate { StartDisk(); };
            rbGpt.CheckedChanged += delegate { if (rbGpt.Checked || rbMbr.Checked) { numSys.Value = DiskPlan.DefaultSysMb(rbGpt.Checked); UpdateDiskUi(); } };
            foreach (RadioButton rb in new[] { rbLayWin, rbLayWinData, rbLayData })
                rb.CheckedChanged += delegate(object o, EventArgs e) { if (((RadioButton)o).Checked) UpdateDiskUi(); };
            foreach (NumericUpDown n in new[] { numSys, numRec, numWinPct }) n.ValueChanged += delegate { UpdateDiskUi(); };
            foreach (CheckBox c in new[] { chkRec, chkMsr }) c.CheckedChanged += delegate { UpdateDiskUi(); };
            cboDataFs.SelectedIndexChanged += delegate { UpdateDiskUi(); };
            cboDisk.SelectedIndexChanged += delegate { UpdateDiskUi(); };
            foreach (TextBox t in new[] { txtDiskLabel, txtDataLabel }) t.TextChanged += delegate { UpdateDiskUi(); };
            // the disks are read the first time the tab is shown
            tabs.SelectedIndexChanged += delegate { if (tabs.SelectedTab == tabDisk && disks == null && !disksLoading) LoadDisks(); };
            diskSettingsSaved = DiskSettingsText(DiskSettings());
            UpdateDiskUi();
            return g;
        }

        // ---------- settings (7z_wimlib.ini, disk.*) ----------
        // Every choice of the tab is kept, except the disk: a disk to clear
        // is always chosen by hand.

        Dictionary<string, string> DiskSettings()
        {
            DiskPlan p = CurrentPlan();
            Dictionary<string, string> d = new Dictionary<string, string>();
            d["disk.style"] = p.Gpt ? "GPT" : "MBR";
            d["disk.layout"] = p.Layout;
            d["disk.sys_mb"] = p.SysMb.ToString();
            d["disk.msr"] = p.Msr ? "1" : "0";
            d["disk.recovery"] = p.Recovery ? "1" : "0";
            d["disk.recovery_mb"] = p.RecoveryMb.ToString();
            d["disk.win_pct"] = p.WindowsPercent.ToString();
            d["disk.win_label"] = txtDiskLabel.Text.Trim();
            d["disk.data_fs"] = p.DataFs;
            d["disk.data_label"] = txtDataLabel.Text.Trim();
            return d;
        }

        static string DiskSettingsText(Dictionary<string, string> d)
        {
            StringBuilder sb = new StringBuilder();
            foreach (KeyValuePair<string, string> kv in d) sb.Append(kv.Key).Append('=').Append(kv.Value).Append('\n');
            return sb.ToString();
        }

        static void SetNumber(NumericUpDown n, string key)
        {
            int v;
            if (int.TryParse(Program.GetSetting(key, ""), out v)) n.Value = Math.Max(n.Minimum, Math.Min(n.Maximum, v));
        }

        // before the events are connected: nothing is saved while loading
        void LoadDiskSettings()
        {
            bool mbr = Program.GetSetting("disk.style", "GPT").Equals("MBR", StringComparison.OrdinalIgnoreCase);
            rbMbr.Checked = mbr;
            rbGpt.Checked = !mbr;
            numSys.Value = DiskPlan.DefaultSysMb(!mbr);
            string layout = Program.GetSetting("disk.layout", "windows");
            rbLayData.Checked = layout == "data";
            rbLayWinData.Checked = layout == "windows_data";
            rbLayWin.Checked = layout != "data" && layout != "windows_data";
            SetNumber(numSys, "disk.sys_mb");
            chkMsr.Checked = Program.GetSetting("disk.msr", "1") != "0";
            chkRec.Checked = Program.GetSetting("disk.recovery", "0") == "1";
            SetNumber(numRec, "disk.recovery_mb");
            SetNumber(numWinPct, "disk.win_pct");
            txtDiskLabel.Text = Program.GetSetting("disk.win_label", "Windows");
            int fs = cboDataFs.Items.IndexOf(Program.GetSetting("disk.data_fs", "NTFS"));
            cboDataFs.SelectedIndex = Math.Max(0, fs);
            txtDataLabel.Text = Program.GetSetting("disk.data_label", "Data");
        }

        // after a change: written only when a value differs from the file
        void SaveDiskSettings()
        {
            if (diskSettingsSaved == null) return;
            Dictionary<string, string> d = DiskSettings();
            string text = DiskSettingsText(d);
            if (text == diskSettingsSaved) return;
            diskSettingsSaved = text;
            Program.SetSettings(d);
        }

        static NumericUpDown SizeBox(int min, int max, int value)
        {
            NumericUpDown n = new NumericUpDown();
            n.Minimum = min;
            n.Maximum = max;
            n.Value = value;
            n.Increment = 10;
            n.ThousandsSeparator = true;
            n.Width = Ui.S(80);
            n.Margin = new Padding(0, Ui.S(5), Ui.S(4), 0);
            return n;
        }

        // Get-Disk takes a few seconds: read in the background
        void LoadDisks()
        {
            if (disksLoading) return;
            disksLoading = true;
            btnDiskRefresh.Enabled = false;
            cboDisk.Items.Clear();
            cboDisk.Items.Add(new DiskItem { Text = L.T("dk.reading") });
            cboDisk.SelectedIndex = 0;
            System.Threading.ThreadPool.QueueUserWorkItem(delegate
            {
                string err;
                List<DiskInfo> list = WimLib.QueryDisks(out err);
                try
                {
                    BeginInvoke((MethodInvoker)delegate
                    {
                        disksLoading = false;
                        disks = list;
                        if (list == null) Log(L.F("dk.read_failed", err));
                        FillDisks();
                        SetDiskBusy(Busy);
                    });
                }
                catch (Exception) { }     // the window is closed
            });
        }

        void FillDisks()
        {
            DiskItem old = cboDisk.SelectedItem as DiskItem;
            int keep = old != null && old.Disk != null ? old.Disk.Number : -1;
            cboDisk.Items.Clear();
            int pick = -1;
            if (disks != null)
                foreach (DiskInfo d in disks)
                {
                    DiskItem it = new DiskItem();
                    it.Disk = d;
                    it.Problem = WimLib.DiskProblem(d, rootDir);
                    it.Text = L.F("dk.disk_line", d.Number, d.Name.Length > 0 ? d.Name : "-", Gb(d.Size),
                        d.Style == "RAW" ? L.T("dk.style_raw") : d.Style, d.Bus,
                        d.Letters.Length > 0 ? d.Letters.Replace(",", ": ") + ":" : L.T("dk.no_volumes"));
                    if (it.Problem != null) it.Text += "  " + L.F("dk.not_usable", L.T(it.Problem + "_short"));
                    cboDisk.Items.Add(it);
                    if (d.Number == keep) pick = cboDisk.Items.Count - 1;
                }
            if (cboDisk.Items.Count == 0) cboDisk.Items.Add(new DiskItem { Text = L.T(disks == null ? "dk.unknown" : "dk.none") });
            else if (pick < 0)
            {
                // nothing is chosen for the user: a disk to clear is always picked by hand
                cboDisk.Items.Insert(0, new DiskItem { Text = L.T("dk.choose") });
                pick = 0;
            }
            cboDisk.SelectedIndex = Math.Max(0, pick);
            UpdateDiskUi();
        }

        void SetDiskBusy(bool busy)
        {
            if (cboDisk == null) return;
            foreach (Control c in new Control[] { cboDisk, rbGpt, rbMbr, rbLayWin, rbLayWinData, rbLayData, btnDiskGo })
                c.Enabled = !busy;
            btnDiskRefresh.Enabled = !busy && !disksLoading;
            if (busy)
                foreach (Control c in new Control[] { numSys, chkMsr, chkRec, numRec, txtDiskLabel, numWinPct, cboDataFs, txtDataLabel })
                    c.Enabled = false;
            else UpdateDiskUi();
        }

        DiskPlan CurrentPlan()
        {
            DiskPlan p = new DiskPlan();
            p.Gpt = rbGpt.Checked;
            p.Windows = !rbLayData.Checked;
            p.Data = !rbLayWin.Checked;
            p.SysMb = (int)numSys.Value;
            p.Msr = chkMsr.Checked;
            p.Recovery = chkRec.Checked;
            p.RecoveryMb = (int)numRec.Value;
            p.WindowsPercent = (int)numWinPct.Value;
            p.Label = txtDiskLabel.Text;
            p.DataFs = cboDataFs.SelectedItem as string ?? "NTFS";
            p.DataLabel = txtDataLabel.Text;
            return p;
        }

        DiskInfo SelectedDisk()
        {
            DiskItem it = cboDisk.SelectedItem as DiskItem;
            return it != null ? it.Disk : null;
        }

        static string MbText(long bytes)
        {
            return bytes >= 10L * 1073741824 ? Gb(bytes) : (bytes / 1048576).ToString("N0") + " MB";
        }

        // enables the rows of the layout and shows the partitions that will be created
        void UpdateDiskUi()
        {
            if (lblPlan == null) return;
            DiskPlan p = CurrentPlan();
            bool gpt = p.Gpt, idle = !Busy;
            lblSysKind.Text = L.T(gpt ? "dk.sys_esp" : "dk.sys_reserved");
            numSys.Enabled = idle && p.Windows;
            chkMsr.Enabled = idle && p.Windows && gpt;
            chkRec.Enabled = idle && p.Windows;
            numRec.Enabled = idle && p.Windows && chkRec.Checked;
            txtDiskLabel.Enabled = idle && p.Windows;
            numWinPct.Enabled = idle && p.Windows && p.Data;
            cboDataFs.Enabled = txtDataLabel.Enabled = idle && p.Data;

            DiskInfo d = SelectedDisk();
            string rest = L.T("dk.plan_rest");
            string winSize = d != null && p.WindowsBytes(d.Size) > 0 ? MbText(p.WindowsBytes(d.Size)) : p.Data ? p.WindowsPercent + "%" : rest;
            string dataSize = d != null && p.DataBytes(d.Size) > 0 ? MbText(p.DataBytes(d.Size)) : p.Windows ? (100 - p.WindowsPercent) + "%" : rest;
            StringBuilder sb = new StringBuilder();
            int n = 1;
            if (p.Windows)
            {
                if (gpt)
                {
                    sb.AppendLine(n++ + ". " + L.F("dk.plan_esp", p.SysMb));
                    if (p.HasMsr) sb.AppendLine(n++ + ". " + L.F("dk.plan_msr", DiskPlan.MsrMb));
                }
                else sb.AppendLine(n++ + ". " + L.F("dk.plan_reserved", p.SysMb));
                sb.AppendLine(n++ + ". " + L.F("dk.plan_windows", p.WindowsLabel(), winSize));
                if (p.Recovery) sb.AppendLine(n++ + ". " + L.F("dk.plan_recovery", p.RecoveryMb));
            }
            if (p.Data) sb.AppendLine(n++ + ". " + L.F("dk.plan_data", p.DataLabelClean(), dataSize, p.DataFs));
            string why = d != null ? WimLib.PlanProblem(p, d.Size) : null;
            // GPT without MSR: allowed, but Windows Setup and some tools expect it
            string note = p.Windows && gpt && !p.Msr && why == null ? "\r\n" + L.T("dk.no_msr_note") : "";
            lblPlan.Text = sb.ToString().TrimEnd() + note;
            lblPlan.ForeColor = why != null ? Theme.Danger : Theme.Text;
            if (why != null) lblPlan.Text += "\r\n" + PlanText(why, d, p);
            SaveDiskSettings();
        }

        // {0} disk size, {1} disk number, {2} Windows partition (or the only
        // data partition), {3} data partition
        string PlanText(string key, DiskInfo d, DiskPlan p)
        {
            long main = p.Windows ? p.WindowsBytes(d.Size) : p.DataBytes(d.Size);
            return L.F(key, Gb(d.Size), d.Number, MbText(Math.Max(0, main)), MbText(Math.Max(0, p.DataBytes(d.Size))));
        }

        void StartDisk()
        {
            if (Busy) return;
            DiskInfo d = SelectedDisk();
            if (d == null) { Ui.Info(L.T("dk.err_no_disk"), MessageBoxIcon.Warning); return; }
            string why = WimLib.DiskProblem(d, rootDir);
            if (why != null) { Ui.Info(L.F(why, d.Number), MessageBoxIcon.Warning); return; }
            DiskPlan p = CurrentPlan();
            why = WimLib.PlanProblem(p, d.Size);
            if (why != null) { Ui.Info(PlanText(why, d, p), MessageBoxIcon.Warning); return; }

            string current = d.Letters.Length > 0 ? L.F("dk.confirm_volumes", d.Letters.Replace(",", ": ") + ":") : L.T("dk.confirm_no_volumes");
            string style = p.Gpt ? "GPT" : "MBR";
            if (!Ui.YesNo(L.F("dk.confirm", d.Number, d.Name, Gb(d.Size), current, style, lblPlan.Text), MessageBoxIcon.Warning)) return;
            if (!ConfirmTyped(L.F("dk.confirm_type", d.Number), d.Number.ToString(), "dk.confirm_type_title")) return;

            Dictionary<string, string> job = new Dictionary<string, string>();
            job["mode"] = "disk";
            job["disk"] = d.Number.ToString();
            job["disk_size"] = d.Size.ToString();
            p.ToJob(job);
            jobDisk = d.Number;
            jobPlan = p;
            jobNewLetter = '\0';
            jobTarget = null;
            Log(L.F("dk.log_start", d.Number, d.Name, style, L.T(!p.Windows ? "dk.layout_data" : p.Data ? "dk.layout_windows_data" : "dk.layout_windows")));
            StartHelper(job);
        }

        // after a successful disk job; a Windows layout goes on to the restore tab
        void DiskDone(string elapsed)
        {
            LoadDisks();
            string style = jobPlan.Gpt ? "GPT" : "MBR";
            string letter = jobNewLetter != '\0' ? jobNewLetter + ":" : "-";
            if (!jobPlan.Windows || jobNewLetter == '\0')
            {
                FillTargets();
                Ui.Info(L.F("dk.done_data", jobDisk, style, letter) + "\r\n\r\n" + elapsed);
                return;
            }
            Ui.Info(L.F("dk.done_windows", jobDisk, style, letter, L.T(jobPlan.Gpt ? "dk.done_uefi" : "dk.done_bios"))
                + (jobPlan.Data ? "\r\n\r\n" + L.F("dk.done_with_data", jobPlan.DataLabelClean(), jobPlan.DataFs) : "")
                + "\r\n\r\n" + elapsed);
            // the restore tab: the new partition as target; FillTargets reads the
            // partitions again, then UEFI (EFI partition) or BIOS (active partition)
            // and the system partition are chosen for its disk
            txtLabel.Text = jobPlan.WindowsLabel();
            FillTargets();
            Select(cboTarget, jobNewLetter + ":\\");
            tabs.SelectedTab = tabApply;
        }
    }
}
