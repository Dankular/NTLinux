#!/usr/bin/env python3
"""validate.py - checks a compat-db entries JSON file against
compat-entry.schema.json. Real validation (the `jsonschema` package),
not a hand-rolled approximation.

Usage: python3 validate.py [entries.json ...]   (defaults to seed-entries.json)
"""
import json
import sys
from pathlib import Path

try:
    import jsonschema
except ImportError:
    print("validate.py: the 'jsonschema' package is required (pip install jsonschema)", file=sys.stderr)
    sys.exit(2)

HERE = Path(__file__).parent


def main() -> int:
    schema = json.loads((HERE / "compat-entry.schema.json").read_text())
    files = [Path(a) for a in sys.argv[1:]] or [HERE / "seed-entries.json"]

    ok = True
    for f in files:
        entries = json.loads(f.read_text())
        for entry in entries:
            try:
                jsonschema.validate(entry, schema)
                print(f"OK: {f}: {entry['id']}")
            except jsonschema.ValidationError as e:
                print(f"FAIL: {f}: {entry.get('id', '?')}: {e.message}")
                ok = False
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
