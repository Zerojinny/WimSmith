// ============================================================
//  7z_wimlib - MainForm.cs
//  The window: the backup / restore page (BackupPanel; its log has a
//  window of its own, LogWindow) and a status
//  bar with the language choice. Ui.Main points to it (Ui.Status,
//  Ui.Info, Ui.ShortPath use Root and SetStatus).
// ============================================================
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace Dvt
{
    public class MainForm : Form
    {
        public readonly string Root;          // folder of 7z_wimlib.exe
        public bool RestartRequested;         // another language was chosen
        readonly BackupPanel panel;
        readonly ComboBox cboLang;

        public MainForm(string root)
        {
            Root = root;
            Ui.Main = this;
            Libs.Migrate(root);       // libwim-15.dll of pe_tools\ -> lib\
            Libs.FetchMissingLicenses(root);   // license texts of wimlib / 7-Zip, when missing (background)
            Text = "7z_wimlib";
            Font = Ui.FontUI;
            StartPosition = FormStartPosition.CenterScreen;
            // the log has its own window (LogWindow): the tabs need about 360 px
            ClientSize = new Size(Ui.S(980), Ui.S(540));
            MinimumSize = new Size(Ui.S(820), Ui.S(520));
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch (Exception) { }

            panel = new BackupPanel(root, Paths,
                delegate { return Program.GetSetting("image_folder", ""); },
                delegate(string dir) { Program.SetSetting("image_folder", dir); });
            panel.Padding = new Padding(0, Ui.S(4), 0, 0);

            // the language: at the right end of the footer of the panel
            cboLang = new ComboBox();
            cboLang.DropDownStyle = ComboBoxStyle.DropDownList;
            cboLang.Width = Ui.S(100);
            foreach (KeyValuePair<string, string> kv in L.Available())
            {
                cboLang.Items.Add(new LangItem(kv.Key, kv.Value));
                if (string.Equals(kv.Key, L.Code, StringComparison.OrdinalIgnoreCase)) cboLang.SelectedIndex = cboLang.Items.Count - 1;
            }
            cboLang.SelectedIndexChanged += delegate { ChangeLanguage(); };
            panel.AddFooter(cboLang);

            panel.Dock = DockStyle.Fill;
            Controls.Add(panel);
            Theme.Apply(this);

            Shown += delegate { panel.RefreshStatus(); };
            FormClosing += OnClosing;
        }

        class LangItem
        {
            public readonly string Code, Name;
            public LangItem(string code, string name) { Code = code; Name = name; }
            public override string ToString() { return Name; }
        }

        public void SetStatus(string text) { panel.SetStatus(text); }

        // the folder of libwim-15.dll and 7-Zip: <exe folder>\lib
        Dictionary<string, string> Paths()
        {
            Dictionary<string, string> p = new Dictionary<string, string>();
            p["Lib"] = Libs.Dir(Root);
            return p;
        }

        void ChangeLanguage()
        {
            LangItem it = cboLang.SelectedItem as LangItem;
            if (it == null || string.Equals(it.Code, L.Code, StringComparison.OrdinalIgnoreCase)) return;
            if (panel.Busy || panel.BrowserBusy)
            {
                Ui.Info(L.T("z.lang_busy"), MessageBoxIcon.Warning);
                for (int n = 0; n < cboLang.Items.Count; n++)
                    if (string.Equals(((LangItem)cboLang.Items[n]).Code, L.Code, StringComparison.OrdinalIgnoreCase)) cboLang.SelectedIndex = n;
                return;
            }
            Program.SetSetting("language", it.Code);
            RestartRequested = true;
            Close();
        }

        // a running job: ask before closing (the elevated helper goes on alone)
        void OnClosing(object sender, FormClosingEventArgs e)
        {
            if (RestartRequested) return;
            if (panel.Busy && !Ui.YesNo(L.T("z.close_busy"), MessageBoxIcon.Warning)) { e.Cancel = true; return; }
            // 7-Zip of the browse tab: stopped with the window
            if (panel.BrowserBusy)
            {
                if (!Ui.YesNo(L.T("z.close_extract"), MessageBoxIcon.Warning)) { e.Cancel = true; return; }
                panel.CancelBrowser();
            }
        }
    }
}
