# Minimal CUDA Voice Kernel Design

This design keeps the current CPU synth as the default path and adds a small CUDA path that can be enabled later for high-polyphony + FX workloads.

## Goals

- Preserve low-latency behavior by default.
- Keep voice state in a structure-of-arrays (SoA) layout for coalesced GPU memory access.
- Render into a per-voice scratch buffer, then reduce to a mono output buffer.
- Allow clean fallback to CPU rendering if GPU misses deadlines.

## Data Layout (SoA)

Use one device array per parameter, indexed by `voiceIdx`.

- `phase[voiceCount * oscCount]`
- `freq[voiceCount]`
- `envLevel[voiceCount]`
- `envStage[voiceCount]`
- `filter state arrays...`

Use host mirrors for setup/control updates, and upload only changed ranges per callback tick.

## Kernel Flow

1. `render_voices_kernel`: each thread computes one `(voice, frame)` sample into `voiceBuffer`.
2. `mixdown_kernel`: reduce `voiceBuffer` across voices for each frame into `outMono`.
3. Async copy `outMono` back to host output buffer.

For a minimal version, use one CUDA stream and double buffering.

## Host-Side Runtime Contract

- Preallocate all GPU buffers at startup for `maxVoices` and `maxFrames`.
- No runtime CUDA allocations in the audio callback.
- Audio callback submits work with non-blocking APIs, then checks completion from previous buffer.
- If previous GPU job is not ready by callback deadline, render that block on CPU and increment an overrun counter.

## Suggested Integration

- Add `IAudioRenderer` interface:
  - `render(float* out, unsigned nframes)`
  - `setVoiceParams(...)`
  - `noteOn(...)`, `noteOff(...)`
- Implement `CpuAudioRenderer` (existing logic).
- Add `CudaAudioRenderer` using the minimal kernel pair.

## First CUDA Milestone

- Keep oscillator as simple saw for validation.
- Skip filter/envelope initially or implement envelope only.
- Validate output against CPU path with a small RMS error tolerance.
- Add profiling for:
  - kernel execution time
  - host-device copy time
  - callback deadline misses
