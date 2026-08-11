#!/usr/bin/env python3
"""
Nhung firmware/web/page.html vao PROGMEM trong firmware/src/main.cpp.

Chay sau moi lan sua page.html:
    python3 tools/embed_page.py

Script thay the phan giua 2 marker PAGE BEGIN / PAGE END, nen phan code
con lai cua main.cpp khong bi anh huong.
"""
import pathlib, re, sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HTML = ROOT / "firmware" / "web" / "page.html"
CPP  = ROOT / "firmware" / "src" / "main.cpp"
DELIM = "CCR"
BEGIN = "/* ---8<--- PAGE BEGIN --- */"
END   = "/* ---8<--- PAGE END --- */"


def minify(src: str) -> str:
    """Bo thut dau dong va dong trong. KHONG gop dong lai de khong lam
    hong JS."""
    out = []
    for line in src.splitlines():
        s = line.strip()
        if s:
            out.append(s)
    return "\n".join(out) + "\n"


def main() -> int:
    if not HTML.exists() or not CPP.exists():
        print(f"khong thay {HTML} hoac {CPP}", file=sys.stderr)
        return 1

    html = minify(HTML.read_text(encoding="utf-8"))

    # raw string literal se hong neu HTML chua chuoi ket thuc delimiter
    if f'){DELIM}"' in html:
        print(f'HTML chua ")%s\\"" - doi DELIM' % DELIM, file=sys.stderr)
        return 1

    block = (
        f'{BEGIN}\n'
        f'const char PAGE_HTML[] PROGMEM = R"{DELIM}(\n'
        f'{html}'
        f'){DELIM}";\n'
        f'{END}'
    )

    cpp = CPP.read_text(encoding="utf-8")
    pat = re.compile(re.escape(BEGIN) + r".*?" + re.escape(END), re.S)
    if not pat.search(cpp):
        print("khong thay marker PAGE BEGIN/END trong main.cpp", file=sys.stderr)
        return 1

    CPP.write_text(pat.sub(lambda _: block, cpp, count=1), encoding="utf-8")
    print(f"da nhung {len(html)} byte HTML vao {CPP.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
