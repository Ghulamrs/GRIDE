#!/usr/bin/env python3
# Stitch a set of Markdown files into one self-contained, print-friendly HTML
# document with a generated table of contents. No third-party dependencies, so
# it runs on the Mac and on the Windows box alike. It covers the Markdown these
# manuals actually use: ATX headings, GFM pipe tables, fenced and indented code,
# bullet/numbered lists, blockquotes, horizontal rules, and inline code / bold /
# italic / links. Output is one file that opens in any browser and prints (or
# "Save as PDF") cleanly.
import html
import re
import sys

# ---- inline ---------------------------------------------------------------

_CODE = re.compile(r'`([^`]+)`')
_BOLD = re.compile(r'\*\*([^*]+)\*\*')
_ITAL = re.compile(r'(?<![*\w])\*([^*\n]+)\*(?!\w)')
_LINK = re.compile(r'\[([^\]]+)\]\(([^)\s]+)\)')

def inline(text):
    # Pull code spans out first so nothing rewrites their contents.
    spans = []
    def stash(m):
        spans.append('<code>' + html.escape(m.group(1)) + '</code>')
        return '\0%d\0' % (len(spans) - 1)
    text = _CODE.sub(stash, text)
    text = html.escape(text, quote=False)
    text = _LINK.sub(lambda m: '<a href="%s">%s</a>'
                     % (html.escape(m.group(2), quote=True), m.group(1)), text)
    text = _BOLD.sub(r'<strong>\1</strong>', text)
    text = _ITAL.sub(r'<em>\1</em>', text)
    text = re.sub(r'\0(\d+)\0', lambda m: spans[int(m.group(1))], text)
    return text

# ---- helpers --------------------------------------------------------------

def slug(text, seen):
    s = re.sub(r'[^a-z0-9]+', '-', text.lower()).strip('-') or 'section'
    base, n = s, 1
    while s in seen:
        n += 1
        s = '%s-%d' % (base, n)
    seen.add(s)
    return s

def is_rule(line):
    t = line.strip()
    return len(t) >= 3 and (set(t) == {'-'} or set(t) == {'='} or set(t) == {'*'})

def is_table_sep(line):
    t = line.strip()
    return '|' in t and set(t) <= set('|-: ') and '-' in t

# ---- block parser ---------------------------------------------------------

def render(md, toc, seen):
    lines = md.split('\n')
    out, i, n = [], 0, len(lines)
    while i < n:
        line = lines[i]

        if not line.strip():
            i += 1
            continue

        # fenced code
        if line.lstrip().startswith('```') or line.lstrip().startswith('~~~'):
            fence = line.lstrip()[:3]
            i += 1
            body = []
            while i < n and not lines[i].lstrip().startswith(fence):
                body.append(lines[i])
                i += 1
            i += 1  # closing fence
            out.append('<pre><code>%s</code></pre>'
                       % html.escape('\n'.join(body)))
            continue

        # heading
        m = re.match(r'(#{1,6})\s+(.*)$', line)
        if m:
            level = len(m.group(1))
            text = m.group(2).strip().rstrip('#').strip()
            sid = slug(text, seen)
            if level <= 3:
                toc.append((level, text, sid))
            out.append('<h%d id="%s">%s</h%d>' % (level, sid, inline(text), level))
            i += 1
            continue

        # horizontal rule
        if is_rule(line):
            out.append('<hr>')
            i += 1
            continue

        # table: header row followed by a separator row
        if '|' in line and i + 1 < n and is_table_sep(lines[i + 1]):
            def cells(row):
                row = row.strip()
                if row.startswith('|'):
                    row = row[1:]
                if row.endswith('|'):
                    row = row[:-1]
                return [c.strip() for c in row.split('|')]
            header = cells(line)
            i += 2
            body = []
            while i < n and '|' in lines[i] and lines[i].strip():
                body.append(cells(lines[i]))
                i += 1
            t = ['<table><thead><tr>']
            t += ['<th>%s</th>' % inline(c) for c in header]
            t.append('</tr></thead><tbody>')
            for r in body:
                t.append('<tr>' + ''.join('<td>%s</td>' % inline(c) for c in r) + '</tr>')
            t.append('</tbody></table>')
            out.append(''.join(t))
            continue

        # indented code block (all lines start with >=4 spaces or a tab)
        if line.startswith('    ') or line.startswith('\t'):
            body = []
            while i < n and (lines[i].startswith('    ') or lines[i].startswith('\t')
                             or not lines[i].strip()):
                if not lines[i].strip():
                    # stop if the blank line ends the block (next line not indented)
                    if i + 1 < n and not (lines[i + 1].startswith('    ')
                                          or lines[i + 1].startswith('\t')):
                        break
                    body.append('')
                else:
                    body.append(re.sub(r'^(    |\t)', '', lines[i]))
                i += 1
            out.append('<pre><code>%s</code></pre>'
                       % html.escape('\n'.join(body).rstrip()))
            continue

        # blockquote
        if line.lstrip().startswith('>'):
            body = []
            while i < n and lines[i].lstrip().startswith('>'):
                body.append(re.sub(r'^\s*>\s?', '', lines[i]))
                i += 1
            out.append('<blockquote>%s</blockquote>' % inline(' '.join(body)))
            continue

        # lists (unordered / ordered), with one level of nesting by indent
        if re.match(r'\s*([-*+]|\d+\.)\s+', line):
            items, i = parse_list(lines, i)
            out.append(items)
            continue

        # paragraph: gather until blank or a block starter
        body = [line]
        i += 1
        while i < n and lines[i].strip() and not _starts_block(lines[i], lines, i):
            body.append(lines[i])
            i += 1
        out.append('<p>%s</p>' % inline(' '.join(s.strip() for s in body)))
    return '\n'.join(out)

def _starts_block(line, lines, i):
    if re.match(r'#{1,6}\s', line) or is_rule(line):
        return True
    if line.lstrip().startswith('```') or line.lstrip().startswith('~~~'):
        return True
    if line.lstrip().startswith('>'):
        return True
    if re.match(r'\s*([-*+]|\d+\.)\s+', line):
        return True
    if '|' in line and i + 1 < len(lines) and is_table_sep(lines[i + 1]):
        return True
    return False

def parse_list(lines, i):
    n = len(lines)
    def indent(s):
        return len(s) - len(s.lstrip(' '))
    base = indent(lines[i])
    ordered = bool(re.match(r'\s*\d+\.\s', lines[i]))
    tag = 'ol' if ordered else 'ul'
    html_out = ['<%s>' % tag]
    while i < n and re.match(r'\s*([-*+]|\d+\.)\s+', lines[i]) and indent(lines[i]) == base:
        item = re.sub(r'^\s*([-*+]|\d+\.)\s+', '', lines[i])
        i += 1
        cont = [item]
        # gather continuation / nested lines
        while i < n and lines[i].strip() and indent(lines[i]) > base \
                and not re.match(r'\s*([-*+]|\d+\.)\s+', lines[i]):
            cont.append(lines[i].strip())
            i += 1
        inner = ''
        if i < n and re.match(r'\s*([-*+]|\d+\.)\s+', lines[i]) and indent(lines[i]) > base:
            inner, i = parse_list(lines, i)
        html_out.append('<li>%s%s</li>' % (inline(' '.join(cont)), inner))
    html_out.append('</%s>' % tag)
    return '\n'.join(html_out), i

# ---- template -------------------------------------------------------------

CSS = """
:root{--bg:#fff;--fg:#1a1a1a;--muted:#5a5a5a;--rule:#e2e2e2;--accent:#6a4a2a;
--codebg:#f5f3ef;--codefg:#3a2a1a;--tableh:#f0ece6;--link:#8a5a2a}
@media (prefers-color-scheme:dark){:root{--bg:#1b1b1b;--fg:#e6e6e6;--muted:#a5a5a5;
--rule:#333;--accent:#d8b48a;--codebg:#242220;--codefg:#e6d8c8;--tableh:#2a2724;--link:#e0b487}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);
font:16px/1.6 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif}
.wrap{max-width:52rem;margin:0 auto;padding:2.5rem 1.25rem 6rem}
h1,h2,h3,h4,h5,h6{line-height:1.25;font-weight:650;scroll-margin-top:1rem}
h1{font-size:2rem;margin:2.2rem 0 .6rem;padding-bottom:.3rem;border-bottom:2px solid var(--rule)}
h2{font-size:1.5rem;margin:2rem 0 .5rem;color:var(--accent)}
h3{font-size:1.2rem;margin:1.5rem 0 .4rem}
h4{font-size:1.02rem;margin:1.2rem 0 .3rem}
p{margin:.6rem 0}
a{color:var(--link);text-decoration:none}a:hover{text-decoration:underline}
code{background:var(--codebg);color:var(--codefg);padding:.1rem .3rem;border-radius:4px;
font:0.85em ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
pre{background:var(--codebg);color:var(--codefg);padding:1rem;border-radius:8px;
overflow-x:auto;border:1px solid var(--rule)}
pre code{background:none;padding:0;font-size:.82rem;line-height:1.45}
blockquote{margin:.8rem 0;padding:.3rem 1rem;border-left:3px solid var(--accent);color:var(--muted)}
hr{border:none;border-top:1px solid var(--rule);margin:2rem 0}
table{border-collapse:collapse;width:100%;margin:1rem 0;font-size:.92rem;display:block;overflow-x:auto}
th,td{border:1px solid var(--rule);padding:.45rem .6rem;text-align:left;vertical-align:top}
th{background:var(--tableh);font-weight:650}
ul,ol{margin:.5rem 0;padding-left:1.5rem}li{margin:.25rem 0}
.masthead{margin:0 0 1rem}.masthead .sub{color:var(--muted);font-size:.95rem}
nav.toc{background:var(--codebg);border:1px solid var(--rule);border-radius:8px;
padding:1rem 1.25rem;margin:1.5rem 0 2.5rem}
nav.toc .t{font-weight:650;margin-bottom:.4rem}
nav.toc ul{list-style:none;margin:0;padding:0}
nav.toc li{margin:.15rem 0}
nav.toc .l2{padding-left:1rem}nav.toc .l3{padding-left:2rem;font-size:.9rem}
nav.toc a{color:var(--fg)}
@media print{body{font-size:11pt}.wrap{max-width:none;padding:0}
nav.toc{break-after:page}h1,h2{break-after:avoid}pre,table,blockquote{break-inside:avoid}
a{color:inherit;text-decoration:none}}
"""

def build(title, subtitle, sections):
    """sections: list of (md_text, source_label). Returns full HTML string."""
    sections = list(sections)
    # Drop a leading content H1 that just repeats the masthead title, so the
    # page shows one banner rather than the same title twice.
    if sections and title:
        first_md, label = sections[0]
        rows = first_md.split('\n')
        for idx, row in enumerate(rows):
            if not row.strip():
                continue
            m = re.match(r'#\s+(.*)$', row)
            if m and m.group(1).strip().rstrip('#').strip() == title:
                del rows[idx]
            break
        sections[0] = ('\n'.join(rows), label)
    toc, seen, body = [], set(), []
    for md, _ in sections:
        body.append(render(md, toc, seen))
    nav = ['<nav class="toc"><div class="t">Contents</div><ul>']
    for level, text, sid in toc:
        nav.append('<li class="l%d"><a href="#%s">%s</a></li>' % (level, sid, inline(text)))
    nav.append('</ul></nav>')
    sub = ('<div class="sub">%s</div>' % html.escape(subtitle)) if subtitle else ''
    return ('<!doctype html><html lang="en"><head><meta charset="utf-8">'
            '<meta name="viewport" content="width=device-width,initial-scale=1">'
            '<title>%s</title><style>%s</style></head><body><div class="wrap">'
            '<div class="masthead">%s</div>%s%s</div></body></html>'
            % (html.escape(title), CSS,
               '<h1 style="border:none;margin-top:0">%s</h1>%s' % (html.escape(title), sub),
               '\n'.join(nav), '\n'.join(body)))

# ---- cli ------------------------------------------------------------------
# usage: docs2html.py <out.html> "<title>" "<subtitle>" <in1.md> [in2.md ...]

def main(argv):
    out, title, subtitle = argv[1], argv[2], argv[3]
    sections = []
    for path in argv[4:]:
        with open(path, encoding='utf-8') as f:
            sections.append((f.read(), path))
    with open(out, 'w', encoding='utf-8') as f:
        f.write(build(title, subtitle, sections))
    print('wrote', out, '(%d source file(s))' % len(sections))

if __name__ == '__main__':
    main(sys.argv)
