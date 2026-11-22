# Builds res\app.ico (white glyph, for the exe and dark themes) and res\app_light.ico
# (black glyph, for light taskbars) from the Segoe Fluent Icons TVMonitor glyph (U+E7F4).
# Each size is rendered with System.Drawing and stored as a classic 32-bit DIB entry
# (BITMAPINFOHEADER + BGRA rows + AND mask), which every Windows version loads.
param(
    [string]$OutDir = (Join-Path $PSScriptRoot '..\res'),
    [int]$Glyph = 0xE7F4
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sizes = 16, 20, 24, 32, 40, 48, 64, 128, 256

function New-IconEntry([int]$size, [System.Drawing.Color]$color) {
    $bmp = New-Object System.Drawing.Bitmap($size, $size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    # Rounded plate behind the glyph so the icon reads on light and dark surfaces alike.
    $bg = if ($color.R -gt 128) { [System.Drawing.Color]::FromArgb(255, 31, 31, 31) } else { [System.Drawing.Color]::FromArgb(255, 243, 243, 243) }
    $pad = [single]($size * 0.06)
    $radius = [single]($size * 0.22)
    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $radius * 2
    $x0 = $pad; $y0 = $pad; $x1 = $size - $pad; $y1 = $size - $pad
    $path.AddArc($x0, $y0, $d, $d, 180, 90)
    $path.AddArc($x1 - $d, $y0, $d, $d, 270, 90)
    $path.AddArc($x1 - $d, $y1 - $d, $d, $d, 0, 90)
    $path.AddArc($x0, $y1 - $d, $d, $d, 90, 90)
    $path.CloseFigure()
    $bgBrush = New-Object System.Drawing.SolidBrush($bg)
    $g.FillPath($bgBrush, $path)
    $bgBrush.Dispose(); $path.Dispose()
    $family = New-Object System.Drawing.FontFamily('Segoe Fluent Icons')
    $font = New-Object System.Drawing.Font($family, [single]($size * 0.60), [System.Drawing.FontStyle]::Regular, [System.Drawing.GraphicsUnit]::Pixel)
    $text = [string][char]$Glyph
    $fmt = [System.Drawing.StringFormat]::GenericTypographic
    $fmt.Alignment = [System.Drawing.StringAlignment]::Center
    $fmt.LineAlignment = [System.Drawing.StringAlignment]::Center
    $rect = New-Object System.Drawing.RectangleF(0, 0, $size, $size)
    $brush = New-Object System.Drawing.SolidBrush($color)
    $g.DrawString($text, $font, $brush, $rect, $fmt)

    $rectAll = New-Object System.Drawing.Rectangle(0, 0, $size, $size)
    $locked = $bmp.LockBits($rectAll, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $locked.Stride
    $pixels = New-Object byte[] ($stride * $size)
    [System.Runtime.InteropServices.Marshal]::Copy($locked.Scan0, $pixels, 0, $pixels.Length)
    $bmp.UnlockBits($locked)
    $g.Dispose(); $bmp.Dispose(); $font.Dispose(); $family.Dispose(); $brush.Dispose()

    $maskRow = [int]([math]::Ceiling($size / 32.0) * 4)
    $maskBytes = $maskRow * $size
    $imgBytes = 40 + $size * $size * 4 + $maskBytes
    $entry = New-Object byte[] $imgBytes

    # BITMAPINFOHEADER
    [BitConverter]::GetBytes([uint32]40).CopyTo($entry, 0)
    [BitConverter]::GetBytes([int32]$size).CopyTo($entry, 4)
    [BitConverter]::GetBytes([int32]($size * 2)).CopyTo($entry, 8)   # XOR + AND
    [BitConverter]::GetBytes([uint16]1).CopyTo($entry, 12)
    [BitConverter]::GetBytes([uint16]32).CopyTo($entry, 14)
    [BitConverter]::GetBytes([uint32]0).CopyTo($entry, 16)           # BI_RGB
    [BitConverter]::GetBytes([uint32]($size * $size * 4 + $maskBytes)).CopyTo($entry, 20)

    # XOR pixels: bottom-up rows, BGRA
    $out = 40
    for ($y = $size - 1; $y -ge 0; $y--) {
        $row = $y * $stride
        [Array]::Copy($pixels, $row, $entry, $out, $size * 4)
        $out += $size * 4
    }
    # AND mask: all zero (alpha channel does the work)
    for ($i = 0; $i -lt $maskBytes; $i++) { $entry[$out + $i] = 0 }
    Write-Output -NoEnumerate $entry
}

function New-Ico([string]$path, [System.Drawing.Color]$color) {
    $blobs = New-Object 'System.Collections.Generic.List[byte[]]'
    foreach ($s in $sizes) { $blobs.Add((New-IconEntry $s $color)) | Out-Null }
    $fs = [System.IO.File]::Create($path)
    $bw = New-Object System.IO.BinaryWriter($fs)
    $bw.Write([uint16]0); $bw.Write([uint16]1); $bw.Write([uint16]$sizes.Count)
    $offset = 6 + 16 * $sizes.Count
    for ($i = 0; $i -lt $sizes.Count; $i++) {
        $s = $sizes[$i]; $blob = $blobs[$i]
        $dim = if ($s -ge 256) { 0 } else { $s }
        $bw.Write([byte]$dim); $bw.Write([byte]$dim); $bw.Write([byte]0); $bw.Write([byte]0)
        $bw.Write([uint16]1); $bw.Write([uint16]32)
        $bw.Write([uint32]$blob.Length); $bw.Write([uint32]$offset)
        $offset += $blob.Length
    }
    foreach ($blob in $blobs) { $bw.Write($blob) }
    $bw.Close(); $fs.Close()
    Write-Output "wrote $path"
}

New-Ico (Join-Path $OutDir 'app.ico') ([System.Drawing.Color]::White)
New-Ico (Join-Path $OutDir 'app_light.ico') ([System.Drawing.Color]::Black)

