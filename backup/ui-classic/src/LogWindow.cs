// ============================================================
//  Z_wimlib - LogWindow.cs
//  The log of the jobs in its own window, hidden at start. Lines are
//  collected while it is hidden; [로그 보기 / Show log] of the main
//  window shows it (next to the main window), closing it only hides it.
// ============================================================
using System;
using System.Drawing;
using System.Windows.Forms;

namespace Dvt
{
    public class LogWindow : Form
    {
        readonly TextBox box;
        // lines of the time before the window was first shown (no handle yet)
        readonly System.Text.StringBuilder pending = new System.Text.StringBuilder();
        bool placed;

        public LogWindow()
        {
            Text = L.T("lg.title");
            Font = Ui.FontUI;
            ShowInTaskbar = false;
            StartPosition = FormStartPosition.Manual;
            ClientSize = new Size(Ui.S(760), Ui.S(420));
            MinimumSize = new Size(Ui.S(400), Ui.S(240));
            try { Icon = Icon.ExtractAssociatedIcon(Application.ExecutablePath); } catch (Exception) { }

            box = new TextBox();
            box.Dock = DockStyle.Fill;
            box.Multiline = true;
            box.ReadOnly = true;
            box.ScrollBars = ScrollBars.Both;
            box.WordWrap = false;
            box.Font = Ui.FontMono;
            box.BackColor = SystemColors.Window;
            box.MaxLength = 0;

            FlowLayoutPanel bar = new FlowLayoutPanel();
            bar.Dock = DockStyle.Bottom;
            bar.Height = Ui.S(40);
            bar.FlowDirection = FlowDirection.RightToLeft;
            bar.Padding = new Padding(Ui.S(6), Ui.S(4), Ui.S(6), 0);
            Button close = Ui.NewButton(L.T("lg.btn_close"), 90);
            Button clear = Ui.NewButton(L.T("lg.btn_clear"), 90);
            Button copy = Ui.NewButton(L.T("lg.btn_copy"), 110);
            bar.Controls.AddRange(new Control[] { close, clear, copy });

            Controls.Add(box);
            Controls.Add(bar);
            CancelButton = close;

            close.Click += delegate { Hide(); };
            clear.Click += delegate { box.Clear(); pending.Length = 0; };
            copy.Click += delegate
            {
                try { if (box.TextLength > 0) Clipboard.SetText(box.Text); }
                catch (Exception ex) { Ui.Info(ex.Message, MessageBoxIcon.Warning); }
            };
            // [X] hides the window; the lines stay
            FormClosing += delegate(object s, FormClosingEventArgs e)
            {
                if (e.CloseReason == CloseReason.UserClosing) { e.Cancel = true; Hide(); }
            };
        }

        public void Append(string line)
        {
            if (box.IsHandleCreated) box.AppendText(line + "\r\n");
            else pending.Append(line).Append("\r\n");
        }

        // shown next to the main window (to its right when there is room, else over it)
        public void ShowNear(Form main)
        {
            if (main != null && Owner == null) Owner = main;
            if (!placed && main != null)
            {
                placed = true;
                Rectangle work = Screen.FromControl(main).WorkingArea;
                Point p = new Point(main.Right + Ui.S(4), main.Top);
                if (p.X + Width > work.Right) p = new Point(main.Left + (main.Width - Width) / 2, main.Top + (main.Height - Height) / 2);
                p.X = Math.Max(work.Left, Math.Min(p.X, work.Right - Width));
                p.Y = Math.Max(work.Top, Math.Min(p.Y, work.Bottom - Height));
                Location = p;
            }
            if (!Visible) Show();
            if (pending.Length > 0) { box.AppendText(pending.ToString()); pending.Length = 0; }
            if (WindowState == FormWindowState.Minimized) WindowState = FormWindowState.Normal;
            Activate();
            box.SelectionStart = box.TextLength;
            box.ScrollToCaret();
        }
    }
}
