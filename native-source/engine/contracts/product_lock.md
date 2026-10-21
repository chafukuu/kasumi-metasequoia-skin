# Product lock primitives

`product_lock.py` is the shared implementation for the security-sensitive parts of the product
lock: SHA256 verification, dictionary manifest provenance, published checksum parsing, atomic
downloads with retries and annotated/lightweight tag resolution.

`scripts/product_lock.py` keeps the product-specific asset set, lock schema and command-line
interface, and vendors this file at `scripts/product_lock_shared.py` as a thin wrapper's
dependency. `scripts/check-product-lock-contract.py` compares the two byte-for-byte on every CI
run, so the copy cannot quietly drift from this one:

```sh
cmp scripts/product_lock_shared.py engine/contracts/product_lock.py
```

The rule survives the engine moving in-tree for the same reason it existed before. The lock is what
decides whether a downloaded dictionary is the reviewed one; keeping the verification primitives in
a single reviewed file, and making CI prove the copy matches it, is what stops a change to the
downloader from landing in only one of the two places. Edit this file, then re-copy it.
