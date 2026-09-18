Add-Type -AssemblyName System.Drawing

function Draw-Frame([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
    $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $g.Clear([System.Drawing.Color]::Transparent)

    $s = $size / 32.0
    function Rect([double]$x, [double]$y, [double]$w, [double]$h) {
        return New-Object System.Drawing.RectangleF ([float]($x*$s)), ([float]($y*$s)), ([float]($w*$s)), ([float]($h*$s))
    }

    $navy   = [System.Drawing.Color]::FromArgb(255, 0, 0, 128)
    $navyD  = [System.Drawing.Color]::FromArgb(255, 0, 0, 90)
    $yellow = [System.Drawing.Color]::FromArgb(255, 255, 204, 51)
    $yellowD= [System.Drawing.Color]::FromArgb(255, 219, 166, 21)
    $white  = [System.Drawing.Color]::FromArgb(255, 255, 255, 255)
    $gray   = [System.Drawing.Color]::FromArgb(255, 210, 210, 210)

    # Folder back tab
    $tabBrush = New-Object System.Drawing.SolidBrush $yellowD
    $g.FillRectangle($tabBrush, (Rect 3 5 11 5))

    # Folder body
    $bodyBrush = New-Object System.Drawing.SolidBrush $yellow
    $bodyRect = Rect 2 9 28 18
    $g.FillRectangle($bodyBrush, $bodyRect)

    # Dark outline around the whole folder
    $pen = New-Object System.Drawing.Pen $navyD, ([float][Math]::Max(1.0, [Math]::Round($s)))
    $g.DrawRectangle($pen, [int]($bodyRect.X), [int]($bodyRect.Y), [int]($bodyRect.Width)-1, [int]($bodyRect.Height)-1)
    $g.DrawRectangle($pen, [int](3*$s), [int](5*$s), [int](11*$s)-1, [int](5*$s))

    # Navy title strip across the top of the folder body (classic window chrome touch)
    $titleBrush = New-Object System.Drawing.SolidBrush $navy
    $g.FillRectangle($titleBrush, (Rect 2 9 28 4))

    if ($size -ge 24) {
        # Vertical divider = the two panes
        $dividerW = [Math]::Max(1.0, [Math]::Round(1.5 * $s))
        $divBrush = New-Object System.Drawing.SolidBrush $navyD
        $g.FillRectangle($divBrush, (Rect (16-$dividerW/(2*$s)) 13 ($dividerW/$s) 14))

        # File rows on each side
        $rowBrush = New-Object System.Drawing.SolidBrush $white
        for ($i=0; $i -lt 3; $i++) {
            $y = 15 + $i*4
            $g.FillRectangle($rowBrush, (Rect 4 $y 9 1.6))
            $g.FillRectangle($rowBrush, (Rect 19 $y 9 1.6))
        }
    }

    $g.Dispose()
    return $bmp
}

function BitmapToPngBytes($bmp) {
    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    return $ms.ToArray()
}

$sizes = @(16, 32, 48, 256)
$images = @()
foreach ($sz in $sizes) {
    $bmp = Draw-Frame $sz
    $png = BitmapToPngBytes $bmp
    $images += [PSCustomObject]@{ Size = $sz; Png = $png }
    $bmp.Dispose()
    Write-Output "size=$sz png bytes=$($png.Length)"
}
Write-Output "images.Count=$($images.Count)"

$outPath = "C:\Users\nagato\Workspace\kestrel\src\kestrel.ico"

$bytes = New-Object System.Collections.Generic.List[byte]
$bytes.AddRange([BitConverter]::GetBytes([UInt16]0))            # reserved
$bytes.AddRange([BitConverter]::GetBytes([UInt16]1))            # type = icon
$bytes.AddRange([BitConverter]::GetBytes([UInt16]$images.Count))

$headerSize = 6 + 16 * $images.Count
$offset = $headerSize
foreach ($img in $images) {
    $wByte = if ($img.Size -ge 256) { 0 } else { $img.Size }
    $hByte = if ($img.Size -ge 256) { 0 } else { $img.Size }
    $bytes.Add([byte]$wByte)
    $bytes.Add([byte]$hByte)
    $bytes.Add([byte]0)        # color count
    $bytes.Add([byte]0)        # reserved
    $bytes.AddRange([BitConverter]::GetBytes([UInt16]1))    # planes
    $bytes.AddRange([BitConverter]::GetBytes([UInt16]32))   # bit count
    $bytes.AddRange([BitConverter]::GetBytes([UInt32]$img.Png.Length))
    $bytes.AddRange([BitConverter]::GetBytes([UInt32]$offset))
    $offset += $img.Png.Length
}
foreach ($img in $images) {
    $bytes.AddRange([byte[]]$img.Png)
}

[System.IO.File]::WriteAllBytes($outPath, $bytes.ToArray())

Write-Output "wrote $outPath ($($bytes.Count) bytes)"
