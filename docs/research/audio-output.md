# Audio output: resampling, WASAPI, WAV, mixing

Findings from public sources (researched 2026-11-07). Items marked *unverified*
were not confirmed in a primary source and need a test before relying on them.

## Design that the sources support

1. Resample each sample once at load time to the shared-mode mix rate with a
   small Kaiser-windowed-sinc kernel written in the app.
2. Open a shared-mode stream with `IAudioClient3::InitializeSharedAudioStream`
   (mix format, `AUDCLNT_STREAMFLAGS_EVENTCALLBACK`, the smallest period that
   `GetSharedModeEnginePeriod` allows). Do not rely on `AUTOCONVERTPCM`: the
   documented flags of that call do not include it. The implementation departs
   from this on purpose and uses the default period: this project puts CPU
   first, and network delay dwarfs what a smaller period would save.
3. Event-driven render thread registered with MMCSS "Pro Audio"; write silence
   on underrun.
4. Stop the client when idle (a small period wakes the CPU every period);
   keep the initialised client for quick restarts and release it after an idle
   timeout (the timeout value is a design choice, not sourced).
5. React to device changes through `IMMNotificationClient`, whose callbacks
   must only set a flag or post a message, never block or release devices.

## Resampling

- Ideal band-limited interpolation: `h(t) = min(1, Fs'/Fs) * sinc(min(Fs, Fs') * t)`
  (J. O. Smith, ccrma.stanford.edu/~jos/resample/Theory_Ideal_Bandlimited_Interpolation.html).
- Kaiser window: `w(n) = I0(beta * sqrt(1 - (n/(M/2))^2)) / I0(beta)`, with
  `I0(x) = sum_k ((x/2)^k / k!)^2` (ccrma.stanford.edu/~jos/sasp/Kaiser_Window.html).
- Filter design (use these, not the window-design constants on the same page):
  `beta = 0.1102 (A - 8.7)` for A > 50 dB; order `M = (A - 8) / (2.285 * dw)`
  (ccrma.stanford.edu/~jos/sasp/Hood_kaiserord.html). Example: A = 96 dB at
  48 kHz with a 2 kHz transition gives beta ~ 9.6 and ~150 taps.
- Direct evaluation at load time: `y[m] = sum_k x[k] c sinc(c (t - k)) w((t - k)/H)`,
  `t = m * fin / fout`, `c = min(1, fout/fin)`, kernel normalised to unity DC
  gain. About 2H multiply-adds per output sample.
- libsamplerate documents linear and zero-order-hold as not band-limited; its
  sinc modes reach ~97 dB SNR (libsndfile.github.io/libsamplerate/api_misc.html).
  Cubic interpolation: no primary source found (*unverified*).

## WASAPI

- `GetSharedModeEnginePeriod` returns default/fundamental/min/max periods in
  frames; the requested period must be a multiple of the fundamental. Example
  from Microsoft at 44.1 kHz: default 448, fundamental 4, min 48 frames.
- The engine period is shared by all apps on the endpoint: read it back with
  `GetCurrentSharedModeEnginePeriod` instead of assuming the request was met.
  Inbox HDAudio supports 128-480 samples at 48 kHz (2.66-10 ms); drivers vary.
- Event mode: `SetEventHandle` after initialisation and before `Start`; per
  wake-up, `GetCurrentPadding` and write up to the free space; shared mode
  uses 0 for periodicity and buffer duration in `Initialize`.
- Low latency raises power use; Windows leaves its special resource mode when
  streaming stops. `Stop` then `Start` on an initialised client is valid.
- MMCSS: "Pro Audio" via `AvSetMmThreadCharacteristics` on the render thread
  (the reference page itself was not fetched).

## WAV

- RIFF `WAVE`: `fmt ` and `data` chunks, chunks padded to an even size (the pad
  byte is not counted in the chunk size; tolerate a missing pad). Clamp chunk
  sizes to the file size; check `nBlockAlign = channels * bits / 8`.
- `WAVE_FORMAT_EXTENSIBLE`: `cbSize >= 22`, container size in `wBitsPerSample`
  (multiple of 8), real type in the `SubFormat` GUID (PCM or IEEE float).
- Normalise by container size: int16 / 32768, int24 / 8388608,
  int32 / 2147483648, float as is.

## Mixing

Sum float voices, apply master gain, then soft-clip at the output (tanh above a
threshold of 0.8-0.9, or `x / (1 + |x|)`). The thresholds are unsourced design
choices to be tested.

## Open questions

Kaiser length convention (`+1` or not), exact first-sound latency of open vs.
stopped streams, and the minimum period on the target devices are not
established.
