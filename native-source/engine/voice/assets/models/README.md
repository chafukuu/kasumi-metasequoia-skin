# Voice models

## silero_vad.onnx

The voice activity detector, committed to this repository rather than downloaded. It is executed
in-process, so it is pinned by digest.

| | |
| --- | --- |
| Upstream | [snakers4/silero-vad](https://github.com/snakers4/silero-vad), `src/silero_vad/data/silero_vad.onnx` |
| Version | v6.2 (identical in v6.2.1); introduced by upstream commit `bfdc0193023f` |
| License | MIT |
| Size | 2327524 bytes |
| SHA256 | `1a153a22f4509e292a94e67d6f9b85e8deb25b4988682b7e174c65279d8788e3` |

CI re-checks that digest on every run, so replacing the file without updating this table fails the
build. The check used to live in the engine's own `voice.yml`; it is now a step of the `lock` job in
`.github/workflows/ci.yml`.

Only the imported standalone Windows host (`MSIME_VOICE_WINDOWS_APP`, off by default) loads this
model. The supported VAD in the shipped library is the RMS-based `VadSegmenter`, which needs no
model at all.

## Whisper models

There are none. Local Whisper inference was removed when the engine was vendored into this
repository -- this product transcribes through a cloud provider. See
[UPSTREAM.md](../../../UPSTREAM.md).
