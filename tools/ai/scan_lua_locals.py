"""Scan Lua files for active main-chunk local counts (Lua limit: 200 per function)."""
import re
import sys
from pathlib import Path

TOKEN = re.compile(r"""
    (?P<lcomment>--\[(?P<leq>=*)\[.*?\](?P=leq)\]) |
    (?P<comment>--[^\n]*) |
    (?P<lstring>\[(?P<seq>=*)\[.*?\](?P=seq)\]) |
    (?P<qstring>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*') |
    (?P<number>0[xX][0-9a-fA-F]+|\d+\.?\d*(?:[eE][+-]?\d+)?) |
    (?P<name>[A-Za-z_][A-Za-z0-9_]*) |
    (?P<punct>\.\.\.|\.\.|[+\-*/%^#<>=~(){}[];:,\.&|])
""", re.VERBOSE | re.DOTALL)

OPENERS = {"function", "if", "for", "while", "repeat"}
CLOSERS = {"end", "until"}


def scan(path):
    text = path.read_text(encoding="utf-8", errors="replace")
    pos, n = 0, len(text)
    depth = 0
    active = 0          # active main-chunk locals
    peak = 0
    peak_line = 0
    line = 1
    header_kw = None    # 'for'/'while' whose header we are inside
    last_sig = ""       # last significant token
    prev_name = ""      # last name token seen (for local name lists)
    events = []         # (line, active) when a local is added at depth 0
    while pos < n:
        m = TOKEN.match(text, pos)
        if not m:
            pos += 1
            continue
        tok = m.group(0)
        start_line = text.count("\n", 0, m.start()) + 1
        if m.lastgroup == "name":
            if header_kw and tok == "do":
                header_kw = None       # do belongs to for/while header
            elif tok == "end" or tok == "until":
                depth -= 1
            elif tok in OPENERS:
                if tok in ("for", "while"):
                    header_kw = tok
                depth += 1
            elif tok == "do":
                depth += 1
            elif tok == "local" and depth == 0:
                # count names declared by this local statement until ';' or
                # an opener/'=' at depth 0; local function counts one name
                p2, names = m.end(), 0
                saw_function = False
                while p2 < n:
                    m2 = TOKEN.match(text, p2)
                    if not m2:
                        break
                    g2, t2 = m2.lastgroup, m2.group(0)
                    if g2 in ("comment", "lcomment"):
                        p2 = m2.end(); continue
                    if g2 == "name":
                        if t2 == "function":
                            saw_function = True
                        else:
                            names += 1
                        p2 = m2.end(); continue
                    if g2 == "punct" and t2 == ",":
                        p2 = m2.end(); continue
                    if g2 == "punct" and t2 == "=":
                        # names before '=' are the declaration list
                        if saw_function:
                            names += 1
                        break
                    if g2 == "punct" and t2 == ";" or (g2 == "name" and t2 in ("in",)):
                        break
                    if saw_function:
                        names += 1
                        break
                    break
                if names == 0:
                    names = 1
                active += names
                events.append((start_line, active))
                if active > peak:
                    peak, peak_line = active, start_line
            pos = m.end()
            last_sig, prev_name = tok, tok
            line = text.count("\n", 0, pos) + 1
            continue
        pos = m.end()
        if m.lastgroup not in ("comment", "lcomment", "qstring", "lstring", "number"):
            last_sig = tok
    return peak, peak_line, active, events


def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else "lua/ai/isolated")
    files = sorted(root.glob("*.lua"))
    rows = []
    for f in files:
        peak, peak_line, total, events = scan(f)
        rows.append((peak, peak_line, total, f.name))
    rows.sort(reverse=True)
    print(f"{'peak':>5} {'@line':>7} {'total':>5}  file")
    for peak, peak_line, total, name in rows[:15]:
        print(f"{peak:>5} {peak_line:>7} {total:>5}  {name}")
    over = [r for r in rows if r[0] > 200]
    print(f"\nfiles over 200 active main-chunk locals: {len(over)}")
    near = [r for r in rows if 150 < r[0] <= 200]
    print(f"files between 150 and 200: {len(near)}")


if __name__ == "__main__":
    main()
