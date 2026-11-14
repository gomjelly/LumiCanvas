# Renders the LumiCanvas icon: resources/icons/lumicanvas.png (256 px, README and
# window icon source) and resources/icons/lumicanvas.ico (16-256 px, executable).
# The artwork is drawn on a 256-unit grid and scaled, so every size stays sharp.
#   powershell -ExecutionPolicy Bypass -File tools/make_icon.ps1
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$outDir = Join-Path $PSScriptRoot '..\resources\icons'
New-Item -ItemType Directory -Force $outDir | Out-Null

function New-RoundedRect([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

function Render-Icon([int]$size) {
    $bmp = New-Object Drawing.Bitmap $size, $size, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = 'AntiAlias'
    $g.PixelOffsetMode = 'HighQuality'
    $g.CompositingQuality = 'HighQuality'
    $g.Clear([Drawing.Color]::Transparent)
    $g.ScaleTransform($size / 256.0, $size / 256.0)

    # Night-blue tile.
    $tile = New-RoundedRect 8 8 240 240 52
    $bg = New-Object Drawing.Drawing2D.LinearGradientBrush ((New-Object Drawing.PointF 8, 8)), ((New-Object Drawing.PointF 248, 248)),
        ([Drawing.Color]::FromArgb(255, 58, 64, 150)), ([Drawing.Color]::FromArgb(255, 20, 18, 52))
    $g.FillPath($bg, $tile)

    # Beam of light from the lens to the canvas.
    $beam = New-Object Drawing.Drawing2D.GraphicsPath
    $beam.AddPolygon([Drawing.PointF[]]@(
        (New-Object Drawing.PointF 60, 70), (New-Object Drawing.PointF 132, 62),
        (New-Object Drawing.PointF 132, 206), (New-Object Drawing.PointF 60, 84)))
    $beamBrush = New-Object Drawing.Drawing2D.LinearGradientBrush ((New-Object Drawing.PointF 60, 0)), ((New-Object Drawing.PointF 134, 0)),
        ([Drawing.Color]::FromArgb(240, 255, 226, 150)), ([Drawing.Color]::FromArgb(150, 255, 240, 200))
    $g.FillPath($beamBrush, $beam)

    # Lens.
    $g.FillEllipse((New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 255, 236, 170))), 42, 59, 34, 36)
    $g.FillEllipse((New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 255, 255, 245))), 50, 68, 16, 18)

    # Canvas lit by the beam.
    $canvas = New-RoundedRect 128 56 92 156 12
    $g.FillPath((New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 252, 247, 236))), $canvas)

    # The guide stroke being traced on the canvas.
    $stroke = New-Object Drawing.Pen ([Drawing.Color]::FromArgb(255, 255, 112, 84)), 13
    $stroke.StartCap = 'Round'; $stroke.EndCap = 'Round'; $stroke.LineJoin = 'Round'
    $g.DrawBezier($stroke, (New-Object Drawing.PointF 148, 176), (New-Object Drawing.PointF 160, 108),
        (New-Object Drawing.PointF 196, 168), (New-Object Drawing.PointF 202, 92))

    # Sparkle on the tile corner.
    $spark = New-Object Drawing.Drawing2D.GraphicsPath
    $spark.AddPolygon([Drawing.PointF[]]@(
        (New-Object Drawing.PointF 70, 168), (New-Object Drawing.PointF 77, 186), (New-Object Drawing.PointF 95, 193),
        (New-Object Drawing.PointF 77, 200), (New-Object Drawing.PointF 70, 218), (New-Object Drawing.PointF 63, 200),
        (New-Object Drawing.PointF 45, 193), (New-Object Drawing.PointF 63, 186)))
    $g.FillPath((New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 255, 222, 140))), $spark)

    $g.Dispose()
    return $bmp
}

function Get-PngBytes($bmp) {
    $ms = New-Object IO.MemoryStream
    $bmp.Save($ms, [Drawing.Imaging.ImageFormat]::Png)
    return $ms.ToArray()
}

# Classic 32-bit DIB icon entry: BITMAPINFOHEADER, bottom-up BGRA rows, empty AND mask.
function Get-DibBytes($bmp) {
    $s = $bmp.Width
    $ms = New-Object IO.MemoryStream
    $w = New-Object IO.BinaryWriter $ms
    $w.Write([uint32]40); $w.Write([int32]$s); $w.Write([int32]($s * 2))
    $w.Write([uint16]1); $w.Write([uint16]32); $w.Write([uint32]0)
    $maskStride = [int][Math]::Ceiling($s / 32.0) * 4
    $w.Write([uint32]($s * $s * 4 + $maskStride * $s))
    $w.Write([int32]0); $w.Write([int32]0); $w.Write([uint32]0); $w.Write([uint32]0)
    for ($y = $s - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $s; $x++) {
            $c = $bmp.GetPixel($x, $y)
            $w.Write([byte]$c.B); $w.Write([byte]$c.G); $w.Write([byte]$c.R); $w.Write([byte]$c.A)
        }
    }
    $w.Write((New-Object byte[] ($maskStride * $s)))
    $w.Flush()
    return $ms.ToArray()
}

$big = Render-Icon 256
$big.Save((Join-Path $outDir 'lumicanvas.png'), [Drawing.Imaging.ImageFormat]::Png)
$big.Dispose()

# ICO: plain bitmaps for the small sizes, which every reader understands, and a
# PNG entry for 256 px, as Windows does for its own icons.
$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256
$images = foreach ($s in $sizes) {
    $b = Render-Icon $s
    if ($s -ge 256) { , (Get-PngBytes $b) } else { , (Get-DibBytes $b) }
    $b.Dispose()
}
$fs = [IO.File]::Create((Join-Path $outDir 'lumicanvas.ico'))
$w = New-Object IO.BinaryWriter $fs
$w.Write([uint16]0); $w.Write([uint16]1); $w.Write([uint16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $dim = if ($sizes[$i] -ge 256) { 0 } else { $sizes[$i] }
    $w.Write([byte]$dim); $w.Write([byte]$dim); $w.Write([byte]0); $w.Write([byte]0)
    $w.Write([uint16]1); $w.Write([uint16]32)
    $w.Write([uint32]$images[$i].Length); $w.Write([uint32]$offset)
    $offset += $images[$i].Length
}
foreach ($img in $images) { $w.Write([byte[]]$img) }
$w.Close()
"Wrote $outDir\lumicanvas.png and lumicanvas.ico ($($sizes -join ', ') px)"
