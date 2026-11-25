param(
    [string]$AssetDir = (Join-Path $PSScriptRoot '..\third_party\opencode'),
    [string]$OutFile = (Join-Path $PSScriptRoot '..\src\opencode_icon_data.cpp')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

function Get-ResizedPixels([string]$srcPath, [int]$w, [int]$h) {
    $src = [System.Drawing.Bitmap]::FromFile($srcPath)
    $dst = New-Object System.Drawing.Bitmap($w, $h, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($dst)
    $g.Clear([System.Drawing.Color]::Transparent)
    $g.CompositingMode = [System.Drawing.Drawing2D.CompositingMode]::SourceCopy
    $g.CompositingQuality = [System.Drawing.Drawing2D.CompositingQuality]::HighQuality
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

    $ia = New-Object System.Drawing.Imaging.ImageAttributes
    $ia.SetWrapMode([System.Drawing.Drawing2D.WrapMode]::TileFlipXY)
    $rect = New-Object System.Drawing.Rectangle(0, 0, $w, $h)
    $g.DrawImage($src, $rect, 0, 0, $src.Width, $src.Height, [System.Drawing.GraphicsUnit]::Pixel, $ia)

    $locked = $dst.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $stride = $locked.Stride
    $bytes = New-Object byte[] ($stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($locked.Scan0, $bytes, 0, $bytes.Length)
    $dst.UnlockBits($locked)

    $ia.Dispose()
    $g.Dispose()
    $dst.Dispose()
    $src.Dispose()

    $out = New-Object uint32[] ($w * $h)
    for ($y = 0; $y -lt $h; $y++) {
        $row = $y * $stride
        for ($x = 0; $x -lt $w; $x++) {
            $out[$y * $w + $x] = [BitConverter]::ToUInt32($bytes, $row + $x * 4)
        }
    }
    return ,$out
}

function Append-SpriteSet([System.Text.StringBuilder]$sb, [string]$srcPath, [string]$prefix, [string]$arrayName, [int[]]$widths, [int[]]$heights) {
    $entries = New-Object 'System.Collections.Generic.List[string]'
    for ($i = 0; $i -lt $widths.Count; $i++) {
        $w = $widths[$i]
        $h = $heights[$i]
        $sym = "${prefix}_${w}x${h}"
        $px = Get-ResizedPixels $srcPath $w $h
        [void]$sb.AppendLine("static const uint32_t ${sym}[] = {")
        for ($y = 0; $y -lt $h; $y++) {
            [void]$sb.Append("    ")
            for ($x = 0; $x -lt $w; $x++) {
                $val = $px[$y * $w + $x]
                [void]$sb.Append(('0x{0:X8}u' -f $val))
                if ($y -lt ($h - 1) -or $x -lt ($w - 1)) { [void]$sb.Append(',') }
            }
            [void]$sb.AppendLine()
        }
        [void]$sb.AppendLine("};")
        $entries.Add("    { $w, $h, $sym }")
    }
    [void]$sb.AppendLine("extern const Raw ${arrayName}[] = {")
    [void]$sb.AppendLine(($entries -join ",`n"))
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine("extern const int ${arrayName}Count = $($widths.Count);")
    [void]$sb.AppendLine()
}

$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('#include <cstdint>')
[void]$sb.AppendLine()
[void]$sb.AppendLine('namespace opencode_icon_data {')
[void]$sb.AppendLine('struct Raw { int w, h; const uint32_t* px; };')
[void]$sb.AppendLine()

$stripW = @(16, 20, 24, 32, 48)
$stripH = @(20, 25, 30, 40, 60)
$sq = @(16, 20, 24, 32, 40, 48)

Append-SpriteSet $sb (Join-Path $AssetDir 'opencode-logo-transparent-light.png') 'kPxStripLight' 'kStripLight' $stripW $stripH
Append-SpriteSet $sb (Join-Path $AssetDir 'opencode-logo-transparent-dark.png') 'kPxStripDark' 'kStripDark' $stripW $stripH
Append-SpriteSet $sb (Join-Path $AssetDir 'opencode-logo-light-square.png') 'kPxSquareLight' 'kSquareLight' $sq $sq
Append-SpriteSet $sb (Join-Path $AssetDir 'opencode-logo-dark-square.png') 'kPxSquareDark' 'kSquareDark' $sq $sq

[void]$sb.AppendLine('}')

[System.IO.File]::WriteAllText((Resolve-Path (Split-Path $OutFile -Parent)).Path + '\' + (Split-Path $OutFile -Leaf), $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
Write-Output "wrote $OutFile"
