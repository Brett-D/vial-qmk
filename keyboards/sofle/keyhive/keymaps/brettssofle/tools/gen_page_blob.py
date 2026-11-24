#!/usr/bin/env python3
# Copyright 2026 Brett-D
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the copy of the config page the keyboard delivers by typing.

Reads sofle-config.html, sofle-light.html, bootstrap.html and loader.js
(this folder) and writes ../config_page_blob.h:

  config_page_gz[]         the full page, minified and gzipped (served over
                           the network; typed with F)
  config_light_gz[]        the light page (typed with P)
  config_bootstrap_keys[]  stage 1: the bootstrap data: URL as US-layout HID
                           usages (| 0x80 = with Shift), typed into the
                           address bar (kept tiny: address bars get slow)
  config_loader_keys[]     stage 2: "~" + loader.js on one line + "~" + its
                           hash + "~", typed into the page stage 1 opened

Run by rules.mk at every build (the header is only rewritten when it
changes) and by gen_config.py, so the delivered page never drifts from
sofle-config.html. Standard library only.

    python gen_page_blob.py [--print-url]
"""

import gzip
import hashlib
import re
import sys
from pathlib import Path

from paths import GEN, PAGE
OUT = GEN / "config_page_blob.h"

# Report rates used for the time estimate (deliver.c).
BOOT_REPORT_MS = 3   # address bar: key down + key up per character
LOADER_KEY_MS = 4    # loader: key down + key up, 2 ms each
STREAM_KEY_MS = 4    # page, first pass: key down + key up, 2 ms each
BLOCK_PAYLOAD, BLOCK_KEYS = 51, 97  # deliver.c: ',' + 96 symbols per 51 bytes


# Bump when the generated header's layout changes, so it is rewritten.
HEADER_FORMAT = "4"


def css_min(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"\s+", " ", s)
    s = re.sub(r"\s*([{};:,>])\s*", r"\1", s)
    return s.replace(";}", "}").strip()


def js_min(s):
    # Conservative: drop indentation, blank lines and whole-line // comments;
    # keep the line breaks (automatic semicolon insertion stays intact).
    lines = (line.strip() for line in s.split("\n"))
    return "\n".join(t for t in lines if t and not t.startswith("//"))


def minify_page(html):
    html = re.sub(r"<style>(.*?)</style>", lambda m: "<style>" + css_min(m.group(1)) + "</style>", html, flags=re.S)
    out = []
    for part in re.split(r"(<script>.*?</script>)", html, flags=re.S):
        if part.startswith("<script>"):
            out.append("<script>" + js_min(part[8:-9]) + "</script>")
        else:
            out.append(re.sub(r"\n[ \t]+", "\n", part))
    return "".join(out)


def bootstrap_url(source):
    body = re.sub(r"<!--.*?-->", "", source, flags=re.S)
    body = "".join(line.strip() for line in body.split("\n"))
    url = "data:text/html," + body
    bad = sorted({c for c in url if not ("!" <= c <= "~") or c in "#%"})
    if bad:
        sys.exit(f"bootstrap.html: characters that can't be typed into the address bar safely: {bad!r}")
    return url


def squeeze_js(text):
    """Drops the spaces that aren't needed: keeps string, template and regex
    literals as they are, and one space only between two word characters."""
    out, i, n = [], 0, len(text)
    word = lambda ch: ch.isalnum() or ch in "_$"
    while i < n:
        ch = text[i]
        if ch in "'\"`":
            j = i + 1
            while text[j] != ch:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif ch == "/" and (not "".join(out).rstrip() or "".join(out).rstrip()[-1] in "(,=:[!&|?{};"):
            j = i + 1
            while text[j] != "/":
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif ch == " ":
            prev = out[-1][-1] if out else ""
            j = i
            while j < n and text[j] == " ":
                j += 1
            if prev and j < n and word(prev) and word(text[j]):
                out.append(" ")
            i = j
        else:
            out.append(ch)
            i += 1
    return "".join(out)


def loader_text(source):
    """loader.js on one line: whole-line comments dropped, lines joined, spaces squeezed."""
    lines = (line.strip() for line in source.split("\n"))
    text = squeeze_js(" ".join(t for t in lines if t and not t.startswith("//")))
    bad = sorted({ch for ch in text if not (" " <= ch <= "}")})
    if bad:
        sys.exit(f"loader.js: characters that can't be typed (or '~'): {bad!r}")
    return text


def js_hash(text):
    """bootstrap.html's check: h = h * 31 + char, as a signed 32-bit number."""
    h = 0
    for ch in text:
        h = (h * 31 + ord(ch)) & 0xFFFFFFFF
    return h - (1 << 32) if h & 0x80000000 else h


def loader_keys_text():
    text = loader_text((PAGE / "loader.js").read_text(encoding="utf-8"))
    return "~" + text + "~" + str(js_hash(text)) + "~"


US_SHIFTED = {'!': '1', '@': '2', '$': '4', '^': '6', '&': '7', '*': '8', '(': '9', ')': '0', '_': '-', '+': '=',
              '{': '[', '}': ']', '|': '\\', ':': ';', '"': "'", '~': '`', '<': ',', '>': '.', '?': '/'}
US_PLAIN = {'-': 0x2D, '=': 0x2E, '[': 0x2F, ']': 0x30, '\\': 0x31, ';': 0x33, "'": 0x34, '`': 0x35, ',': 0x36,
            '.': 0x37, '/': 0x38}


def us_key(c):
    """HID usage | 0x80 (Shift) for a printable ASCII character, US layout."""
    if "a" <= c <= "z":
        return 0x04 + ord(c) - ord("a")
    if "A" <= c <= "Z":
        return 0x80 | (0x04 + ord(c) - ord("A"))
    if "1" <= c <= "9":
        return 0x1E + ord(c) - ord("1")
    if c == "0":
        return 0x27
    if c == " ":
        return 0x2C
    if c in US_PLAIN:
        return US_PLAIN[c]
    if c in US_SHIFTED:
        return 0x80 | us_key(US_SHIFTED[c])
    raise ValueError(f"no US key for {c!r}")


def stream_keys(gz):
    """Keys deliver.c types for one pass of a page (',' + 96 symbols per block, '...')."""
    blocks = (len(gz) + BLOCK_PAYLOAD - 1) // BLOCK_PAYLOAD
    return blocks * BLOCK_KEYS + 3


def c_array(data, per_line=24):
    rows = [", ".join(f"0x{b:02x}" for b in data[i:i + per_line]) for i in range(0, len(data), per_line)]
    return "    " + ",\n    ".join(rows)


def artifacts():
    """(page, minified page, gzip bytes, bootstrap URL, bootstrap keys) - also used by tests."""
    page = (GEN / "sofle-config.html").read_text(encoding="utf-8")
    small = minify_page(page)
    gz = gzip.compress(small.encode("utf-8"), 9, mtime=0)
    url = bootstrap_url((PAGE / "bootstrap.html").read_text(encoding="utf-8"))
    return page, small, gz, url, [us_key(c) for c in url]


def light_artifacts():
    """(page, minified, gzip) of the light page."""
    page = (GEN / "sofle-light.html").read_text(encoding="utf-8")
    small = minify_page(page)
    return page, small, gzip.compress(small.encode("utf-8"), 9, mtime=0)


def build():
    page, small, gz, url, keys = artifacts()
    lpage, lsmall, lgz = light_artifacts()
    ltext = loader_keys_text()
    lkeys = [us_key(ch) for ch in ltext]
    boot_s = len(keys) * 2 * BOOT_REPORT_MS / 1000 + len(lkeys) * LOADER_KEY_MS / 1000
    full_s = stream_keys(gz) * STREAM_KEY_MS / 1000
    light_s = stream_keys(lgz) * STREAM_KEY_MS / 1000
    source = hashlib.sha256((HEADER_FORMAT + "\0" + small + "\0" + lsmall + "\0" + url + "\0" + ltext).encode("utf-8")).hexdigest()[:16]
    text = f"""// Generated by tools/gen_page_blob.py from sofle-config.html,
// sofle-light.html, bootstrap.html and loader.js - do not edit (rules.mk regenerates it).
// Source {source}
//
// Full page: {len(page)} bytes, minified {len(small)}, gzipped {len(gz)}.
// Light page: {len(lpage)} bytes, minified {len(lsmall)}, gzipped {len(lgz)}.
// Typing time: address + loader {boot_s:.1f} s; first pass: light page {light_s:.1f} s, full page {full_s:.1f} s.

#pragma once

#include <stdint.h>

// One copy in flash, shared by the typed delivery and the web server; the
// arrays are defined in config_page_blob.c.
#define CONFIG_PAGE_GZ_LEN {len(gz)}
extern const uint8_t config_page_gz[CONFIG_PAGE_GZ_LEN];

#define CONFIG_LIGHT_GZ_LEN {len(lgz)}
extern const uint8_t config_light_gz[CONFIG_LIGHT_GZ_LEN];

// The bootstrap data: URL as HID usages for a US layout; | 0x80 = with Shift.
#define CONFIG_BOOTSTRAP_LEN {len(keys)}
extern const uint8_t config_bootstrap_keys[CONFIG_BOOTSTRAP_LEN];

// The loader, typed into the page the address opened (same encoding).
#define CONFIG_LOADER_LEN {len(lkeys)}
extern const uint8_t config_loader_keys[CONFIG_LOADER_LEN];

#ifdef CONFIG_PAGE_BLOB_DEFINE
const uint8_t config_page_gz[CONFIG_PAGE_GZ_LEN] = {{
{c_array(gz)}
}};

const uint8_t config_light_gz[CONFIG_LIGHT_GZ_LEN] = {{
{c_array(lgz)}
}};

const uint8_t config_bootstrap_keys[CONFIG_BOOTSTRAP_LEN] = {{
{c_array(keys)}
}};

const uint8_t config_loader_keys[CONFIG_LOADER_LEN] = {{
{c_array(lkeys)}
}};
#endif
"""
    return text, url, (len(page), len(small), len(gz), len(keys), len(lkeys), boot_s, full_s, len(lgz), light_s)


def main():
    text, url, (raw, small, gz, boot, loader, boot_s, full_s, lgz, light_s) = build()
    if "--print-url" in sys.argv:
        print(url)
        return
    old = OUT.read_text(encoding="utf-8") if OUT.exists() else ""
    # gzip bytes differ between zlib versions: only rewrite when the source changed.
    same_source = re.search(r"// Source \w+", old) and re.search(r"// Source \w+", old).group(0) == re.search(r"// Source \w+", text).group(0)
    if same_source:
        text = old
        # Report what is embedded (this zlib may compress a little differently).
        gz = int(re.search(r"#define CONFIG_PAGE_GZ_LEN (\d+)", old).group(1))
        lgz = int(re.search(r"#define CONFIG_LIGHT_GZ_LEN (\d+)", old).group(1))
    if old != text:
        OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"config_page_blob.h: full page {raw} -> {small} minified -> {gz} gzipped, light page {lgz} gzipped; "
          f"address {boot} + loader {loader} keys; typing ~{boot_s:.0f} s + light ~{light_s:.0f} s (full ~{full_s:.0f} s){'' if old != text else ' (unchanged)'}", file=sys.stderr)


if __name__ == "__main__":
    main()
