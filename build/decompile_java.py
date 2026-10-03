#!/usr/bin/env python3
"""Export recovered Java for inspection, not a promise of recompilable source."""
import json
from pathlib import Path
from loguru import logger
logger.disable('androguard')
from androguard.decompiler.decompile import DvMachine, DvClass

root = Path(__file__).resolve().parent
machine = DvMachine(str(root / 'original/classes.dex'))
out = root / 'decompiled-java'
out.mkdir(exist_ok=True)
errors = []
for i, (name, klass) in enumerate(sorted(machine.classes.items())):
    target = out / (name.strip('L;') + '.java')
    target.parent.mkdir(parents=True, exist_ok=True)
    try:
        dv = DvClass(klass, machine.vma)
        dv.process()
        target.write_text('// Decompiled from user-supplied APK. Inspection only.\n' + dv.get_source())
    except Exception as exc:
        errors.append({'class': name, 'error': str(exc)})
    if (i + 1) % 50 == 0:
        print(f'Processed {i + 1}/{len(machine.classes)} classes', flush=True)
(root / 'decompilation-errors.json').write_text(json.dumps(errors, indent=2))
print(f'Java exports: {len(list(out.rglob("*.java")))}; failures: {len(errors)}', flush=True)
