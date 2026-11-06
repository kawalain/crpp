# Spatialization and reverb for short click sounds

Findings from public sources (researched 2026-11-07). *unverified* marks items
that were not confirmed in a primary source; costs marked "estimate" are
arithmetic, not measurements, and must be measured before the design is frozen.

## Recommended architecture

Per voice (dry path): distance gain, air-absorption one-pole low-pass, ITD
delay, far-ear head-shadow one-pole low-pass, constant-power pan. All voices
feed one shared stereo Freeverb-style reverb bus through a distance-dependent
send. Optional early reflections are precomputed per seat pair. FTZ/DAZ is set
once on the audio thread; the output is a float sum followed by a soft limiter.

The listener sits at a fixed seat of a 4x4 grid, so only about 15 relative
positions exist: precompute per-position coefficients (gains, delays, filter
coefficients, send levels) instead of convolving an HRTF per voice
(128-tap HRIR per ear x 48 voices is ~590 MMAC/s, estimate).

## Spatialization

- Constant-power pan: `L = cos(p*pi/2)`, `R = sin(p*pi/2)`, p in [0, 1]; center
  is -3 dB per channel (sources disagree: -3 dB vs -4.5 dB conventions).
  miniaudio's pan is linear, not constant-power.
- Woodworth ITD: `ITD(theta) = (a/c)(theta + sin theta)`, theta = 0 in front;
  with a = 0.0875 m and c = 343 m/s the maximum is ~656 us (estimate; the
  formula was not confirmed from a primary page). Sources disagree on whether it
  is a low- or high-frequency cue.
- Head shadow: first-order filter (Brown and Duda 1998); a cited tau
  `a/(2c)` puts the corner near 1.25 kHz (*unverified* against the paper).
- Fractional delay: per-voice delays are static, so linear interpolation is
  probably sufficient (a judgement; Smith recommends Lagrange for fast-changing
  delays).
- Distance and air absorption (OpenAL Soft `alc/alu.cpp`, `core/context.h`):
  `gain = Ref / lerp(Ref, clamp(d, Ref, Max), Rolloff)`;
  `HF gain *= 0.99426^(distance_m * factor)` (-0.05 dB/m). That is mild for a
  room under 10 m; treat as tunable.

## Reverb

- Freeverb (csound `Opcodes/freeverb.c`): 8 parallel low-pass-feedback combs per
  channel with delays 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 samples at
  44.1 kHz, then 4 series allpasses of 556, 441, 341, 225; the right channel is
  spread by 23 samples; allpass feedback 0.5; comb feedback `room*0.28 + 0.7`;
  damping `damp*0.4`; input gain 0.015. About 100 KB and ~150 flops per stereo
  frame (estimate). Scale delays by `rate / 44100`.
- RT60 of a comb loop: `T60 = -3d / log10(g)`; the shortest Freeverb decay
  (~0.5 s) is close to a small office. Office RT60 targets disagree (0.2-0.5 s,
  0.4-0.6 s, 0.5-1.1 s); choose by listening.
- Shared bus costs one reverb plus a send per voice; per-voice reverb costs 48x.
  Raise the send with distance (OpenAL's room rolloff is the reference).
- Alternatives (Schroeder, FDN, Dattorro): Schroeder keeps comb open-loop gain
  <= ~0.85 and allpasses < ~10 ms; FDN and Dattorro were not verified.
- Early reflections: image-source method for a rectangular room (Allen and
  Berkley 1979); positions depend only on the seat pair, so a few taps per pair
  can be precomputed. Pre-delay: no source found; start at 5-15 ms (heuristic).

## Anti-click, denormals, limiting

- Denormals: set FTZ/DAZ once per audio thread, or add a tiny offset in feedback
  loops and snap states below ~1e-8 to zero. The penalty reported by developers
  is large (anecdotes of 25x or more); no reverb benchmark was found.
- Smooth gain changes with a one-pole smoother, tau ~10-50 ms (judgement);
  fade a stolen voice over ~10-30 ms; a sample cut at a non-zero level clicks
  regardless of the fade.
- Sum in float, apply headroom gain, then a soft limiter with its ceiling just
  below full scale; `tanh` alone is not a limiter.

## Not established

Freeverb's original tuning file, Dattorro's topology and the Brown-Duda
equations were not read; there is no sourced pre-delay figure and no Steam Audio
CPU numbers. Subjective tuning (levels, RT60, damping) is done by ear.
