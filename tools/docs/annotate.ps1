# Crop a screenshot to the region worth showing, draw numbered callouts on it,
# and scale it for print.
#
# -Spec is a hashtable:
#   @{ src='in.png'; dst='out.png'; scale=0.66; radius=34
#      crop=@(x,y,w,h)                      # optional, source pixels
#      marks=@( @{ n=1; x=100; y=50; box=@(10,10,200,40) }, ... ) }
#
# Mark and box coordinates are relative to the CROP when one is given, so a
# figure can be recropped without recomputing every callout from the full
# window.
param([Parameter(Mandatory=$true)][hashtable]$Spec)

Add-Type -AssemblyName System.Drawing

$loaded = [System.Drawing.Bitmap]::FromFile($Spec.src)
if ($Spec.crop) {
    $c = $Spec.crop
    $cw = [int]$c[2]; $ch = [int]$c[3]
    $img = New-Object System.Drawing.Bitmap $cw, $ch
    $cg = [System.Drawing.Graphics]::FromImage($img)
    $cg.DrawImage($loaded, (New-Object System.Drawing.Rectangle 0,0,$cw,$ch),
                  (New-Object System.Drawing.Rectangle ([int]$c[0]),([int]$c[1]),$cw,$ch), 'Pixel')
    $cg.Dispose(); $loaded.Dispose()
} else {
    $img = $loaded
}
$w = $img.Width; $h = $img.Height

$g = [System.Drawing.Graphics]::FromImage($img)
$g.SmoothingMode = 'AntiAlias'
$g.TextRenderingHint = 'ClearTypeGridFit'

$accent = [System.Drawing.Color]::FromArgb(255, 196, 88, 8)
$boxPen = New-Object System.Drawing.Pen $accent, 4
$boxPen.DashStyle = 'Dash'
$fill = New-Object System.Drawing.SolidBrush $accent
$white = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::White)
$ring = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), 4
$r = if ($Spec.radius) { [int]$Spec.radius } else { 34 }
$font = New-Object System.Drawing.Font "Segoe UI", ([float]($r * 1.15)), ([System.Drawing.FontStyle]::Bold), ([System.Drawing.GraphicsUnit]::Pixel)
$fmt = New-Object System.Drawing.StringFormat
$fmt.Alignment = 'Center'; $fmt.LineAlignment = 'Center'

foreach ($m in $Spec.marks) {
    if ($m.box) {
        $b = $m.box
        $g.DrawRectangle($boxPen, [int]$b[0], [int]$b[1], [int]$b[2], [int]$b[3])
    }
}
foreach ($m in $Spec.marks) {
    $cx = [int]$m.x; $cy = [int]$m.y
    $g.FillEllipse($fill, ($cx - $r), ($cy - $r), (2*$r), (2*$r))
    $g.DrawEllipse($ring, ($cx - $r), ($cy - $r), (2*$r), (2*$r))
    $rect = New-Object System.Drawing.RectangleF ($cx - $r), ($cy - $r + 2), (2*$r), (2*$r)
    $g.DrawString([string]$m.n, $font, $white, $rect, $fmt)
}
$g.Dispose()

$scale = if ($Spec.scale) { [double]$Spec.scale } else { 1.0 }
$outW = [int]($w * $scale); $outH = [int]($h * $scale)
$out = New-Object System.Drawing.Bitmap $outW, $outH
$og = [System.Drawing.Graphics]::FromImage($out)
$og.InterpolationMode = 'HighQualityBicubic'
$og.PixelOffsetMode = 'HighQuality'
$og.DrawImage($img, (New-Object System.Drawing.Rectangle 0, 0, $outW, $outH))
$og.Dispose(); $img.Dispose()
$out.Save($Spec.dst, [System.Drawing.Imaging.ImageFormat]::Png)
$out.Dispose()
"$([System.IO.Path]::GetFileName($Spec.dst))  ${outW}x${outH}"
