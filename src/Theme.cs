// ============================================================
//  7z_wimlib - Theme.cs
//  The dark look ("Dark Console"): colors, and Apply(), which walks a
//  window's controls and gives each kind its dark colors (also the ones
//  added later). Windows draws the title bar, the scroll bars and the
//  combo box lists dark when asked (DWM / SetWindowTheme, Windows 10
//  1809 and later; older versions keep them light).
//    DarkTabControl : flat tabs, the selected one underlined
//    FlatProgress   : thin progress bar (also "marquee")
//  The classic look is kept in backup\ui-classic.
// ============================================================
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace Dvt
{
    public static class Theme
    {
        public static readonly Color Back = Color.FromArgb(0x17, 0x19, 0x1C);       // window
        public static readonly Color Panel = Color.FromArgb(0x20, 0x23, 0x28);      // fields, boxes
        public static readonly Color Button = Color.FromArgb(0x2A, 0x2E, 0x34);
        public static readonly Color ButtonHover = Color.FromArgb(0x34, 0x39, 0x40);
        public static readonly Color Border = Color.FromArgb(0x35, 0x3A, 0x40);
        public static readonly Color Line = Color.FromArgb(0x2C, 0x30, 0x35);
        public static readonly Color Footer = Color.FromArgb(0x11, 0x13, 0x16);
        public static readonly Color Text = Color.FromArgb(0xE6, 0xE8, 0xEB);
        public static readonly Color Dim = Color.FromArgb(0xA0, 0xA6, 0xAE);       // 6:1 on Panel
        public static readonly Color Accent = Color.FromArgb(0x3D, 0xD6, 0xC6);
        public static readonly Color AccentText = Color.FromArgb(0x08, 0x20, 0x1E);
        public static readonly Color AccentSoft = Color.FromArgb(0x7F, 0xE3, 0xD8);
        public static readonly Color Danger = Color.FromArgb(0xF4, 0x70, 0x67);
        public static readonly Color DangerText = Color.FromArgb(0x1A, 0x08, 0x07);

        // main buttons: filled in the accent (or danger) color
        static readonly Dictionary<Button, bool> filled = new Dictionary<Button, bool>();   // true = danger
        static readonly HashSet<Control> hooked = new HashSet<Control>();

        public static void SetFilled(Button b, bool danger)
        {
            filled[b] = danger;
            StyleButton(b);
        }

        // the dark colors for c and all the controls in it (also those added later)
        public static void Apply(Control c)
        {
            ApplyOne(c);
            foreach (Control child in c.Controls) Apply(child);
            if (hooked.Add(c))
            {
                c.ControlAdded += delegate(object s, ControlEventArgs e) { Apply(e.Control); };
                c.Disposed += delegate { hooked.Remove(c); };
            }
        }

        static void ApplyOne(Control c)
        {
            if (c is Form)
            {
                c.BackColor = Back;
                c.ForeColor = Text;
                Native(c, delegate(IntPtr h) { DarkTitle(h); });
            }
            else if (c is TabPage)
            {
                ((TabPage)c).UseVisualStyleBackColor = false;
                c.BackColor = Back;
                c.ForeColor = Text;
            }
            else if (c is Button) StyleButton((Button)c);
            else if (c is TextBox)
            {
                TextBox t = (TextBox)c;
                t.BackColor = Panel;
                t.ForeColor = Text;
                t.BorderStyle = BorderStyle.FixedSingle;
                Native(c, delegate(IntPtr h) { SetWindowTheme(h, "DarkMode_Explorer", null); });
            }
            else if (c is ComboBox)
            {
                ComboBox b = (ComboBox)c;
                b.FlatStyle = FlatStyle.Standard;
                b.BackColor = Panel;
                b.ForeColor = Text;
                Native(c, delegate(IntPtr h) { SetWindowTheme(h, "DarkMode_CFD", null); });
            }
            else if (c is NumericUpDown)
            {
                NumericUpDown n = (NumericUpDown)c;
                n.BackColor = Panel;
                n.ForeColor = Text;
                n.BorderStyle = BorderStyle.FixedSingle;
                foreach (Control part in n.Controls)
                    Native(part, delegate(IntPtr h) { SetWindowTheme(h, "DarkMode_Explorer", null); });
            }
            else if (c is ListView)
            {
                ListView v = (ListView)c;
                v.BackColor = Panel;
                v.ForeColor = Text;
                v.BorderStyle = BorderStyle.FixedSingle;
                v.GridLines = false;
                if (v.View == View.Details && !v.OwnerDraw)
                {
                    v.OwnerDraw = true;
                    v.DrawColumnHeader += DrawHeader;
                    // the rows in DrawSubItem; a selected row in the accent tone (also without focus)
                    v.DrawSubItem += DrawCell;
                }
                Native(c, delegate(IntPtr h)
                {
                    SetWindowTheme(h, "DarkMode_Explorer", null);
                    IntPtr header = SendMessage(h, 0x101F, IntPtr.Zero, IntPtr.Zero);   // LVM_GETHEADER
                    if (header != IntPtr.Zero) SetWindowTheme(header, "DarkMode_ItemsView", null);
                });
            }
            else if (c is TreeView)
            {
                TreeView t = (TreeView)c;
                t.BackColor = Panel;
                t.ForeColor = Text;
                t.LineColor = Dim;
                t.BorderStyle = BorderStyle.FixedSingle;
                Native(c, delegate(IntPtr h) { SetWindowTheme(h, "DarkMode_Explorer", null); });
            }
            else if (c is SplitContainer)
            {
                c.BackColor = Line;               // the splitter
                ((SplitContainer)c).Panel1.BackColor = Back;
                ((SplitContainer)c).Panel2.BackColor = Back;
            }
            else if (c is GroupBox) c.ForeColor = Dim;
            else if (c is LinkLabel)
            {
                LinkLabel l = (LinkLabel)c;
                l.LinkColor = AccentSoft;
                l.ActiveLinkColor = Text;
                l.VisitedLinkColor = AccentSoft;
                l.DisabledLinkColor = Dim;
            }
            else if (c is Label || c is CheckBox || c is RadioButton) c.ForeColor = MapText(c.ForeColor);
        }

        public static readonly Color Selected = Color.FromArgb(0x1D, 0x3B, 0x39);

        static void DrawCell(object sender, DrawListViewSubItemEventArgs e)
        {
            if (e.Item == null || !e.Item.Selected) { e.DrawDefault = true; return; }
            ListView v = (ListView)sender;
            Rectangle r = e.Bounds;
            using (SolidBrush b = new SolidBrush(Selected)) e.Graphics.FillRectangle(b, r);
            int x = r.Left + Ui.S(4);
            if (e.ColumnIndex == 0 && v.SmallImageList != null && e.Item.ImageIndex >= 0 && e.Item.ImageIndex < v.SmallImageList.Images.Count)
            {
                Image img = v.SmallImageList.Images[e.Item.ImageIndex];
                e.Graphics.DrawImage(img, x, r.Top + (r.Height - img.Height) / 2, img.Width, img.Height);
                x += img.Width + Ui.S(2);
            }
            TextFormatFlags f = TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine | TextFormatFlags.NoPrefix;
            if (e.Header != null && e.Header.TextAlign == HorizontalAlignment.Right) f |= TextFormatFlags.Right;
            Rectangle t = new Rectangle(x, r.Top, Math.Max(0, r.Right - x - Ui.S(4)), r.Height);
            string text = e.SubItem != null ? e.SubItem.Text : e.Item.Text;
            TextRenderer.DrawText(e.Graphics, text, v.Font, t, Color.White, f);
        }

        static void DrawHeader(object sender, DrawListViewColumnHeaderEventArgs e)
        {
            Rectangle r = e.Bounds;
            using (SolidBrush b = new SolidBrush(Button)) e.Graphics.FillRectangle(b, r);
            using (Pen p = new Pen(Border))
            {
                e.Graphics.DrawLine(p, r.Right - 1, r.Top + Ui.S(4), r.Right - 1, r.Bottom - Ui.S(4));
                e.Graphics.DrawLine(p, r.Left, r.Bottom - 1, r.Right, r.Bottom - 1);
            }
            TextFormatFlags f = TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis | TextFormatFlags.SingleLine;
            f |= e.Header.TextAlign == HorizontalAlignment.Right ? TextFormatFlags.Right : TextFormatFlags.Left;
            Rectangle t = new Rectangle(r.Left + Ui.S(6), r.Top, r.Width - Ui.S(12), r.Height);
            TextRenderer.DrawText(e.Graphics, e.Header.Text, Ui.FontUI, t, Dim, f);
        }

        // the light theme's text colors -> dark ones
        static Color MapText(Color c)
        {
            int a = c.ToArgb();
            if (a == Color.DimGray.ToArgb() || a == SystemColors.GrayText.ToArgb() || a == Color.Gray.ToArgb()) return Dim;
            if (a == Ui.ColorDanger.ToArgb()) return Danger;
            if (a == Dim.ToArgb() || a == Danger.ToArgb() || a == Accent.ToArgb() || a == AccentSoft.ToArgb()) return c;
            return Text;
        }

        static void StyleButton(Button b)
        {
            b.FlatStyle = FlatStyle.Flat;
            b.UseVisualStyleBackColor = false;
            b.FlatAppearance.BorderSize = 1;
            ButtonColors(b);
            if (hooked.Add(b))
            {
                b.EnabledChanged += delegate { ButtonColors(b); };
                b.Disposed += delegate { hooked.Remove(b); filled.Remove(b); };
            }
        }

        static void ButtonColors(Button b)
        {
            bool danger;
            bool main = filled.TryGetValue(b, out danger);
            if (!b.Enabled)
            {
                b.BackColor = main ? Button : Panel;
                b.ForeColor = Dim;
                b.FlatAppearance.BorderColor = Line;
            }
            else if (main)
            {
                Color fill = danger ? Danger : Accent;
                b.BackColor = fill;
                b.ForeColor = danger ? DangerText : AccentText;
                b.FlatAppearance.BorderColor = fill;
                b.FlatAppearance.MouseOverBackColor = Lighter(fill, 0.15f);
                b.FlatAppearance.MouseDownBackColor = Lighter(fill, 0.3f);
            }
            else
            {
                b.BackColor = Button;
                b.ForeColor = Text;
                b.FlatAppearance.BorderColor = Border;
                b.FlatAppearance.MouseOverBackColor = ButtonHover;
                b.FlatAppearance.MouseDownBackColor = Border;
            }
        }

        public static Color Lighter(Color c, float amount)
        {
            return Color.FromArgb((int)(c.R + (255 - c.R) * amount), (int)(c.G + (255 - c.G) * amount), (int)(c.B + (255 - c.B) * amount));
        }

        // ---------- Windows: dark title bar, dark scroll bars ----------

        [DllImport("dwmapi.dll")]
        static extern int DwmSetWindowAttribute(IntPtr hwnd, int attr, ref int value, int size);
        [DllImport("uxtheme.dll", CharSet = CharSet.Unicode)]
        static extern int SetWindowTheme(IntPtr hwnd, string appName, string idList);
        [DllImport("user32.dll")]
        static extern IntPtr SendMessage(IntPtr hwnd, int msg, IntPtr w, IntPtr l);

        static void DarkTitle(IntPtr h)
        {
            int on = 1;
            try
            {
                // DWMWA_USE_IMMERSIVE_DARK_MODE: 20 (Windows 10 2004+), 19 before
                if (DwmSetWindowAttribute(h, 20, ref on, 4) != 0) DwmSetWindowAttribute(h, 19, ref on, 4);
            }
            catch (Exception) { }
        }

        // now when the window exists, else when it is created
        static void Native(Control c, Action<IntPtr> a)
        {
            Action<IntPtr> safe = delegate(IntPtr h) { try { a(h); } catch (Exception) { } };
            if (c.IsHandleCreated) safe(c.Handle);
            c.HandleCreated += delegate { safe(c.Handle); };
        }
    }

    // a group box with a thin dark frame and its title in the secondary color
    public class DarkGroupBox : GroupBox
    {
        public DarkGroupBox()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                     ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            using (SolidBrush b = new SolidBrush(Theme.Back)) g.FillRectangle(b, ClientRectangle);
            Size ts = TextRenderer.MeasureText(g, Text, Font);
            int y = ts.Height / 2;
            using (Pen p = new Pen(Theme.Line)) g.DrawRectangle(p, 0, y, Width - 1, Height - y - 1);
            Rectangle tr = new Rectangle(Ui.S(8), 0, ts.Width + Ui.S(4), ts.Height);
            using (SolidBrush b = new SolidBrush(Theme.Back)) g.FillRectangle(b, tr);
            TextRenderer.DrawText(g, Text, Font, tr, Theme.Dim, TextFormatFlags.HorizontalCenter | TextFormatFlags.Top);
        }
    }

    // flat tabs on the window color: the selected tab white with an accent
    // line below, the others grey. The pages fill the rest (no frame).
    public class DarkTabControl : TabControl
    {
        int hover = -1;

        public DarkTabControl()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                     ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            SizeMode = TabSizeMode.Normal;
            Font = Ui.FontBold;               // measured with the bold text
            Padding = new Point(Ui.S(14), Ui.S(9));
            Dock = DockStyle.Fill;
        }

        int HeaderHeight
        {
            get
            {
                if (!IsHandleCreated || TabCount == 0) return Ui.S(40);
                return GetTabRect(0).Bottom + Ui.S(1);
            }
        }

        public override Rectangle DisplayRectangle
        {
            get
            {
                int top = HeaderHeight + 1;
                return new Rectangle(0, top, Width, Math.Max(0, Height - top));
            }
        }

        protected override void OnSelectedIndexChanged(EventArgs e) { base.OnSelectedIndexChanged(e); Invalidate(); }

        protected override void OnMouseMove(MouseEventArgs e)
        {
            int h = -1;
            for (int i = 0; i < TabCount; i++) if (GetTabRect(i).Contains(e.Location)) h = i;
            if (h != hover) { hover = h; Invalidate(); }
            base.OnMouseMove(e);
        }

        protected override void OnMouseLeave(EventArgs e) { hover = -1; Invalidate(); base.OnMouseLeave(e); }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            int head = HeaderHeight;
            using (SolidBrush b = new SolidBrush(Theme.Back)) g.FillRectangle(b, ClientRectangle);
            using (Pen p = new Pen(Theme.Line)) g.DrawLine(p, 0, head, Width, head);
            for (int i = 0; i < TabCount; i++)
            {
                Rectangle r = GetTabRect(i);
                bool sel = i == SelectedIndex;
                Color fore = sel ? Color.White : (i == hover ? Theme.Text : Theme.Dim);
                TextRenderer.DrawText(g, TabPages[i].Text, sel ? Ui.FontBold : Ui.FontUI, r, fore,
                    TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
                if (sel)
                    using (SolidBrush b = new SolidBrush(Theme.Accent))
                        g.FillRectangle(b, r.Left + Ui.S(4), head - Ui.S(2), r.Width - Ui.S(8), Ui.S(2));
            }
        }
    }

    // a thin progress bar in the accent color; Style Marquee = running, no percent
    public class FlatProgress : Control
    {
        ProgressBarStyle style = ProgressBarStyle.Blocks;
        int value, phase;
        readonly Timer timer = new Timer();

        public FlatProgress()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                     ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            timer.Interval = 30;
            timer.Tick += delegate { phase = (phase + 1) % 120; Invalidate(); };
            Height = Ui.S(4);
        }

        public int Value
        {
            get { return value; }
            set { int v = Math.Max(0, Math.Min(100, value)); if (v != this.value) { this.value = v; Invalidate(); } }
        }

        public ProgressBarStyle Style
        {
            get { return style; }
            set
            {
                if (style == value) return;
                style = value;
                if (style == ProgressBarStyle.Marquee) timer.Start(); else timer.Stop();
                Invalidate();
            }
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            using (SolidBrush b = new SolidBrush(Theme.Line)) g.FillRectangle(b, ClientRectangle);
            using (SolidBrush b = new SolidBrush(Theme.Accent))
            {
                if (style == ProgressBarStyle.Marquee)
                {
                    int w = Math.Max(Ui.S(40), Width / 4);
                    int x = (int)((Width + w) * (phase / 120.0)) - w;
                    g.FillRectangle(b, x, 0, w, Height);
                }
                else if (value > 0) g.FillRectangle(b, 0, 0, Width * value / 100, Height);
            }
        }

        protected override void Dispose(bool disposing)
        {
            if (disposing) timer.Dispose();
            base.Dispose(disposing);
        }
    }

    // a combo box in which text can be typed (DropDown). Disabled, Windows
    // draws its edit part white with the text still selected (blue); here it
    // keeps the dark field color, the text in the dim color, nothing selected.
    public class EditCombo : ComboBox
    {
        const int WM_CTLCOLORSTATIC = 0x0138, WM_REFLECT = 0x2000;
        static IntPtr brush;

        [DllImport("gdi32.dll")] static extern int SetTextColor(IntPtr hdc, int color);
        [DllImport("gdi32.dll")] static extern int SetBkColor(IntPtr hdc, int color);
        [DllImport("gdi32.dll")] static extern IntPtr CreateSolidBrush(int color);

        public EditCombo()
        {
            DropDownStyle = ComboBoxStyle.DropDown;
            EnabledChanged += delegate { Unselect(); };
            TextChanged += delegate { if (!Enabled) Unselect(); };
            HandleCreated += delegate { if (!Enabled) Unselect(); };
        }

        void Unselect()
        {
            if (!IsHandleCreated || DropDownStyle == ComboBoxStyle.DropDownList) return;
            try { SelectionLength = 0; SelectionStart = 0; } catch (Exception) { }
        }

        protected override void WndProc(ref Message m)
        {
            if (m.Msg == WM_CTLCOLORSTATIC || m.Msg == (WM_REFLECT | WM_CTLCOLORSTATIC))
            {
                // the edit part while disabled (read-only edits report the same)
                SetTextColor(m.WParam, ColorTranslator.ToWin32(Theme.Dim));
                SetBkColor(m.WParam, ColorTranslator.ToWin32(Theme.Panel));
                if (brush == IntPtr.Zero) brush = CreateSolidBrush(ColorTranslator.ToWin32(Theme.Panel));
                m.Result = brush;
                return;
            }
            base.WndProc(ref m);
        }
    }
}
