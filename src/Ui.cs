// ============================================================
//  7z_wimlib - Ui.cs (taken from the DVT GUI)
//  Shared helpers: DPI scaling, colors, message boxes, small
//  control factories.
// ============================================================
using System;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Windows.Forms;

namespace Dvt
{
    public static class Ui
    {
        public static MainForm Main;

        // A path inside the build folder without the build folder itself
        // (data\Media\config\...); other paths (USB drive) as they are
        public static string ShortPath(string path)
        {
            if (Main == null || string.IsNullOrEmpty(path) || string.IsNullOrEmpty(Main.Root)) return path;
            string root = Main.Root.TrimEnd('\\') + "\\";
            return path.StartsWith(root, StringComparison.OrdinalIgnoreCase) ? path.Substring(root.Length) : path;
        }
        public static float Scale = 1f;

        // fonts of the current language (L.FontFamily), set by InitFonts
        public static Font FontUI, FontBold, FontNav, FontHeader, FontHelpTitle;
        public static readonly Font FontMono = new Font("Consolas", 9f);

        public static void InitFonts(string family)
        {
            FontUI = new Font(family, 9f);
            if (!string.Equals(FontUI.Name, family, StringComparison.OrdinalIgnoreCase))
            {
                // font not installed: fall back to Segoe UI
                family = "Segoe UI";
                FontUI = new Font(family, 9f);
            }
            FontBold = new Font(family, 9f, FontStyle.Bold);
            FontNav = new Font(family, 11f, FontStyle.Bold);
            FontHeader = new Font(family, 13f, FontStyle.Bold);
            FontHelpTitle = new Font(family, 11f, FontStyle.Bold);
        }
        public static readonly Color ColorSection = Color.FromArgb(236, 240, 246);
        public static readonly Color ColorSectionBar = Color.FromArgb(120, 140, 170);
        public static readonly Color ColorSectionText = Color.FromArgb(40, 60, 95);
        public static readonly Color ColorChanged = Color.FromArgb(255, 244, 200);
        public static readonly Color ColorDanger = Color.FromArgb(176, 32, 32);

        // pixels at 96 DPI -> pixels at the current DPI
        public static int S(int px) { return (int)Math.Round(px * Scale); }

        public static void Status(string text)
        {
            if (Main != null) Main.SetStatus(text);
        }

        public static void Info(string text) { Info(text, MessageBoxIcon.Information); }

        public static void Info(string text, MessageBoxIcon icon)
        {
            MessageBox.Show(Main, text, "7z_wimlib", MessageBoxButtons.OK, icon);
        }

        public static bool YesNo(string text, MessageBoxIcon icon)
        {
            return MessageBox.Show(Main, text, "7z_wimlib", MessageBoxButtons.YesNo, icon) == DialogResult.Yes;
        }

        public static Button NewButton(string text, int width)
        {
            Button b = new Button();
            b.Text = text;
            b.Width = S(width);
            b.Height = S(28);
            b.Margin = new Padding(0, S(2), S(8), S(2));
            b.UseVisualStyleBackColor = true;
            return b;
        }

        // Label that fills its table cell
        public static Label NewLabel(string text)
        {
            Label l = new Label();
            l.Text = text;
            l.Dock = DockStyle.Fill;
            l.TextAlign = ContentAlignment.MiddleLeft;
            l.AutoEllipsis = true;
            return l;
        }

        public static Label NewBoldLabel(string text)
        {
            Label l = NewLabel(text);
            l.Font = FontBold;
            return l;
        }

        public static Label NewHint(string text)
        {
            Label l = NewLabel(text);
            l.ForeColor = SystemColors.GrayText;
            return l;
        }

        // Label sized to its text, for a FlowLayoutPanel next to buttons
        public static Label NewFlowLabel(string text)
        {
            Label l = new Label();
            l.Text = text;
            l.AutoSize = true;
            l.Margin = new Padding(S(6), S(8), 0, 0);
            return l;
        }

        // main action button of a page, filled in the page color
        // (dark theme: the accent color, or the danger color for ColorDanger)
        public static void Primary(Button b, Color accent)
        {
            b.Font = FontBold;
            Theme.SetFilled(b, accent.ToArgb() == ColorDanger.ToArgb());
        }

        public static FlowLayoutPanel NewFlow()
        {
            FlowLayoutPanel f = new FlowLayoutPanel();
            f.Dock = DockStyle.Fill;
            f.FlowDirection = FlowDirection.LeftToRight;
            f.WrapContents = false;
            f.Padding = new Padding(0);
            f.Margin = new Padding(0);
            return f;
        }

        // One column, rows of fixed height (96 DPI pixels); 0 = the rest
        public static TableLayoutPanel NewRows(params int[] heights)
        {
            TableLayoutPanel t = new TableLayoutPanel();
            t.Dock = DockStyle.Fill;
            t.ColumnCount = 1;
            t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100f));
            t.RowCount = heights.Length;
            foreach (int h in heights)
            {
                if (h > 0) t.RowStyles.Add(new RowStyle(SizeType.Absolute, S(h)));
                else t.RowStyles.Add(new RowStyle(SizeType.Percent, 100f));
            }
            t.Margin = new Padding(0);
            return t;
        }

        public static TabPage NewTabPage(string text, Control content, bool padded)
        {
            TabPage tp = new TabPage(text);
            tp.Font = FontUI;
            tp.UseVisualStyleBackColor = true;
            if (padded) tp.Padding = new Padding(S(8));
            content.Dock = DockStyle.Fill;
            tp.Controls.Add(content);
            return tp;
        }

        public static void OpenFolder(string path)
        {
            if (Directory.Exists(path))
                Process.Start("explorer.exe", "\"" + path + "\"");
            else
                Info(L.F("msg.folder_missing", path), MessageBoxIcon.Warning);
        }

        public static void OpenInNotepad(string path)
        {
            if (File.Exists(path))
                Process.Start("notepad.exe", "\"" + path + "\"");
            else
                Info(L.F("msg.file_missing", path), MessageBoxIcon.Warning);
        }
    }
}
