# ============================================================
#  WimSmith - make_icon.ps1
#  Draws the program icon (three stacked teal layers with a hammer
#  = images in one WIM file, worked on by the smith) with System.Drawing
#  and writes WimSmith.ico: 16, 20, 24, 32, 40, 48, 64 px as 32-bit BMP and
#  256 px as PNG. Each size is drawn on its own (no downscaling);
#  the hammer is thicker at 32 px and below. Up to the rename from
#  7z_wimlib the icon had a down arrow instead (see git history).
#  Colors follow src\Theme.cs (Accent #3DD6C6, AccentText #08201E).
#
#  Usage (from any folder):
#    powershell -NoProfile -ExecutionPolicy Bypass -File backup\icon\make_icon.ps1
#  Default output: WimSmith.ico in the project folder (overwritten) and
#  the PNG of each size in backup\icon\png\. Then run build.bat.
# ============================================================
param(
    [string]$OutIco = (Join-Path $PSScriptRoot '..\..\WimSmith.ico'),
    [string]$PngDir = (Join-Path $PSScriptRoot 'png')
)
Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

function C($hex) { [System.Drawing.ColorTranslator]::FromHtml($hex) }
function Poly($g, $hex, $pts) {
    $b = New-Object System.Drawing.SolidBrush (C $hex)
    $p = @(); for ($i = 0; $i -lt $pts.Length; $i += 2) { $p += New-Object System.Drawing.PointF ([single]$pts[$i]), ([single]$pts[$i+1]) }
    $g.FillPolygon($b, [System.Drawing.PointF[]]$p); $b.Dispose()
}

function Render([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'; $g.PixelOffsetMode = 'HighQuality'; $g.CompositingQuality = 'HighQuality'
    $g.Clear([System.Drawing.Color]::Transparent)
    $s = $size / 64.0
    $g.ScaleTransform($s, $s)
    # bottom layer
    Poly $g '#124A45' @(8,42, 32,54, 56,42, 56,47, 32,59, 8,47)
    Poly $g '#1D6B64' @(32,30, 56,42, 32,54, 8,42)
    # middle layer
    Poly $g '#1A7C73' @(8,32, 32,44, 56,32, 56,37, 32,49, 8,37)
    Poly $g '#2AA89B' @(32,20, 56,32, 32,44, 8,32)
    # top layer
    Poly $g '#23B0A3' @(8,22, 32,34, 56,22, 56,27, 32,39, 8,27)
    Poly $g '#3DD6C6' @(32,10, 56,22, 32,34, 8,22)
    # hammer (WimSmith: the smith who works on the images)
    $w = 3.4; if ($size -le 20) { $w = 4.6 } elseif ($size -le 32) { $w = 4.0 }
    $ink = New-Object System.Drawing.SolidBrush (C '#08201E')
    $pen = New-Object System.Drawing.Pen (C '#08201E'), ([single]$w)
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    $st = $g.Save()
    $g.TranslateTransform([single]26, [single]30.5)
    $g.RotateTransform([single]40)
    $g.DrawLine($pen, [single]0, [single]0, [single]0, [single]-12)
    $hw = 13.0; $hh = 5.6; if ($size -le 20) { $hw = 14.0; $hh = 6.6 }
    $head = New-Object System.Drawing.Drawing2D.GraphicsPath
    $r = 1.6; $x0 = -$hw / 2; $y0 = -12 - $hh
    $head.AddArc([single]$x0, [single]$y0, [single]($r*2), [single]($r*2), 180, 90)
    $head.AddArc([single]($x0+$hw-$r*2), [single]$y0, [single]($r*2), [single]($r*2), 270, 90)
    $head.AddArc([single]($x0+$hw-$r*2), [single]($y0+$hh-$r*2), [single]($r*2), [single]($r*2), 0, 90)
    $head.AddArc([single]$x0, [single]($y0+$hh-$r*2), [single]($r*2), [single]($r*2), 90, 90)
    $head.CloseFigure()
    $g.FillPath($ink, $head); $head.Dispose(); $ink.Dispose()
    $g.Restore($st)
    $pen.Dispose(); $g.Dispose()
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

$sizes = 16, 20, 24, 32, 40, 48, 64, 256
$images = @()
New-Item -ItemType Directory -Force $PngDir | Out-Null
foreach ($sz in $sizes) {
    $bmp = Render $sz
    $bmp.Save((Join-Path $PngDir "icon_$sz.png"), [System.Drawing.Imaging.ImageFormat]::Png)
    if ($sz -ge 256) { $ms = New-Object System.IO.MemoryStream; $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png); $data = $ms.ToArray() }
    else { $data = DibBytes $bmp }
    $images += ,@($sz, $data); $bmp.Dispose()
}

$fs = [System.IO.File]::Create($OutIco)
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
"Wrote $OutIco ($((Get-Item $OutIco).Length) bytes, $($images.Count) sizes)"
