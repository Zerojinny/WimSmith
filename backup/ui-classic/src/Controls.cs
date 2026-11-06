// ============================================================
//  Z_wimlib - Controls.cs (taken from the DVT GUI)
//  Owner-drawn controls for clearer navigation:
//    NavButton       : large button of the left sidebar
//    HeaderBanner    : colored title bar at the top of a page
//    AccentTabControl: tabs with the selected tab filled in the
//                      page color and an "unsaved" mark
// ============================================================
using System;
using System.Drawing;
using System.Windows.Forms;

namespace Dvt
{
    public static class Palette
    {
        public static readonly Color Sidebar = Color.FromArgb(30, 38, 50);
        public static readonly Color SidebarHover = Color.FromArgb(48, 58, 74);
        public static readonly Color SidebarText = Color.FromArgb(225, 230, 238);
        public static readonly Color SidebarSub = Color.FromArgb(150, 160, 175);
        public static readonly Color Local = Color.FromArgb(0, 99, 177);     // blue
        public static readonly Color Media = Color.FromArgb(0, 122, 100);    // teal
        public static readonly Color Test = Color.FromArgb(190, 92, 0);      // orange
        public static readonly Color Adk = Color.FromArgb(104, 72, 160);     // purple
        public static readonly Color Seeder = Color.FromArgb(176, 42, 92);   // crimson
        public static readonly Color Backup = Color.FromArgb(0, 120, 120);   // teal
        public static readonly Color TabIdle = Color.FromArgb(232, 235, 240);
        public static readonly Color TabIdleText = Color.FromArgb(60, 64, 72);
        public static readonly Color Unsaved = Color.FromArgb(255, 196, 0);

        public static Color Light(Color c, float amount)
        {
            return Color.FromArgb(
                (int)(c.R + (255 - c.R) * amount),
                (int)(c.G + (255 - c.G) * amount),
                (int)(c.B + (255 - c.B) * amount));
        }
    }

    public class NavButton : Control
    {
        public string Title = "";
        public string Subtitle = "";
        public Color Accent = Palette.Local;
        bool selected, hover, unsaved;

        public NavButton()
        {
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                     ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            Cursor = Cursors.Hand;
            Height = Ui.S(64);
            Margin = new Padding(0, 0, 0, Ui.S(4));
        }

        public bool Selected { get { return selected; } set { selected = value; Invalidate(); } }
        public bool Unsaved { get { return unsaved; } set { if (unsaved != value) { unsaved = value; Invalidate(); } } }

        protected override void OnMouseEnter(EventArgs e) { hover = true; Invalidate(); base.OnMouseEnter(e); }
        protected override void OnMouseLeave(EventArgs e) { hover = false; Invalidate(); base.OnMouseLeave(e); }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            Color bg = selected ? Accent : (hover ? Palette.SidebarHover : Palette.Sidebar);
            using (SolidBrush b = new SolidBrush(bg)) g.FillRectangle(b, ClientRectangle);
            using (SolidBrush b = new SolidBrush(selected ? Color.White : Accent))
                g.FillRectangle(b, 0, 0, Ui.S(5), Height);

            int x = Ui.S(16);
            Rectangle rt = new Rectangle(x, Ui.S(10), Width - x - Ui.S(8), Ui.S(24));
            Rectangle rs = new Rectangle(x, Ui.S(34), Width - x - Ui.S(8), Ui.S(20));
            TextRenderer.DrawText(g, Title, Ui.FontNav, rt, selected ? Color.White : Palette.SidebarText,
                TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
            TextRenderer.DrawText(g, Subtitle, Ui.FontUI, rs, selected ? Palette.Light(Accent, 0.8f) : Palette.SidebarSub,
                TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
            if (unsaved)
            {
                int d = Ui.S(10);
                using (SolidBrush b = new SolidBrush(Palette.Unsaved))
                    g.FillEllipse(b, Width - d - Ui.S(12), Ui.S(16), d, d);
            }
        }
    }

    public class HeaderBanner : Control
    {
        public string Title = "";
        public string Subtitle = "";
        public Color Accent = Palette.Local;

        public HeaderBanner(string title, string subtitle, Color accent)
        {
            Title = title;
            Subtitle = subtitle;
            Accent = accent;
            SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
                     ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
            Dock = DockStyle.Fill;
            Margin = new Padding(0, 0, 0, Ui.S(6));
        }

        protected override void OnPaint(PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            using (SolidBrush b = new SolidBrush(Accent)) g.FillRectangle(b, ClientRectangle);
            int x = Ui.S(14);
            TextRenderer.DrawText(g, Title, Ui.FontHeader, new Rectangle(x, Ui.S(4), Width - 2 * x, Ui.S(30)), Color.White,
                TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
            TextRenderer.DrawText(g, Subtitle, Ui.FontUI, new Rectangle(x, Ui.S(32), Width - 2 * x, Ui.S(20)), Palette.Light(Accent, 0.82f),
                TextFormatFlags.Left | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
        }
    }

    public class AccentTabControl : TabControl
    {
        public Color Accent = Palette.Local;

        public AccentTabControl(Color accent)
        {
            Accent = accent;
            DrawMode = TabDrawMode.OwnerDrawFixed;
            SizeMode = TabSizeMode.Normal;
            // bold font so that the measured tab width fits the bold text;
            // each TabPage gets the normal font back (Ui.NewTabPage)
            Font = Ui.FontBold;
            Padding = new Point(Ui.S(16), Ui.S(7));
            Dock = DockStyle.Fill;
        }

        // A tab with unsaved changes gets a dot in front of its text
        public void SetUnsaved(TabPage page, bool unsaved)
        {
            string baseText = page.Tag as string;
            if (baseText == null) { baseText = page.Text; page.Tag = baseText; }
            string text = unsaved ? "\u25CF " + baseText : baseText;
            if (page.Text != text) page.Text = text;
        }

        protected override void OnSelectedIndexChanged(EventArgs e)
        {
            base.OnSelectedIndexChanged(e);
            Invalidate();
        }

        protected override void OnDrawItem(DrawItemEventArgs e)
        {
            if (e.Index < 0 || e.Index >= TabPages.Count) return;
            TabPage page = TabPages[e.Index];
            Rectangle r = GetTabRect(e.Index);
            bool sel = (e.Index == SelectedIndex);
            using (SolidBrush b = new SolidBrush(sel ? Accent : Palette.TabIdle))
                e.Graphics.FillRectangle(b, r);
            if (!sel)
            {
                using (Pen p = new Pen(Palette.Light(Accent, 0.55f)))
                    e.Graphics.DrawLine(p, r.Left, r.Bottom - 1, r.Right, r.Bottom - 1);
            }
            Color fore = sel ? Color.White : Palette.TabIdleText;
            if (page.Text.StartsWith("\u25CF") && !sel) fore = Color.FromArgb(160, 100, 0);
            TextRenderer.DrawText(e.Graphics, page.Text, Ui.FontBold, r, fore,
                TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.SingleLine);
        }
    }
}
