#!/usr/bin/env python3
"""Reject a vendored validator that differs from the engine's copy of the contract."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
source = root / 'engine/contracts/dictionary/product.py'
# An incomplete checkout is not a validator that drifted. Reporting both as "differs" sends whoever
# reads it looking for a content change that is not there.
if not source.is_file():
    raise SystemExit(f'{source.relative_to(root)} is missing; the engine is vendored in-tree, so this is an incomplete checkout')
if source.read_bytes() != (root / 'scripts/dictionary_product.py').read_bytes():
    raise SystemExit('Dictionary validator differs from the engine contract')
print('Dictionary product validator matches the engine contract')
