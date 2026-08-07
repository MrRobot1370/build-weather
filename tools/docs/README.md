# Regenerating the user guide figures

`docs/USER_GUIDE.md` is written by hand; the figures under `docs/images/` are
generated, so they can be refreshed when the UI changes rather than going
stale silently.

## The pieces

| Script | What it does |
| --- | --- |
| `drive-app.ps1` | Launches Build Weather, sends real mouse and wheel input, captures the window |
| `annotate.ps1` | Crops a capture, draws numbered callouts and region boxes, scales it for print |
| `../build-docs.py` | Renders `USER_GUIDE.md` to `USER_GUIDE.pdf` |

## Doing it

1. Produce the data the figures show, once:

   ```
   tools\build-ninja.cmd
   tools\make-time-traces.cmd
   ```

2. Capture. Run this from a **fresh** PowerShell process:

   ```
   powershell -NoProfile -ExecutionPolicy Bypass -File tools\docs\capture.ps1
   ```

3. Annotate, then rebuild the PDF:

   ```
   python tools\build-docs.py
   ```

4. Read every page of the PDF and check it. Not a formality: pagination
   problems (a figure split across a page break, a section leaving one line
   on an otherwise blank page, a table column past the margin) only show up
   in the paginated output.

## Three things that will waste your time

**Run the capture in a fresh process.**
`SetProcessDpiAwarenessContext` can only be set once per process. In a reused
PowerShell session the call fails silently, `GetWindowRect` returns
DPI-virtualized coordinates, and the window comes back at the wrong size with
every annotation coordinate off. If a capture is not 2272x1466 on a 150%
display, this is why.

**Do not use `powershell -File` with array arguments.**
`-File` passes arguments as literal strings, so `-AppArgs @("a","b")` is not
evaluated and the app receives one nonsense argument. Either invoke the script
in-session with `&`, or bake the arguments into a wrapper like `capture.ps1`.

**Annotation coordinates are resolution-dependent.**
They are window pixels at 2272x1466, read straight off a previous capture.
Changing the window size, the display scaling or the Qt version invalidates
them, and they have to be re-read rather than scaled.

## A note on the checked-in figures

The captures in `docs/images/` were taken on a 2272x1466 window. A couple of
small boxes in `01-overview.png` and `02-tooltip.png` show collapsed
directories in neutral grey, which predates the change that colours them by
their mean duration. It is about half a percent of the pixels in those two
figures. Re-run the capture on a full-resolution display to refresh them.
