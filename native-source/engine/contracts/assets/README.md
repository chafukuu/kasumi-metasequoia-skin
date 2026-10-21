# Runtime assets contract

`assets.json` is the shared inventory for the desktop runtime bundle. Each entry declares
its stable ID, installation path, role, source and profiles. `user` entries name writable
user files and are deliberately excluded from shipping profiles. Dictionary database
format/schema compatibility remains owned by `contracts/dictionary/`.

- `generate.py` produces `assets.h` for C++ resource lookup and helpcode scheme selection.
  This is the live part: roughly twenty engine translation units include that header.
- `product.py` reads the inventory for packaging and verifies the exact ZIP member set,
  individual sizes/digests, contract version, dictionary product and source-commit agreement.

The builder that produced the `engine-assets-desktop.zip` bundle described below is **not** in
this repository; it left with the dictionary pipeline (see [UPSTREAM.md](../../UPSTREAM.md)). The
inventory and its verifier are kept because the header is generated from them and because the
bundle format is still what the release side is checked against.

Run `python3 contracts/assets/generate.py --check` after editing the inventory. CI checks the
generated header.

A consumer of the bundle uses the shared verifier before extraction:

```python
from pathlib import Path
from contracts.assets.product import verify

manifest = verify(Path("engine-assets-desktop.zip"), expected_sha256=product_lock_digest)
```

The expected ZIP digest comes from the platform product lock, not the downloaded manifest.
Verification is read-only and does not extract into application directories. The returned
manifest records the producer commit and Google Pinyin source commit. The bundle carries
source/permission notices without changing the underlying data permissions.

Contract version 2 adds `sc.lm`, the kenlm trigram that scores word-lattice sentences, and its
notice. Because `verify()` compares the manifest's `assets_contract_version` against this
inventory, a bundle built against version 1 no longer verifies: the release side has to produce
one that carries `sc.lm` before a host can accept it. The model itself is not committed — the
`language-model/` source path names the directory where `scripts/build-language-model.ps1`
converts it from the ARPA corpus pinned in `language-model/lock.json`, which is how the Windows
installer obtains it. A bundle producer can use the same script, or its own equivalent, as long
as the bytes match that lock.

`mutable-copy` files are immutable release inputs but writable runtime dictionaries after
installation. `resource` files remain in the resource directory; `user` files remain under
the user's durable data directory. `engine-assets-manifest.json` is versioned separately
from the dictionary database format. Older standalone `dict-*` assets remain supported.
