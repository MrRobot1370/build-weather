# Drive the running app with real mouse/keyboard input and capture frames.
#
#   -Script  a list of steps, each a hashtable:
#              @{ a='wait';   s=2 }
#              @{ a='click';  x=100; y=50 }
#              @{ a='scroll'; x=600; y=400; d=3 }     d>0 zoom in / wheel up
#              @{ a='drag';   x=600; y=400; x2=700; y2=450 }
#              @{ a='key';    k='{ESC}' }
#              @{ a='shot';   f='step1.png' }
#   Coordinates are CLIENT-relative to the app window, in physical pixels
#   (same space as the captured PNGs).
param(
    [string]$Exe = "F:\tmp\Fun-Prj\Build weather\build\msvc-x64\bin\Release\BuildWeather.exe",
    [string[]]$AppArgs = @(),
    [int]$StartupWait = 10,
    [Parameter(Mandatory=$true)][object[]]$Script
)

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class UI {
 [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
 [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
 [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
 [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
 [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
 [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr v);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
 [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, int d, IntPtr e);
 public delegate bool EnumProc(IntPtr h, IntPtr l);
 [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left,Top,Right,Bottom; }
 [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
 public const uint LEFTDOWN=0x0002, LEFTUP=0x0004, WHEEL=0x0800;
 public static IntPtr Find(string s){ IntPtr f=IntPtr.Zero; EnumWindows((h,l)=>{ if(!IsWindowVisible(h)) return true;
   var sb=new StringBuilder(256); GetWindowText(h,sb,256); if(sb.ToString().Contains(s)){f=h;return false;} return true; },IntPtr.Zero); return f; }
}
"@

$dpiPerMonitorV2 = New-Object System.IntPtr(-4)
[UI]::SetProcessDpiAwarenessContext($dpiPerMonitorV2) | Out-Null
$env:QT_FORCE_STDERR_LOGGING = "1"

$quoted = $AppArgs | ForEach-Object { '"' + $_ + '"' }
$proc = if ($quoted.Count -gt 0) {
    Start-Process $Exe -ArgumentList $quoted -PassThru `
        -RedirectStandardError "$env:TEMP\bw_err.txt" -RedirectStandardOutput "$env:TEMP\bw_out.txt"
} else {
    Start-Process $Exe -PassThru `
        -RedirectStandardError "$env:TEMP\bw_err.txt" -RedirectStandardOutput "$env:TEMP\bw_out.txt"
}
Start-Sleep -Seconds $StartupWait

$hwnd = [UI]::Find("Build Weather")
if ($hwnd -eq [IntPtr]::Zero) {
    "WINDOW NOT FOUND"; Get-Content "$env:TEMP\bw_err.txt","$env:TEMP\bw_out.txt" -EA SilentlyContinue
    if (!$proc.HasExited) { Stop-Process -Id $proc.Id -Force }; exit 1
}
[UI]::SetForegroundWindow($hwnd) | Out-Null
Start-Sleep -Milliseconds 400

# Window-relative, deliberately: PrintWindow captures the whole window, so a
# coordinate read off a captured PNG is directly usable as a click target.
function Get-Origin {
    $r = New-Object UI+RECT
    [UI]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $p = New-Object UI+POINT; $p.X = $r.Left; $p.Y = $r.Top
    return $p
}
function Move-To([int]$cx, [int]$cy) {
    $o = Get-Origin
    [UI]::SetCursorPos($o.X + $cx, $o.Y + $cy) | Out-Null
    Start-Sleep -Milliseconds 120
}
function Save-Shot([string]$file) {
    $r = New-Object UI+RECT
    [UI]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp); $hdc = $g.GetHdc()
    [UI]::PrintWindow($hwnd, $hdc, 2) | Out-Null
    $g.ReleaseHdc($hdc); $g.Dispose(); $bmp.Save($file); $bmp.Dispose()
    "shot: $file"
}

$cr = New-Object UI+RECT
[UI]::GetClientRect($hwnd, [ref]$cr) | Out-Null
"client area: $($cr.Right) x $($cr.Bottom)"

foreach ($step in $Script) {
    switch ($step.a) {
        'wait'   { Start-Sleep -Milliseconds ([int]($step.s * 1000)) }
        # Two positions, because a single SetCursorPos can land without the
        # target ever seeing a move, and a hover tooltip needs the move.
        'move'   { Move-To ($step.x - 6) ($step.y - 6)
                   Move-To $step.x $step.y
                   Start-Sleep -Milliseconds 900 }
        'click'  { Move-To $step.x $step.y
                   [UI]::mouse_event([UI]::LEFTDOWN, 0,0,0,[IntPtr]::Zero)
                   Start-Sleep -Milliseconds 60
                   [UI]::mouse_event([UI]::LEFTUP, 0,0,0,[IntPtr]::Zero)
                   Start-Sleep -Milliseconds 350 }
        'scroll' { Move-To $step.x $step.y
                   $ticks = [math]::Abs($step.d); $dir = if ($step.d -ge 0) { 120 } else { -120 }
                   for ($i=0; $i -lt $ticks; $i++) {
                       [UI]::mouse_event([UI]::WHEEL, 0,0,$dir,[IntPtr]::Zero)
                       Start-Sleep -Milliseconds 90
                   }
                   Start-Sleep -Milliseconds 350 }
        'drag'   { Move-To $step.x $step.y
                   [UI]::mouse_event([UI]::LEFTDOWN, 0,0,0,[IntPtr]::Zero)
                   Start-Sleep -Milliseconds 80
                   $steps = 12
                   for ($i=1; $i -le $steps; $i++) {
                       $ix = [int]($step.x + ($step.x2 - $step.x) * $i / $steps)
                       $iy = [int]($step.y + ($step.y2 - $step.y) * $i / $steps)
                       Move-To $ix $iy
                   }
                   [UI]::mouse_event([UI]::LEFTUP, 0,0,0,[IntPtr]::Zero)
                   Start-Sleep -Milliseconds 350 }
        'key'    { [System.Windows.Forms.SendKeys]::SendWait($step.k)
                   Start-Sleep -Milliseconds 300 }
        'shot'   { Save-Shot $step.f }
        default  { "unknown step: $($step.a)" }
    }
}

Stop-Process -Id $proc.Id -Force
"--- stderr/stdout ---"
Get-Content "$env:TEMP\bw_err.txt","$env:TEMP\bw_out.txt" -EA SilentlyContinue | Select-Object -Last 25
