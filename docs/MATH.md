# The maths behind BoringSynth

What each part of the plug-in calculates, and why, with the formulas the code uses. It's for reading alongside the source: each section names the file where the maths lives. How the parts fit together, and the decisions behind them, are in [DESIGN.md](DESIGN.md). When a calculation in the code changes, update this file in the same commit.

Nothing here needs more than school algebra and a little trigonometry. The few ideas that go further (Fourier series, the FFT) are explained where they come up.

## Contents

1. [Conventions](#1-conventions)
2. [Pitch](#2-pitch)
3. [Sampling, Nyquist and aliasing](#3-sampling-nyquist-and-aliasing)
4. [Wave tables and band-limiting](#4-wave-tables-and-band-limiting)
5. [Making the waveforms equally loud: RMS](#5-making-the-waveforms-equally-loud-rms)
6. [A note's level](#6-a-notes-level)
7. [Envelopes](#7-envelopes)
8. [The filter](#8-the-filter)
9. [The LFO](#9-the-lfo)
10. [Smoothing and crossfades](#10-smoothing-and-crossfades)
11. [Knob curves](#11-knob-curves)
12. [Not built: aliasing on purpose](#12-not-built-aliasing-on-purpose)

## 1. Conventions

- $f_s$ is the **sample rate**: how many samples the host asks for each second, typically 44,100 or 48,000.
- **Phase** is where a wave is in its cycle, in radians: $0$ at the start, $2\pi$ after one full cycle. An oscillator keeps a phase and moves it on by a fixed **phase step** $\Delta$ every sample.
- **Decibels** compare levels: a gain $g$ is $20 \log_{10} g$ dB. So ×2 is about +6 dB, ×10 is +20 dB, ×0.707 is −3 dB and ×0.25 is −12 dB.
- **Octaves** measure pitch and cutoff: moving by $x$ octaves multiplies a frequency by $2^x$. Ears hear pitch this way, so equal steps in octaves sound like equal steps. That's why the cutoff, the filter envelope and key tracking all work in octaves.
- A **semitone** is $\tfrac{1}{12}$ of an octave: a frequency ratio of $2^{1/12} \approx 1.0595$.

## 2. Pitch

*`SynthVoice.cpp`, `Oscillator.h`*

**Note to frequency.** MIDI numbers the keys, middle C being 60 and the A above it 69, tuned to 440 Hz. Every semitone multiplies the frequency by $2^{1/12}$:

$$f = 440 \cdot 2^{(n - 69)/12}$$

**Frequency to phase step.** One cycle is $2\pi$ radians, and the wave must complete $f$ cycles in $f_s$ samples:

$$\Delta = \frac{2\pi f}{f_s}$$

A4 at 48 kHz moves $0.0576$ radians per sample, taking about 109 samples per cycle. The oscillator adds $\Delta$ to its phase each sample and subtracts $2\pi$ whenever it passes it.

**Pitch bend.** The MIDI pitch wheel sends 0 to 16,383, centred on 8,192. That becomes $b$ from −1 to 1, each half scaled separately (8,192 steps below the centre, 8,191 above) so both ends reach exactly ±1. With a bend range of $R$ semitones the pitch is multiplied by

$$2^{\,bR/12}$$

which glides over 10 ms, multiplicatively (evenly in pitch), because many keyboards send the wheel in coarse steps.

**The LFO on pitch** multiplies the pitch again, by a ratio worked out once per sample for every voice (section 9).

## 3. Sampling, Nyquist and aliasing

*`BandLimitedWaveTable.h`, `Oscillator.h`*

A digital signal only knows the sound at each sample. The highest frequency it can represent is half the sample rate, the **Nyquist frequency** $f_s/2$ (22,050 Hz at 44.1 kHz). A frequency above it doesn't vanish: it **aliases**, folding back below Nyquist as if mirrored there. A tone at $f$ between $f_s/2$ and $f_s$ is heard at

$$f_\text{alias} = f_s - f$$

and in general at the distance from $f$ to the nearest whole multiple of $f_s$. At 44.1 kHz, a harmonic at 25 kHz is heard at 19.1 kHz, and one at 40 kHz at 4.1 kHz.

Aliased tones land at frequencies unrelated to the note, so they sound wrong: metallic and inharmonic. They also move the wrong way: bend a note up and its aliases slide down. So BoringSynth makes sure nothing it generates ever goes above Nyquist:

- Each note reads a version of its waveform with only the harmonics that fit (section 4).
- A note whose fundamental reaches Nyquist ($\Delta \ge \pi$) is silent rather than aliased. Its phase holds still, so it carries on smoothly if the pitch comes back down.
- The filter's cutoff is kept at or below $0.45 f_s$ (section 8).

## 4. Wave tables and band-limiting

*`WaveTable.h`, `BandLimitedWaveTable.cpp`, `Waveforms.cpp`*

### Reading a wave from a table

Computing a waveform from scratch every sample is slow, so each one is stored as a **wave table**: one cycle sampled at $N = 2048$ points. To read it at phase $\varphi$, the oscillator finds the position $p = \varphi \cdot N / 2\pi$ in the table and **interpolates linearly** between the two samples either side:

$$y = s[i] + (p - i)\,(s[i+1] - s[i]), \qquad i = \lfloor p \rfloor$$

The table stores one extra sample, a copy of the first, so $s[i+1]$ always exists. For a sine, the worst error of this is about $1.2 \times 10^{-6}$ of full scale, about −118 dB: far below hearing.

### The shapes

With phase $\varphi$ from $0$ to $2\pi$, each shape starts at 0, rising:

| Waveform | Formula | Jumps |
|---|---|---|
| Sine | $\sin \varphi$ | None |
| Triangle | $\tfrac{2}{\pi} \arcsin(\sin \varphi)$ | None (corners only) |
| Saw | $\varphi/\pi$ below $\pi$, then $\varphi/\pi - 2$ | From 1 to −1 at $\varphi = \pi$ |
| Square | 1 below $\pi$, −1 from $\pi$ | At $0$ and $\pi$ |

A sample landing exactly on a jump takes its midpoint, 0, which is also where band-limiting settles.

### Harmonics

Any repeating wave is a sum of sines at whole multiples of its frequency, its **harmonics** (a Fourier series). A note at 440 Hz has harmonics at 440, 880, 1,320 Hz and so on. Their strengths are what make a saw sound unlike a square:

| Waveform | Harmonic $n$'s amplitude |
|---|---|
| Sine | Only $n = 1$ |
| Saw | $\dfrac{2}{\pi n}$, every $n$ |
| Square | $\dfrac{4}{\pi n}$, odd $n$ only |
| Triangle | $\dfrac{8}{\pi^2 n^2}$, odd $n$ only (much weaker high up, which is why it sounds soft) |

### Band-limiting

A saw has harmonics forever, so a high note's harmonics go past Nyquist and alias. The fix is to remove them before they're played. When the plug-in loads, each waveform is turned into a ladder of 11 tables, its **levels**:

1. An **FFT** (fast Fourier transform) splits the 2,048-sample cycle into its harmonics. A table of 2,048 samples can hold harmonics 1 to 1,023.
2. For each level, every harmonic above the level's limit is set to zero. Level 0 keeps all 1,023; level $k$ keeps at most $1024 / 2^k$: 512, 256, … down to level 10, which keeps only the fundamental.
3. An inverse FFT turns what's left back into a waveform.

**Choosing a level.** For a phase step $\Delta$, harmonic $h$ is at Nyquist when $h\Delta = \pi$, so the harmonics that fit are those up to

$$h_\text{fit} = \left\lceil \frac{\pi}{\Delta} \right\rceil - 1$$

and the note reads the richest level whose limit is no more than that. For A4 at 48 kHz, $\pi/\Delta = 54.5$, so 54 harmonics would fit; level 5 keeps 32, the most that's no more than 54. Its highest harmonic is at $32 \times 440 = 14{,}080$ Hz.

The levels halve, so a note may lose up to half the harmonics that would fit: A4 misses harmonics 33 to 54 (14.5 to 23.8 kHz). That's the price of a small ladder. Those harmonics are faint (a saw's 40th is $\tfrac{1}{40}$ of its fundamental, −32 dB) and at the top of hearing.

The ladder costs 4 waveforms × 11 levels × 2,049 samples × 4 bytes, about 350 KB, built once and shared by every voice.

### Overshoot at the jumps

Cutting off a wave's harmonics makes it ripple next to each jump and overshoot it, by about 9 % of the jump's size, however many harmonics are kept. This is the **Gibbs phenomenon**. A saw or square jumps by 2, so its band-limited version peaks near 1.18, not 1. That's exact maths, not a fault: it's what a perfectly band-limited jump looks like.

## 5. Making the waveforms equally loud: RMS

*`Waveforms.cpp` (`waveformGain`)*

At the same peak, a square sounds far louder than a sine: it sits at its peak the whole time. Loudness follows a wave's power far better than its peak. The usual measure is the **RMS**, the root of the mean of the squares:

$$\text{RMS} = \sqrt{\frac{1}{T} \int_0^T y(t)^2 \, dt}$$

Square every value (so negatives count too, and big values count more), average over a cycle, and take the square root. For the four shapes, each with a peak of 1:

| Waveform | Mean of $y^2$ | RMS |
|---|---|---|
| Square | 1 (it's always ±1) | $1$ |
| Sine | $\tfrac{1}{2}$ | $1/\sqrt{2} \approx 0.707$ |
| Saw, triangle | $\tfrac{1}{3}$ (every value from −1 to 1 equally often) | $1/\sqrt{3} \approx 0.577$ |

`waveformGain` scales each waveform by the sine's RMS divided by its own, so all four have an RMS of 0.707:

| Waveform | Gain | Peak afterwards |
|---|---|---|
| Sine | 1 | 1 |
| Triangle | $\sqrt{1.5} \approx 1.22$ | 1.22 |
| Saw | $\sqrt{1.5} \approx 1.22$ | 1.44 (1.22 × its 1.18 overshoot) |
| Square | $\sqrt{0.5} \approx 0.71$ | 0.83 (0.71 × its 1.18 overshoot) |

The sine keeps a gain of exactly 1, so it's unchanged from before the other waveforms existed.

**Why one gain suits every level.** A wave's power is the sum of its harmonics' powers, each being half its amplitude squared (Parseval's theorem). For the saw:

$$\sum_{n=1}^{\infty} \frac{1}{2} \left( \frac{2}{\pi n} \right)^2 = \frac{2}{\pi^2} \sum_{n=1}^{\infty} \frac{1}{n^2} = \frac{2}{\pi^2} \cdot \frac{\pi^2}{6} = \frac{1}{3}$$

which matches the table. The harmonics band-limiting removes are the weakest, carrying little of that sum: even keeping only 32 harmonics leaves 98 % of the saw's power. So the RMS hardly changes from level to level.

Equal RMS is only roughly equal loudness: ears are most sensitive around 2 to 5 kHz, where a saw has energy and a sine has none. It's the standard simple approximation.

## 6. A note's level

*`SynthVoice.cpp`, `BoringSynthProcessor.cpp`*

**Velocity.** A key's velocity $v$ (0 to 1) and the Velocity Sensitivity $s$ (0 to 1) set the note's gain:

$$g = (1 - s) + s v^2$$

At $s = 1$ this is the General MIDI curve, velocity squared: half the velocity is a quarter of the level, −12 dB. At $s = 0$, velocity is ignored. The hardest hit always gets full level.

**Per-note level.** Every note is multiplied by 0.1, a sine's peak, so eight sines peaking together reach 0.8. Other waveforms peak higher or lower (section 5).

**The sample.** Each sample of a note is

$$y = e_\text{amp} \cdot 0.1 \cdot g \cdot g_\text{LFO} \cdot \text{filter}(\text{oscillator})$$

with $e_\text{amp}$ the amp envelope (0 to 1) and $g_\text{LFO}$ the LFO's tremolo (1 unless the LFO is on the volume). The voices are summed.

**Volume** in dB becomes a gain $10^{\text{dB}/20}$, with −60 dB meaning silence (a gain of exactly 0). It glides over 20 ms, linearly in gain rather than in dB, since multiplicative gliding could never reach 0.

**The output guard** then replaces any NaN or infinity with 0 and clips anything past ±1.

## 7. Envelopes

*`Envelope.h`*

The amp and filter envelopes are **ADSR**: attack, decay, sustain, release, each stage a straight line. Each sample adds or subtracts a fixed step:

| Stage | Step per sample |
|---|---|
| Attack, from 0 to 1 over $A$ seconds | $+\dfrac{1}{A f_s}$ |
| Decay, from 1 to the sustain level $S$ over $D$ seconds | $-\dfrac{1 - S}{D f_s}$ |
| Sustain | 0, or gliding to a changed $S$ over 20 ms |
| Release, from wherever the level is, to 0 over $R$ seconds | $-\dfrac{\text{level}}{R f_s}$ |

The release step is worked out from the level the note is at when the key is let go, so the release always lasts exactly $R$ whatever stage it starts from. An attack or decay of 0 takes a single sample. The shortest release is 5 ms: short enough to sound instant, long enough not to click.

## 8. The filter

*`Filter.h`, `SynthVoice.cpp`*

### Where the cutoff ends up

The cutoff knob sets $f_c$ in Hz. Key tracking $t$ (0 to 1), Filter Velocity $V$ (octaves) and the note's velocity $v$ move it, in octaves:

$$f_\text{note} = f_c \cdot 2^{\, t (n - 60)/12 \; - \; V (1 - v)}$$

At 100 % tracking, a note an octave above middle C opens the filter an octave further. The softest notes are darkened by $V$ octaves and the hardest not at all. The result is kept between 20 Hz and 20 kHz.

Then, each sample, the filter envelope $e_\text{filter}$ (0 to 1) times its amount $E$ (−8 to +8 octaves) and the LFO's effect $L$ (octaves) move it again:

$$f = f_\text{note} \cdot 2^{\, E e_\text{filter} + L}$$

kept within 20 Hz to 20 kHz, and below $0.45 f_s$ where the filter can't work.

### Resonance

Resonance $r$ (0 to 1) becomes the filter's **Q**, spread evenly on a log scale from 0.707 (flat, no peak) to 10 (a +20 dB peak):

$$Q = 0.707 \cdot \left( \frac{10}{0.707} \right)^r$$

### The filter itself

It's a **state-variable filter** in the "topology-preserving transform" form described by Andrew Simper (Cytomic): two integrators in a loop, which give low-pass, band-pass and high-pass outputs from one calculation. Its coefficients come from the cutoff and Q:

$$g = \tan\!\left( \frac{\pi f}{f_s} \right), \quad k = \frac{1}{Q}, \quad a_1 = \frac{1}{1 + g(g + k)}, \quad a_2 = g a_1, \quad a_3 = g a_2$$

The $\tan$ "pre-warps" the cutoff, so it lands exactly where it's set even near Nyquist. Each sample, with input $x$ and the filter's two remembered values $c_1, c_2$:

$$
\begin{aligned}
v_3 &= x - c_2 \\
\text{band} &= a_1 c_1 + a_2 v_3 \\
\text{low} &= c_2 + a_2 c_1 + a_3 v_3 \\
\text{high} &= x - k \cdot \text{band} - \text{low} \\
c_1 &\leftarrow 2\,\text{band} - c_1, \qquad c_2 \leftarrow 2\,\text{low} - c_2
\end{aligned}
$$

It stays stable at any cutoff and Q, even when they change every sample. All three outputs have a gain of exactly $Q$ at the cutoff: −3 dB with no resonance, up to +20 dB at full resonance. Low-pass and high-pass fall 12 dB per octave away from the cutoff, band-pass 6 dB per octave either side.

**Cost.** The $\tan$ is the most expensive calculation in the plug-in. While the cutoff moves, the coefficients are worked out every 8 samples (0.17 ms at 48 kHz) rather than every sample, plus once more when it stops, so it always lands exactly. The steps are far too small and fast to hear.

## 9. The LFO

*`Lfo.h`, `Synth.cpp`*

One LFO serves every voice. It has the same four shapes as the oscillator, but calculated directly rather than read from a table, each exactly within ±1 and starting at 0, rising.

### Its jumps are ramps

On the volume or the cutoff, the saw's and square's instant jumps would click. So each jump becomes a straight ramp lasting 5 ms whatever the LFO's rate: $w = \Delta \cdot 0.005 f_s$ radians of the cycle, centred on where the jump would be. The saw's rise is steepened slightly to make room: it climbs from −1 to 1 over $2\pi - w$ instead of $2\pi$. At 20 Hz, the fastest rate, the ramps take a tenth of the cycle each.

### Depth, per destination

With depth $d$ (0 to 1) and the LFO's value $u$ (−1 to 1):

| Destination | Effect |
|---|---|
| Pitch | Multiplies the pitch by $2^{\,d^2 u}$: up to an octave either way. Squared, so most of the knob covers the small depths vibrato uses: 20 % is about ½ semitone. |
| Volume | Multiplies the level by $1 - \tfrac{d}{2}(1 - u)$: full level at the LFO's peak, down to $1 - d$ at its trough. Never above 1. |
| Filter cutoff | Moves it by $4 d u$ octaves. |

The mod wheel $m$ (0 to 1) adds to the depth: $d = \min(1, d_\text{knob} + d_\text{wheel} \cdot m)$.

### Sync

Synced, the rate is a note length of $q$ quarter notes (a quarter note is 1, a bar of 4/4 is 4, a dotted eighth $\tfrac{3}{4}$) at the host's tempo:

$$f_\text{LFO} = \frac{\text{BPM}/60}{q}, \text{ at most 50 Hz}$$

While the song plays, the LFO's phase follows the song's position $P$ (in quarter notes):

$$\varphi = 2\pi \cdot \operatorname{frac}\!\left( \frac{P}{q} \right)$$

If it drifts more than $10^{-4}$ of a cycle from there (only a loop, a jump or starting playback does that), it fades to the right place over 5 ms.

## 10. Smoothing and crossfades

*Throughout*

Anything that would jump, and so click, glides or crossfades instead, almost always in a straight line over a fixed time:

| What | How | Time |
|---|---|---|
| Volume, LFO depth, resonance, filter envelope amount | Linear glide | 20 ms |
| Cutoff, pitch bend | Multiplicative glide (evenly in octaves) | 20 ms, 10 ms |
| Waveform, filter type, LFO shape, LFO destination, filter on/off | Linear crossfade from old to new | 10 ms |
| A stolen voice's old note | Linear fade to silence | 5 ms |
| LFO retrigger, LFO sync correction | Linear crossfade from the old wave to the restarted one | 5 ms |

A linear crossfade over $N$ samples outputs $y = y_\text{old} + \tfrac{i}{N}(y_\text{new} - y_\text{old})$ on sample $i$. The fades count whole samples, so they end exactly on their last one, whatever the host's block size.

## 11. Knob curves

*`Parameters.cpp`*

A knob from 20 Hz to 20 kHz turned evenly would spend almost all its travel above 1 kHz. So the time, cutoff and LFO rate knobs are **skewed**: a knob position $p$ (0 to 1) gives

$$\text{value} = \min + (\max - \min) \cdot p^{1/s}$$

with $s$ chosen so the knob's middle lands on a chosen value. For the cutoff that's 632 Hz, $\sqrt{20 \times 20000}$, the geometric middle of the range. Envelope times centre on 0.5 s, the LFO rate on 1 Hz.

## 12. Not built: aliasing on purpose

Section 3 explains why BoringSynth avoids aliasing. Some lo-fi sounds, early samplers and game consoles above all, are made of it, so it's worth noting how it could be invited back. None of this exists; it would be off by default, as every new feature is.

- **A naive oscillator.** Always read level 0, with all 1,023 harmonics. Low notes barely change; high notes' harmonics fold back below Nyquist as a gritty haze, and slide the wrong way under pitch bends. Its sound depends on the host's sample rate: the same note aliases differently at 44.1 and 96 kHz.
- **Sample-rate reduction.** Pretend the sample rate is a lower $f_s'$: keep one sample every $f_s / f_s'$ (a fraction is fine, using a phase counter) and hold it until the next. Everything above $f_s'/2$ folds back around it, and the holding adds mirror-image copies above $f_s'/2$, the buzzy sheen of old 8- and 12-bit samplers. A "Rate" knob could sweep it, or the LFO move it. Placed after the voices are mixed, it costs almost nothing, and it sounds about the same at any host sample rate.
- **Notes past Nyquist.** Let a note's fundamental go above $f_s/2$ instead of falling silent, and it folds back as a lower note. Sweep the pitch upwards through Nyquist and it turns round and comes back down. A curiosity more than an instrument.
