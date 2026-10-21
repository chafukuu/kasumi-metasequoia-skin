# Shared VoiceInput

Reusable C++17 code is in `src/` with public headers in `include/msime/voice/`. The standalone Windows host in `platforms/windows/` must not enter the public targets. The host owns microphone permission, hotkeys, UI, token storage, threading, and committing text; for this product the host is `server/`.

Build `cmake -S voice -B build-voice`, then build and run CTest. Capture is optional, and core engine builds must not acquire voice dependencies. Local Whisper recognition was removed with the vendoring; do not reintroduce it without saying why the cloud path is insufficient. Public code uses `metasequoia::voice`, UTF-8 text and mono 16 kHz finite float PCM. Never open a microphone, send user audio or contact paid providers in automated tests; use synthetic samples and loopback HTTP fixtures.

Preserve imported history and licenses. Network code must release handles on success and failure, bound responses, check status, support cancellation and avoid logging response bodies or tokens. Audio device state is per instance. Start/stop are serialized by the host; callbacks must never call stop or destroy the capture object.
