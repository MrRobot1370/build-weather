# Capture every documentation figure in one fresh process.
#
# Fresh process matters: SetProcessDpiAwarenessContext can only be set once
# per process, so re-running the capture inside an already-used PowerShell
# session silently falls back to DPI-virtualized coordinates and the window
# comes back at 1096x788 instead of 2272x1466. Every annotation coordinate is
# tuned for the latter, so the capture must not be run in a reused session.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$bw   = Resolve-Path (Join-Path $here "..\..")
$raw  = Join-Path $bw "docs\_captures"


New-Item -ItemType Directory -Force $raw | Out-Null

& (Join-Path $here "drive-app.ps1") -AppArgs @("$bw\build\ninja-x64","--source",$bw,
                            "--traces","$bw\build\clangcl-x64",
                            "--baseline",(Join-Path $raw "baseline.ninja_log")) -StartupWait 16 -Script @(
  @{a='move'; x=2100; y=1300},
  @{a='shot'; f="$raw\map_overview.png"},
  @{a='move'; x=1150; y=430},
  @{a='shot'; f="$raw\map_tooltip.png"},
  @{a='scroll'; x=1150; y=600; d=5},
  @{a='move'; x=2100; y=1300},
  @{a='shot'; f="$raw\map_zoom.png"},
  @{a='click'; x=1241; y=207},
  @{a='click'; x=176;  y=137},
  @{a='wait';  s=1},
  @{a='move';  x=1150; y=1300},
  @{a='shot';  f="$raw\analysis_slow.png"},
  @{a='click'; x=210;  y=207},
  @{a='wait';  s=1},
  @{a='click'; x=700;  y=418},
  @{a='move';  x=1150; y=1300},
  @{a='wait';  s=1},
  @{a='shot';  f="$raw\analysis_headers.png"},
  @{a='click'; x=299;  y=137},
  @{a='wait';  s=1},
  @{a='move';  x=1150; y=1300},
  @{a='shot';  f="$raw\compare.png"}
) | Select-Object -Last 1

Add-Type -AssemblyName System.Drawing
$b = [System.Drawing.Bitmap]::FromFile("$raw\map_overview.png")
"captured at $($b.Width)x$($b.Height)"
$b.Dispose()

