# BoringSynth design notes

How the plug-in is put together, the rules it follows, and why the main decisions were made. Read this before changing anything structural. When a decision here changes, update this file in the same commit.

The step-by-step plan is in [ROADMAP.md](ROADMAP.md).

## How a note becomes sound

1. The DAW calls `BoringSynthProcessor::processBlock` with an empty buffer and the MIDI for that block.
2. JUCE's `Synthesiser` (our subclass `Synth`) splits the block at each MIDI event and assigns notes to voices. It handles note on/off, voice stealing, the pitch wheel and the sustain pedal.
3. Each playing `SynthVoice` asks its `Oscillator` for one sample at a time. The oscillator reads the current waveform's `BandLimitedWaveTable`, choosing the level whose harmonics all stay below Nyquist for the note's current pitch.
4. If the filter is on, the voice runs each sample through its own `Filter` (each voice has one, so steps 16–18 can give each note its own cutoff). Then it scales the sample by the note's level (velocity) and its amp `Envelope`, and adds it to the buffer. The voice ends once the envelope's release has finished. If the voice was just stolen for a new note, it also plays the old note underneath, fading it out over 5 ms (its "fading note"). The output is mono, written to every channel.
5. The processor applies the smoothed Volume, then `protectOutput`: NaN or infinity becomes silence, and anything past full scale is clipped to it. This is always the very last step.

### Who owns what

- The **processor** owns the parameters (`AudioProcessorValueTreeState`), the `WaveformTables` and the `Synth`. The tables are declared **before** the synth, so they outlive the voices that read them.
- **Voices** hold references to the shared tables and a `VoiceParameters` struct of references to the parameters' live values. A new parameter that voices need goes in `VoiceParameters`.
- **Tables are built once**, in the processor's constructor, never on the audio thread.

## Permanent identifiers

DAW projects store these. Changing any of them breaks every project that uses the plug-in, so treat them as permanent. The parameter tests spell the IDs out literally, so renaming one in the code makes a test fail.

| What | Value |
|------|-------|
| Product name | `BoringSynth` (set once, as `BORINGSYNTH_PRODUCT_NAME` in `CMakeLists.txt`) |
| Company / manufacturer code / plug-in code | `Jack` / `Jack` / `Brng` |
| Parameter IDs | `volume`, `velocitySensitivity`, `pitchBendRange`, `waveform`, `ampAttack`, `ampDecay`, `ampSustain`, `ampRelease`, `filterEnabled`, `filterCutoff`, `filterResonance`, `filterType`, `filterKeyTracking`, `filterAttack`, `filterDecay`, `filterSustain`, `filterRelease`, `filterEnvAmount` |
| Waveform choices, in order | `Sine`, `Triangle`, `Saw`, `Square`. Saved by position, so new waveforms may only be **added at the end**. |
| Filter type choices, in order | `Low-pass`, `High-pass`, `Band-pass`. Saved by position, so new types may only be **added at the end**. |
| Saved state | XML with tag `Parameters` and attribute `stateVersion="1"` |

**Changing the saved state:**
- **Adding a parameter** needs no version bump. Older projects simply don't have it, and it loads at its default. The default should reproduce how the plug-in sounded before the parameter existed.
- **Renaming, removing or reinterpreting** anything saved means bumping `currentStateVersion` and converting older states when they load.
- **Loading ignores** empty data, garbage, and XML from anything else, leaving the parameters as they are.

A parameter's **range, display text and smoothing can change freely**, because projects store values in the parameter's own units, not knob positions.

## Real-time rules

The audio thread (everything called from `processBlock`) must never:

- **allocate or free memory**. No `new`, no growing containers. Voices and tables all exist before audio starts.
- **wait on a lock** that another thread might hold. JUCE's `Synthesiser` takes its own lock, but nothing else ever holds it, so it never waits.
- **touch files, the network, or the UI**.

Parameters reach the audio thread through `getRawParameterValue`'s lock-free atomics, read with `load()`.

Anything that needs real work (building tables now, loading wavetable files later) happens at load time or on a background thread. Results are handed to the audio thread without it waiting or allocating.

## Behaviour the tests protect

- **New features keep the old sound at their defaults.** Each step is checked with the reference render (`Tests --record` / `--compare`).
- **Output doesn't depend on the host's block size.** The same MIDI gives identical output in blocks of 1, 7 or 512 samples.
- **Timing is sample-accurate**: `setMinimumRenderingSubdivisionSize (1)`.
- **Every waveform is zero at phase 0**, where notes start, so notes start without a click.
- **No aliasing**: notes read band-limited tables, and anything at or above Nyquist is silent.
- **Levels**: each note is 0.1 at full scale. Eight notes peaking together stay below full scale, and the output guard stops anything louder from exceeding it.

## Decisions and why

| Decision | Why |
|----------|-----|
| JUCE coding standards, plus a blank line inside every namespace's braces | Matches JUCE's own code. The blank lines keep namespace braces from looking like class braces. |
| 16 voices, all created at load | Bounds the worst-case CPU no matter how much MIDI arrives, and never allocates. Notes past 16 steal a voice. A Polyphony parameter would choose how many of a preallocated maximum may play. |
| Volume −60 to +6 dB; the bottom is silence | A parameter needs finite ends. −60 dB is effectively inaudible in a mix and keeps knob resolution where it matters. The range or a skew can change later without breaking projects. |
| Volume smoothed **linearly in gain**, over 20 ms | Multiplicative (dB-style) smoothing can never reach a gain of exactly 0. |
| Velocity curve: (velocity / 127)² at 100 % sensitivity, blended toward 1 below that | The General MIDI / DLS curve. Velocity 127 is always full level, so turning sensitivity up only makes softer notes quieter. |
| Velocity sensitivity defaults to 0 % | Keeps the original "velocity ignored" sound. |
| Pitch bend range defaults to ±2 semitones | The common standard. This deliberately breaks the "default to off" rule, because a synth that ignores the wheel feels broken. |
| Bends glide over 10 ms, smoothed multiplicatively | Smooths coarse wheel steps while still feeling immediate. Multiplicative smoothing makes the glide even in pitch. |
| Sustain pedal on, sostenuto ignored | Sustain is on nearly every keyboard. Most synths ignore sostenuto, and JUCE's version also holds notes whose keys are already up. |
| Wave tables of 2048 samples, linear interpolation | Within 1.2 × 10⁻⁶ of a true sine (−118 dB), far below audibility, and cheap. |
| 11 band-limited levels an octave apart (1023, 512, … 1 harmonics), built with an FFT | Standard "mipmapped" wavetables. **Tradeoffs:** a note can lose up to its top octave of harmonics (the quietest ones), and bending across a level boundary changes brightness in a step. |
| Waveforms matched to the sine's RMS | Simple, explainable loudness matching. Triangle and saw are scaled by √1.5, square by √0.5, sine by exactly 1, so the sine stays bit-for-bit unchanged. |
| Waveform changes during a note crossfade over 10 ms, counted in whole samples | Avoids a click at the switch. Whole samples make the fade end on exactly its last sample, which floats didn't. |
| Our own `Envelope` class rather than `juce::ADSR` | `juce::ADSR` jumps when the sustain level changes mid-note. Updating its settings every block (needed so knob moves reach held notes) also resets the release speed from the sustain level, so a note released early fades too fast, and with a sustain of 0 it cuts off instantly. Ours glides sustain changes over 20 ms, always releases from the note's current level over the release time, and takes settings every block. Each case has a test. |
| Envelope defaults: attack 0, sustain 100 %, release 5 ms (decay 200 ms) | Reproduces the old fixed 5 ms fade bit for bit. An instant attack is click-free because every waveform starts at zero. Decay only matters once sustain is lowered. |
| Decay and release can't go below 5 ms | So the envelope can never jump in level and click. |
| Envelope parameter IDs start with `amp` | A filter envelope with its own attack, decay, sustain and release comes in step 17, and IDs can't be renamed later. |
| Time knobs 0 or 5 ms up to 10 s, skewed so the middle is 0.5 s | Most of the knob's travel goes to short times, where small changes matter most. |
| Tail length reported to the DAW is the release time | So the DAW keeps processing until notes have finished fading out. |
| A stolen voice fades its old note out over 5 ms, underneath the new note | JUCE's synth stops a stolen voice with no tail-off, and stopping a sounding note dead clicks. The voice keeps a snapshot of the old note (oscillator, pitch, level) in a slot that's part of the voice, so nothing is allocated, and fades it for the shortest release time. Stealing is the only way a note gets cut off; JUCE handles MIDI "all sound off" with a normal release. |
| A filter on every voice, not one on the output | With identical settings, one filter on the mix would sound the same, since the filter is linear. But key tracking, the filter envelope and velocity → filter (steps 16–18) give each note its own cutoff, and each note should start with a clean filter rather than one still ringing from earlier notes. One filter shared by all voices is called "paraphonic": a distinct sound, but usually a hardware compromise. |
| The filter has an on/off switch, off by default | A low-pass filter even fully open shifts the phase of high frequencies, so "always on, cutoff at 20 kHz" wouldn't reproduce the old sound exactly. Older projects load with it off, bit-for-bit unchanged. Serum and Vital also start with their filters off. Switching it during a note crossfades over 10 ms. |
| Our own `Filter` class rather than `juce::dsp::StateVariableTPTFilter` | The same design (Simper's TPT state-variable filter, stable at any setting), but as a few plain numbers. JUCE's keeps its state in `std::vector`s, so copying it allocates, and a stolen voice's fading note copies its filter on the audio thread. It also gives band-pass and high-pass for step 15. |
| Filter types: low-pass (default), high-pass, band-pass, all from one calculation, each with a gain of Q at the cutoff | The state-variable filter produces all three at once, so offering them costs almost nothing. With the same gain at the cutoff, Resonance behaves the same for every type, and switching types doesn't jump in level there. The band-pass is left unnormalised (its peak rises with resonance), matching the other two. |
| A filter type change crossfades over 10 ms by blending the filter's outputs | At the cutoff, low-pass and high-pass are half a cycle apart, so an instant switch jumps by up to twice the note's level. Each voice keeps a smoothed "amount" per type and blends the three outputs; with one type chosen it's exactly that type's output (`1 × it + 0 × the others`), so nothing changes when the type isn't moving. No second filter is needed. |
| Signal order: oscillator → filter → amp envelope | The standard subtractive synth order. The filter is linear, so this only matters once later steps make the filter's settings depend on the note. |
| Resonance 0–100 % maps to Q 0.707–10, evenly on a log scale | 0 % is flat (no peak); 100 % is a +20 dB peak. This filter can't self-oscillate at any finite Q, so there's no runaway. |
| Cutoff 20 Hz – 20 kHz, knob centre about 632 Hz | The range of hearing, skewed so equal turns are equal steps in pitch. It's capped just below Nyquist at low sample rates. |
| Cutoff and resonance glide over 20 ms; coefficients are recalculated every 8 samples while gliding | Avoids zipper noise. Recalculating takes a `tan` and a `pow`; doing it every sample made a moving filter cost 3.5× as much (8.4 % of a core at 16 voices against 3.9 %), and 8 samples (0.17 ms) is far too short to hear as steps. The last sample of a glide always lands exactly on the new setting. |
| Key tracking 0–100 %, default 0 %, around middle C (MIDI 60) | At 100 % the cutoff moves an octave per octave played, so every note keeps the same tone; at 0 % (the old behaviour) it's fixed, so high notes sound duller. At middle C the cutoff is exactly the knob's setting. The tracked cutoff stays within 20 Hz – 20 kHz. At 0 % the cutoff is multiplied by exactly 1, so nothing changes. |
| A filter envelope per voice, its amount in octaves (−8 to +8, default 0) | Each note sweeps from its own key press, the reason for having a filter per voice. Octaves make the amount mean the same wherever the cutoff is: at the envelope's peak the cutoff is multiplied by 2^amount. At 0 the multiplier is exactly 1. |
| The filter envelope itself isn't smoothed; only its amount knob glides | Smoothing the envelope would blunt fast plucks, its main use. The cutoff follows it sample by sample, with coefficients recalculated every 8 samples, as for any moving cutoff (about 5 % of a core with all 16 voices sweeping). |
| The filter envelope's shape defaults to a pluck: no attack, 300 ms decay to 0 % sustain, 300 ms release | It has no effect until the amount is turned up, so the defaults are chosen to sound good the moment it is. |
| The per-block coefficient refresh only runs when nothing moves the cutoff | Refreshing at block starts while the envelope moved made the output depend on the host's block size (caught by the block-size test). While anything moves the cutoff, the every-8-samples updates handle it, at the same moments whatever the block size. Each note also restarts that 8-sample count, so a note never depends on what its voice played before. |

## Known limitations

- **A voice stolen twice within 5 ms** still cuts its first old note: each voice keeps only one fading note, so the newer one replaces it. That needs three notes on one voice in 5 ms with every voice busy.
- **A fading note keeps its last pitch.** Pitch bends during its 5 ms fade don't reach it, which is too short to hear.
- **The envelope's stages are straight lines.** Many synths curve them (exponential decay and release), which can sound more natural. Easy to add later as an option, without breaking projects.
- **Brightness steps when bending saw or square** across a band-limiting level boundary. Crossfading between neighbouring levels would fix it if it's audible.
- **High resonance can get loud.** A +20 dB peak on a strong harmonic can push the output past full scale, where the output guard clips it. Real synths often compensate by lowering the level as resonance rises; worth listening for.
- **The filter is perfectly clean.** Analog filters, and many digital ones, saturate (distort gently) when driven hard, which many people like. Ours doesn't, which also keeps it linear.
- **Key tracking follows the note played, not pitch bends.** A note bent up an octave keeps the cutoff of the key that was pressed. Some synths track the bent pitch; that could be added later.
- **Not yet checked by tools:** Steinberg's VST3 validator, and building on macOS or Linux. pluginval is run by hand, not from CTest.

## Working on it

- **Tests:** `cmake --build build --config Release --target Tests`, then `ctest --test-dir build -C Release --output-on-failure`.
- **Sound unchanged?** Record before a change and compare after; see the README.
- **Speed:** `Tests --benchmark` (Release build). Run it before and after anything that adds work per voice or per sample. Baseline on the development machine after step 14: 16 sine notes at about 43× real time (2.3 % of one CPU core, 0.25 ms of each 10.67 ms block); 16 saw notes with the pitch wheel moving at about 40×; 16 saw notes through the filter with its cutoff moving at about 25× (3.9 %). The filter costs nothing while it's off. Step 17 added a filter envelope case, measured in a slower session at about the same cost as the moving cutoff (5.1 % against 4.8 %). **The numbers drift with how busy the computer is** (by 15–25 % between sessions), so to judge a change, benchmark the previous commit at the same time: `git stash`, rebuild, benchmark, `git stash pop`, rebuild, benchmark. Step 15 measured this way cost nothing measurable.
- **Prove a new test can fail:** break the code it protects on purpose, confirm the test fails, then restore the file and confirm it's byte-for-byte identical.
- **pluginval** at strictness 10 on both Release and Debug builds, since Debug turns on JUCE's internal checks.
- **Commits** are scanned for secrets by the gitleaks hook in `.githooks`.
- **README and docs** have no tests: after editing them, read the result.
