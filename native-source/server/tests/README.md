# Tests

This project now includes a lightweight regression test target wired into the main CMake build.

## What It Covers

Current tests focus on high-value IME behaviors that are easy to accidentally break during refactors:

- `shuangpin` scheme request construction
- `quanpin` scheme request construction
- case-preserving shuangpin segmentation
- full-helpcode detection
- `engine + shuangpin` continuation behavior
- cloud suggestion trigger timing for shuangpin

The goal is not to fully simulate the whole IME UI pipeline yet. The goal is to protect the core input rules and
session behavior.

## Build and run

Run these commands from the **MSIME-Windows repository root** after preparing
vcpkg and Boost with `python server/scripts/prepare_env.py`:

```powershell
python scripts/product_lock.py fetch-dictionaries --staging-root .
if ($LASTEXITCODE -ne 0) { throw 'Dictionary verification failed' }
$env:LOCALAPPDATA = Join-Path $PWD 'build/test-data/user-local'
$env:METASEQUOIA_IME_DATA_DIR = Join-Path $env:LOCALAPPDATA 'metasequoiaime'
New-Item -ItemType Directory -Force $env:METASEQUOIA_IME_DATA_DIR | Out-Null
Copy-Item MetasequoiaImeDict/out/* $env:METASEQUOIA_IME_DATA_DIR -Force
Copy-Item engine/helpcode/helpcodes $env:METASEQUOIA_IME_DATA_DIR -Recurse -Force
Copy-Item server/assets/tables/* $env:METASEQUOIA_IME_DATA_DIR -Force
Copy-Item server/assets/config/config.toml $env:METASEQUOIA_IME_DATA_DIR -Force
.\server\tests\scripts\llaunch.ps1 -Configuration Release
```

Use a dedicated PowerShell terminal: these environment variables isolate both
Windows helpers and the shared Engine from the installed user's data for the
rest of that session. The scripts run CTest; they do not launch the interactive
Server, install or register the input method. CI additionally runs the native
Win32/x64 pipe probe after building the full Server.

Tests belong to the **Server CMake project**, which provides the Engine, GUI and
third-party targets. `server/tests/CMakeLists.txt` is an `add_subdirectory` input,
not an independent project. The obsolete test presets have been removed; an old
locally generated `server/tests/CMakePresets.json` is no longer used.

Once test data is prepared, the convenience scripts work from any directory:

```powershell
.\server\tests\scripts\llaunch.ps1 -Configuration Release  # build, then CTest
.\server\tests\scripts\lrun.ps1 -Configuration Release     # CTest only
```

Omitting `-Configuration` selects Debug in `server/build`; Release uses
`server/build-release`. Both compile `MetasequoiaImeServerTests` and
`test_webview_contract` in the parent build tree. Configuration, compilation,
test failures and an empty CTest registry return failure rather than launching a
stale executable or reporting success.

The equivalent direct CTest command from the repository root is:

```powershell
ctest --test-dir server/build-release -C Release --output-on-failure --no-tests=error
```

## Test Layout

- `tests/src/main.cpp`
  - tiny custom test runner
- `tests/includes/test_framework.h`
  - lightweight assertion helpers
- `tests/src/test_shuangpin_scheme.cpp`
  - shuangpin scheme unit tests
- `tests/src/test_quanpin_scheme.cpp`
  - quanpin scheme unit tests
- `tests/src/test_shuangpin_query.cpp`
  - query/helper function tests
- `tests/src/test_engine_shuangpin_session.cpp`
  - session-level behavior tests

## Model path benchmark

Build the Server test target in Release mode. Run the benchmark from a Windows terminal:

```powershell
cmake --build server/build-release --config Release --target MetasequoiaImeServerTests
server/build-release/bin/Release/MetasequoiaImeServerTests.exe --benchmark-model-path 10000 > model-path.csv
```

The executable creates small model files in a temporary data directory.
It restores `METASEQUOIA_IME_DATA_DIR` when it exits.
It does not load model contents.
Each row reports P50, P95, P99, and worst latency in nanoseconds.

`hot` repeats one path.
`rotating` cycles through 128 paths.
The rotating set increases metadata lookup pressure.
It does not guarantee an operating system cache miss.
`first-touch` checks each missing path once without benchmark warmup.
The operating system can still cache shared parent directories.
`contended` adds a thread that creates and removes files in the model directory.
This tests directory activity on the same volume.
It does not simulate all disk workloads.

The benchmark calls the production path resolvers used by `ApplyConfiguration()`.
It measures their cost per call.
It does not measure full `handle_key()` latency.
The benchmark sets `METASEQUOIA_IME_DATA_DIR` to an isolated temporary directory.
Each resolver call reads this override and builds a path.
Normal installations do not set this override. Their directory lookup takes a different path.
The `disabled-empty-id` row returns before `data_directory()` runs.
The `data-directory-only` row measures `data_directory()` with the temporary override.
Median differences give only a rough scale for the remaining resolver work.
Do not subtract percentiles from separate rows to assign costs to path joining or file checks.
Use the rows to inspect latency distributions and tail latency across path access patterns.
They do not establish the same cost or performance loss in production.
Run each case more than once on the target Windows machine.
Record the storage type and active disk workload with the CSV results.

## Recommended Practice

When you change input behavior, add a regression test if the change affects:

- input segmentation
- space/enter/backspace behavior
- helpcode handling
- continuation after candidate selection
- create-word flow
- cloud suggestion timing

The best tests in this project are the ones that capture a concrete input example that previously broke.
