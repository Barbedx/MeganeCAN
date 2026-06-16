#!/usr/bin/env python3
"""extract_log.py — pull the raw serial lines out of a saved proxy-page .html dump.

The proxy page stores every streamed line as <div class="l">...</div> inside #log.
Saving the page as HTML serialises those divs, but wrapped in markup and HTML-escaped,
which is unreadable. This strips it back to plain text — one log line per output line.

Usage:  python extract_log.py "in.html" [out.txt]
        python extract_log.py --dir "C:/path/to/folder"   # batch every *.html
"""
import html
import os
import re
import sys

LINE_RE = re.compile(r'<div class="l"[^>]*>(.*?)</div>', re.DOTALL)
TAG_RE = re.compile(r"<[^>]+>")


def extract(src_html: str) -> list:
    lines = []
    for m in LINE_RE.finditer(src_html):
        inner = TAG_RE.sub("", m.group(1))      # drop <mark> etc.
        lines.append(html.unescape(inner).strip())
    return lines


def convert(path: str, out: str = None) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        data = f.read()
    lines = extract(data)
    if out is None:
        out = os.path.splitext(path)[0] + ".txt"
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("%-60s -> %-40s (%d lines)" % (os.path.basename(path), os.path.basename(out), len(lines)))
    return out


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return
    if args[0] == "--dir":
        folder = args[1]
        outdir = os.path.join(folder, "_txt")
        os.makedirs(outdir, exist_ok=True)
        for name in sorted(os.listdir(folder)):
            if name.lower().endswith(".html"):
                base = os.path.splitext(name)[0] + ".txt"
                convert(os.path.join(folder, name), os.path.join(outdir, base))
        print("\nwrote .txt files to:", outdir)
    else:
        convert(args[0], args[1] if len(args) > 1 else None)


if __name__ == "__main__":
    main()
