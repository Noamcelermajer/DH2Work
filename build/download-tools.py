#!/usr/bin/env python3
"""Download pinned build tools and verify hashes before using them."""
import hashlib
import json
from pathlib import Path
import urllib.request

root = Path(__file__).resolve().parent
for entry in json.loads((root / 'tool-manifest.json').read_text()):
    path = root / entry['path']
    path.parent.mkdir(exist_ok=True)
    if path.exists():
        data = path.read_bytes()
    else:
        with urllib.request.urlopen(entry['url'], timeout=90) as response:
            data = response.read()
    if hashlib.sha256(data).hexdigest() != entry['sha256']:
        raise SystemExit('Hash mismatch: ' + entry['path'])
    path.write_bytes(data)
    print('Verified ' + entry['path'])
