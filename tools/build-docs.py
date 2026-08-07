#!/usr/bin/env python3
"""Render docs/USER_GUIDE.md to docs/USER_GUIDE.pdf.

Markdown is the source of truth; this produces the printable form. The CSS
here is print CSS, not web CSS, and most of it exists to prevent the specific
ways a paginated document goes wrong: a figure split across two pages, a
heading stranded at the bottom of one, a table column overflowing the margin,
a long path running off the edge.

Chromium (Edge, or Chrome as a fallback) does the pagination because it is the
only renderer guaranteed to be on a Windows box and it honours the
break-inside / break-before properties this depends on.

Usage:  python tools/build-docs.py
"""

from __future__ import annotations

import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

try:
    import markdown
except ImportError:
    sys.exit("pip install markdown")

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
SOURCE = DOCS / "USER_GUIDE.md"
HTML = DOCS / "_user_guide.html"
PDF = DOCS / "USER_GUIDE.pdf"

BROWSERS = [
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
]

CSS = """
@page {
    size: A4;
    margin: 17mm 15mm 20mm 15mm;
}

html { -webkit-print-color-adjust: exact; print-color-adjust: exact; }

body {
    font-family: "Segoe UI", "Helvetica Neue", Arial, sans-serif;
    font-size: 10.5pt;
    line-height: 1.52;
    color: #1a2330;
    margin: 0;
    /* Never let a paragraph leave one lonely line behind. */
    orphans: 3;
    widows: 3;
}

/* ---- headings ------------------------------------------------------- */
h1 {
    font-size: 25pt;
    line-height: 1.15;
    margin: 0 0 2mm 0;
    color: #8a4a06;
    letter-spacing: -0.4pt;
}
h1 + p { color: #556078; font-size: 11pt; margin-top: 0; }

/* Each numbered section starts a page. Slightly wasteful of paper and worth
   it: it removes every chance of a section heading stranded at the foot of a
   page, and of a figure being separated from the list that explains it. */
h2 {
    font-size: 15pt;
    margin: 0 0 3.5mm 0;
    padding-bottom: 1.8mm;
    border-bottom: 2px solid #e8b06a;
    color: #7a4205;
    break-before: page;
    break-after: avoid;
}
h1 + p + h2, h2:first-of-type { break-before: avoid; }

h3 {
    font-size: 11.5pt;
    margin: 6mm 0 2mm 0;
    color: #1a2330;
    break-after: avoid;
}

p { margin: 0 0 3mm 0; }
strong { color: #16202c; }

/* ---- figures -------------------------------------------------------- */
/* A screenshot and its caption are one unit and never split. The height cap
   is what keeps a figure plus its explanation list on the same page. */
figure {
    margin: 3mm 0 4mm 0;
    break-inside: avoid;
    page-break-inside: avoid;
    text-align: center;
}
figure img {
    max-width: 100%;
    max-height: 92mm;
    width: auto;
    height: auto;
    border: 1px solid #c7d0dd;
    border-radius: 2mm;
}
/* Small crops look absurd blown up to the full text width, and the height
   they gain pushes whatever follows onto the next page. */
figure img.narrow { max-width: 64%; }
figure img.wide   { max-width: 100%; }

figcaption {
    font-size: 8.6pt;
    color: #667085;
    margin-top: 1.6mm;
    font-style: italic;
}

/* An ordered list right after a figure explains its callouts, so keep the
   two together where the page allows it. */
figure + ol { break-before: avoid; }

/* ---- lists ---------------------------------------------------------- */
ol, ul { margin: 0 0 3mm 0; padding-left: 7mm; }
li { margin-bottom: 1.4mm; }
li > p { margin: 0 0 1.5mm 0; }

/* ---- tables --------------------------------------------------------- */
/* table-layout: fixed plus word-break is what stops a long Windows path
   from pushing a column past the right margin. */
table {
    width: 100%;
    border-collapse: collapse;
    margin: 3mm 0 5mm 0;
    font-size: 9.4pt;
    table-layout: fixed;
    break-inside: avoid;
    page-break-inside: avoid;
}
th, td {
    border: 1px solid #d3dae5;
    padding: 1.7mm 2.4mm;
    text-align: left;
    vertical-align: top;
    word-break: break-word;
    overflow-wrap: anywhere;
}
th {
    background: #f2f4f8;
    font-weight: 600;
    color: #3a4556;
    font-size: 8.8pt;
    text-transform: uppercase;
    letter-spacing: 0.3pt;
}
tr:nth-child(even) td { background: #fafbfd; }

/* ---- code ----------------------------------------------------------- */
code {
    font-family: "Cascadia Mono", Consolas, monospace;
    font-size: 9pt;
    background: #f2f4f8;
    padding: 0.3mm 1mm;
    border-radius: 1mm;
    overflow-wrap: anywhere;
}
pre {
    background: #f7f8fa;
    border: 1px solid #e1e6ee;
    border-left: 3px solid #e8b06a;
    border-radius: 1.5mm;
    padding: 2.6mm 3mm;
    margin: 0 0 4mm 0;
    break-inside: avoid;
    page-break-inside: avoid;
    /* Wrap rather than clip: a command line must never run off the page. */
    white-space: pre-wrap;
    overflow-wrap: anywhere;
}
pre code { background: none; padding: 0; font-size: 8.9pt; line-height: 1.45; }

/* ---- notes ---------------------------------------------------------- */
blockquote {
    margin: 4mm 0;
    padding: 2.6mm 4mm;
    background: #fdf6e8;
    border-left: 3px solid #d99a2b;
    border-radius: 0 1.5mm 1.5mm 0;
    color: #5c4410;
    break-inside: avoid;
    page-break-inside: avoid;
}
blockquote p { margin: 0; }
blockquote p + p { margin-top: 2mm; }

/* The Markdown source uses --- to separate sections; h2 already starts a
   page, so the rules would print as stray lines at the top of every page. */
hr { display: none; }
"""


def to_figures(html: str) -> str:
    """Turn a lone image paragraph into a figure with its alt text as caption."""

    def repl(match: re.Match[str]) -> str:
        tag, alt = match.group(1), match.group(2)
        caption = f"<figcaption>{alt}</figcaption>" if alt else ""
        return f"<figure>{tag}{caption}</figure>"

    return re.sub(
        r'<p>(<img [^>]*alt="([^"]*)"[^>]*/?>)</p>',
        repl,
        html,
    )


def find_browser() -> str:
    for candidate in BROWSERS:
        if Path(candidate).exists():
            return candidate
    sys.exit("no Edge or Chrome found to paginate the PDF")


def main() -> int:
    if not SOURCE.exists():
        sys.exit(f"missing {SOURCE}")

    body = markdown.markdown(
        SOURCE.read_text(encoding="utf-8"),
        extensions=["tables", "fenced_code", "attr_list", "sane_lists"],
    )
    body = to_figures(body)

    HTML.write_text(
        "<!doctype html>\n<html><head><meta charset='utf-8'>"
        "<title>Build Weather User Guide</title>"
        f"<style>{CSS}</style></head><body>\n{body}\n</body></html>",
        encoding="utf-8",
    )

    if PDF.exists():
        PDF.unlink()

    browser = find_browser()
    # --no-pdf-header-footer keeps Chromium from stamping a URL and date over
    # the margins, which is the default and looks like a mistake in a manual.
    subprocess.run(
        [
            browser,
            "--headless=new",
            "--disable-gpu",
            "--no-sandbox",
            "--no-pdf-header-footer",
            "--print-to-pdf-no-header",
            f"--print-to-pdf={PDF}",
            HTML.resolve().as_uri(),
        ],
        check=False,
        capture_output=True,
        timeout=180,
    )

    # Headless Chromium sometimes returns before the file is flushed.
    for _ in range(40):
        if PDF.exists() and PDF.stat().st_size > 0:
            break
        time.sleep(0.25)

    if not PDF.exists():
        sys.exit("Chromium did not produce a PDF")

    print(f"{PDF}  ({PDF.stat().st_size / 1024:.0f} KiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
