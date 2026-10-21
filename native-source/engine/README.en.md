# Input engine (engine/)

[中文 README](README.md) · [Website](https://msime.app)

The C++ conversion engine behind Metasequoia IME for Windows: input sessions, candidate lookup, helpcodes, the shared contracts and the voice module.

This directory used to be a submodule of `metasequoiaime/MSIME-Engine`. It is now a component of this repository, a sibling of `server/`, `ui/`, `windows/` and `installer/`. Its upstream commit, and exactly what the Windows specialization removed, are recorded in [UPSTREAM.md](UPSTREAM.md). **Treat it as first-party code**: reviewed, formatted and tested like anything else here, no longer merged from upstream.

| Directory | Purpose |
| --- | --- |
| `core/`, `quanpin/`, `shuangpin/`, `english/`, `japanese/` | Input sessions and candidate lookup |
| `contracts/` | The authoritative IPC wire format, dictionary format, punctuation policy and settings-window message schema |
| `helpcode/` | Helpcode (形码) tables and generators |
| `local_modes/`, `providers/`, `user_dictionary/` | Local modes, online candidate sources, the user dictionary |
| `voice/` | Recording, WAV encoding, recognition and text cleanup; builds independently |
| `googlepinyinime-rev/`, `ngram/kenlm/`, `utfcpp/`, `voice/third_party/` | Vendored third-party code, kept in its upstream formatting and licence |

Integration is through `<metasequoia/session.h>`. See [runtime architecture](docs/runtime-architecture.md).

## Build

The product build does not build this directory on its own: `server/CMakeLists.txt` pulls it in with `add_subdirectory`, so CI compiles and exercises the engine through the server and the TSF frontend. After changing something here, run the server's build and tests.

Building it standalone still works and is much faster while working on the engine. Dependencies are CMake 3.25+, a C++17 compiler, Boost, fmt, spdlog and SQLite3.

```powershell
vcpkg install --triplet x64-windows-static
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure --timeout 20
```

`ctest` runs every engine target registered in the root `CMakeLists.txt`: the segmentation, IPC and punctuation contracts, the platform API, data paths, runtime isolation, dictionary state and the per-mode session tests. `tests/` additionally holds a separate Windows-only project that is **not** part of that build — add new tests to the registered targets so they actually run.

Formatting is driven from the repository root by `scripts/format.sh`, using the `.clang-format` in this directory (`voice/` has its own with a wider column limit). The vendored third-party trees and the generated headers are excluded.

## Contract rules

The IPC wire format, opcodes, voice framing and settings-window message definitions live in `contracts/` and are the single source of truth. Consumers include the same headers; do not restate them on either side. Dictionary naming is defined by `contracts/dictionary/format.json`, shared with the repository root's `scripts/dictionary_product.py`.

## Data and licensing

The dictionaries are neither stored nor built here. They arrive as a release pinned by tag and digest in `product-lock.json` and are downloaded at build time; the pipeline that produced them left with the specialization. Per-source licensing is tracked in [NOTICE.md](NOTICE.md) — read it before redistributing the built databases.

Recognition goes through a cloud provider; local Whisper inference was removed. The only committed model is `voice/assets/models/silero_vad.onnx`, loaded solely by the standalone Windows host that is off by default.

## Licence

GPL-3.0. Vendored `googlepinyinime-rev`, `ngram/kenlm` (LGPL-2.1-or-later), `utfcpp` and `voice/third_party/miniaudio` keep their upstream licences.
