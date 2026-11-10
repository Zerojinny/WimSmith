# ============================================================
#  WimSmith - make_file_icons.ps1
#  Draws the icons of the files WimSmith opens (.wim, .esd, .swm):
#  a document page with the three stacked layers of the program icon
#  (make_icon.ps1) and a band with the extension, in a color per type
#  (WIM teal, ESD violet, SWM orange). Writes icons\wim.ico, esd.ico
#  and swm.ico: 16, 20, 24, 32, 40, 48, 64 px as 32-bit BMP and 256 px
#  as PNG, each size drawn on its own; below 32 px the band has no text
#  and the layers are drawn larger.
#  They are icon resources 2 / 3 / 4 of WimSmith.exe (app.rc), used as
#  DefaultIcon of the file associations (assoc.cpp).
#
#  Usage (from any folder):
#    powershell -NoProfile -ExecutionPolicy Bypass -File backup\icon\make_file_icons.ps1
#  Then run build.bat. The PNG of each size goes to backup\icon\png-files\.
# ============================================================
param(
    [string]$OutDir = (Join-Path $PSScriptRoot '..\..\icons'),
    [string]$PngDir = (Join-Path $PSScriptRoot 'png-files')
)
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

function C($hex) { [System.Drawing.ColorTranslator]::FromHtml($hex) }
function P([double[]]$a) { $p = @(); for ($i = 0; $i -lt $a.Length; $i += 2) { $p += New-Object System.Drawing.PointF ([single]$a[$i]), ([single]$a[$i+1]) }; [System.Drawing.PointF[]]$p }
function Poly($g, $hex, [double[]]$pts) { $b = New-Object System.Drawing.SolidBrush (C $hex); $g.FillPolygon($b, (P $pts)); $b.Dispose() }
function RoundRect($g, $hex, $x, $y, $w, $h, $r) {
    $gp = New-Object System.Drawing.Drawing2D.GraphicsPath
    $gp.AddArc($x, $y, 2*$r, 2*$r, 180, 90); $gp.AddArc($x+$w-2*$r, $y, 2*$r, 2*$r, 270, 90)
    $gp.AddArc($x+$w-2*$r, $y+$h-2*$r, 2*$r, 2*$r, 0, 90); $gp.AddArc($x, $y+$h-2*$r, 2*$r, 2*$r, 90, 90); $gp.CloseFigure()
    $b = New-Object System.Drawing.SolidBrush (C $hex); $g.FillPath($b, $gp); $b.Dispose(); $gp.Dispose()
}
function Text($g, $s, $hex, $x, $y, $w, $h, $em) {
    $f = New-Object System.Drawing.Font 'Segoe UI', ([single]$em), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
    $sf = New-Object System.Drawing.StringFormat; $sf.Alignment = 'Center'; $sf.LineAlignment = 'Center'
    $b = New-Object System.Drawing.SolidBrush (C $hex)
    $g.DrawString($s, $f, $b, (New-Object System.Drawing.RectangleF ([single]$x), ([single]$y), ([single]$w), ([single]$h)), $sf)
    $b.Dispose(); $f.Dispose()
}

# the layers of each type: top, top side, middle, middle side, bottom, bottom side; the band
$Types = [ordered]@{
    wim = @{ pal = @('#3DD6C6','#23B0A3','#2AA89B','#1A7C73','#1D6B64','#124A45'); band = '#1FA396' }
    esd = @{ pal = @('#A99BFF','#8574E8','#7F6CE0','#5E4CB8','#5543A6','#3A2C7A'); band = '#6E5BD6' }
    swm = @{ pal = @('#FFC15A','#E39B2A','#E0962A','#B06F14','#A76612','#76470A'); band = '#D0861C' }
}

# the stack of the program icon (64 units)
function Layers($g, $pal) {
    $faces = @(@($pal[4], $pal[5], 30), @($pal[2], $pal[3], 20), @($pal[0], $pal[1], 10))
    foreach ($f in $faces) {
        $t = $f[2]
        Poly $g $f[1] @(8,($t+12), 32,($t+24), 56,($t+12), 56,($t+17), 32,($t+29), 8,($t+17))
        Poly $g $f[0] @(32,$t, 56,($t+12), 32,($t+24), 8,($t+12))
    }
}

function Render([int]$size, [string]$type) {
    $t = $Types[$type]
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'; $g.CompositingQuality = 'HighQuality'; $g.TextRenderingHint = 'AntiAliasGridFit'
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.ScaleTransform($size / 64.0, $size / 64.0)
    $small = $size -lt 32
    # the page with a folded corner
    Poly $g '#7C8B8A' @(9,2, 43,2, 56,15, 56,62, 9,62)
    Poly $g '#F5F8F8' @(10.5,3.5, 42.4,3.5, 54.5,15.6, 54.5,60.5, 10.5,60.5)
    Poly $g '#C9D3D2' @(42.4,3.5, 42.4,15.6, 54.5,15.6)
    # the layers (larger when small) and the band with the extension
    $st = $g.Save()
    if ($small) { $g.TranslateTransform(11, 2); $g.ScaleTransform(0.68, 0.68) } else { $g.TranslateTransform(13.5, 5); $g.ScaleTransform(0.6, 0.6) }
    Layers $g $t.pal
    $g.Restore($st)
    if ($small) { RoundRect $g $t.band 10.5 45 44 15.5 2 }
    else { RoundRect $g $t.band 10.5 43 44 17.5 2; Text $g $type.ToUpper() '#FFFFFF' 10.5 43 44 17.5 12.5 }
    $g.Dispose()
    return $bmp
}

function DibBytes($bmp) {
    $n = $bmp.Width
    $ms = New-Object System.IO.MemoryStream
    $bw = New-Object System.IO.BinaryWriter $ms
    $maskRow = [int]([Math]::Ceiling($n / 32.0) * 4)
    $bw.Write([int]40); $bw.Write([int]$n); $bw.Write([int]($n * 2)); $bw.Write([int16]1); $bw.Write([int16]32)
    $bw.Write([int]0); $bw.Write([int]($n * $n * 4 + $maskRow * $n)); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0); $bw.Write([int]0)
    for ($y = $n - 1; $y -ge 0; $y--) { for ($x = 0; $x -lt $n; $x++) {
        $c = $bmp.GetPixel($x, $y); $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R); $bw.Write([byte]$c.A) } }
    for ($y = 0; $y -lt $n; $y++) { for ($i = 0; $i -lt $maskRow; $i++) {
        $bits = 0
        for ($k = 0; $k -lt 8; $k++) { $x = $i * 8 + $k; if ($x -lt $n -and $bmp.GetPixel($x, $n - 1 - $y).A -eq 0) { $bits = $bits -bor (0x80 -shr $k) } }
        $bw.Write([byte]$bits) } }
    $bw.Flush(); return $ms.ToArray()
}

New-Item -ItemType Directory -Force $OutDir, $PngDir | Out-Null
$sizes = 16, 20, 24, 32, 40, 48, 64, 256
foreach ($type in $Types.Keys) {
    $images = @()
    foreach ($sz in $sizes) {
        $bmp = Render $sz $type
        $bmp.Save((Join-Path $PngDir "${type}_$sz.png"), [System.Drawing.Imaging.ImageFormat]::Png)
        if ($sz -ge 256) { $ms = New-Object System.IO.MemoryStream; $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png); $data = $ms.ToArray() }
        else { $data = DibBytes $bmp }
        $images += ,@($sz, $data); $bmp.Dispose()
    }
    $out = Join-Path $OutDir "$type.ico"
    $fs = [System.IO.File]::Create($out)
    $bw = New-Object System.IO.BinaryWriter $fs
    $bw.Write([int16]0); $bw.Write([int16]1); $bw.Write([int16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach ($im in $images) {
        $d = [byte]($im[0] % 256)
        $bw.Write($d); $bw.Write($d); $bw.Write([byte]0); $bw.Write([byte]0)
        $bw.Write([int16]1); $bw.Write([int16]32); $bw.Write([int]$im[1].Length); $bw.Write([int]$offset)
        $offset += $im[1].Length
    }
    foreach ($im in $images) { $bw.Write([byte[]]$im[1]) }
    $bw.Close()
    "Wrote $out ($((Get-Item $out).Length) bytes)"
}
