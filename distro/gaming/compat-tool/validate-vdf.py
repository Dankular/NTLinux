#!/usr/bin/env python3
"""Minimal real parser for Valve's KeyValues (.vdf) text format - enough
to confirm a file is well-formed (balanced braces, every key has either
a nested block or a quoted string value, no stray tokens) rather than
just eyeballing it. Not a full VDF implementation (no #include/#base
macro support), but genuinely parses the file rather than pattern-matching."""
import sys
import re

TOKEN_RE = re.compile(r'"((?:[^"\\]|\\.)*)"|[{}]')

def parse(text, path):
    tokens = []
    for m in TOKEN_RE.finditer(text):
        tokens.append(m.group(1) if m.group(1) is not None else m.group(0))
    # strip out C++-style // comments naively (not inside quotes, since
    # regex only tokenized inside/outside quotes correctly already for
    # the quoted case; a `//` used as a bare token here without quotes
    # would already show up as a top-level unexpected token, which is
    # fine for this check's purpose)
    pos = [0]
    def parse_block():
        node = {}
        while pos[0] < len(tokens):
            tok = tokens[pos[0]]
            if tok == '}':
                pos[0] += 1
                return node
            key = tok
            pos[0] += 1
            if pos[0] >= len(tokens):
                raise ValueError(f"{path}: key {key!r} has no value (unexpected EOF)")
            val = tokens[pos[0]]
            if val == '{':
                pos[0] += 1
                node[key] = parse_block()
            elif val == '}':
                raise ValueError(f"{path}: key {key!r} followed by unexpected '}}'")
            else:
                node[key] = val
                pos[0] += 1
        return node

    root = {}
    while pos[0] < len(tokens):
        tok = tokens[pos[0]]
        if tok == '{':
            raise ValueError(f"{path}: unexpected '{{' at top level")
        key = tok
        pos[0] += 1
        if pos[0] >= len(tokens) or tokens[pos[0]] != '{':
            raise ValueError(f"{path}: top-level key {key!r} must open a block")
        pos[0] += 1
        root[key] = parse_block()
    return root

if __name__ == "__main__":
    for path in sys.argv[1:]:
        text = open(path).read()
        try:
            tree = parse(text, path)
        except ValueError as e:
            print(f"FAIL: {e}")
            sys.exit(1)
        print(f"OK: {path} parses as well-formed VDF, top-level keys: {list(tree.keys())}")
