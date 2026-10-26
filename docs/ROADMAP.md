# BoringSynth roadmap

The plan for growing BoringSynth, one small step at a time, from a plain sine synth into a full single-oscillator synth, and then a wavetable synth. Each step leaves the plug-in working and is one commit. Before moving on: the tests pass, pluginval passes, and it's been listened to in a DAW. How things are built, and why, is in [DESIGN.md](DESIGN.md).

**Rules for every step:** nothing on the audio thread allocates, locks or touches files; a new feature at its default sounds like the plug-in did before it; and each new test is shown to fail when the code it protects is broken.

## Done

| Phase | Steps |
|-------|-------|
| 0. Groundwork | 1. Permanent tests · 2. Final identity (BoringSynth, `Jack` / `Brng`) |
| 1. Parameters | 3. Parameter system and Volume · 4. Smoothing |
| 2. Playing basics | 5. Velocity sensitivity · 6. Pitch bend and range · 7. Sustain pedal |
| 3. Oscillator | 8. Split into files, `Oscillator` class · 9. Sine read from a wave table · 10. Band-limited table levels (FFT) · 11. Waveform choice: sine, triangle, saw, square |
| 4. Amp envelope | 12. ADSR amp envelope, our own `Envelope` class, with defaults that reproduce the old 5 ms fade · 13. Stolen voices fade their old note out over 5 ms instead of clicking |
| 5. Filter | 14. Per-voice resonant low-pass filter (our own `Filter` class): an on/off switch (off by default), Cutoff and Resonance · 15. Filter type: low-pass, high-pass, band-pass, crossfading when changed · 16. Key tracking, 0–100 % around middle C · 17. A filter envelope per note, its amount in octaves · 18. Velocity → filter: softer notes darker, by up to 8 octaves |
| 6. LFO | 19. One shared, free-running LFO on pitch (vibrato): Rate and Depth, worked out once per sample for every voice · 20. LFO destination: pitch, volume (tremolo) or filter cutoff, crossfading when changed · 21a. The LFO's own shapes, each exactly within ±1 · 21b. LFO Shape: sine, triangle, saw or square, crossfading when changed, with the saw's and square's jumps as 5 ms ramps (band-limited tables were tried first and rejected; see DESIGN.md) · 22. Mod wheel → LFO depth, its amount 0–100 % on top of the knob · 23a. LFO Retrigger: every note restarts the LFO, fading over 5 ms · 23b. LFO Sync: the rate as a note length at the host's tempo, its position following the song's while it plays |

Along the way, the test tools also gained a reference render (`--record` / `--compare`), named render options, shared measuring helpers, a CPU benchmark (`--benchmark`), and a stand-in for a host's play head (tempo, song position, playing or not).

## Next

**Phase 7: GUI and presets.** Milestone: a complete single-oscillator synth.
- 24. JUCE's generic editor.
- 25. A real layout, connected through parameter attachments.
- 26. Presets: save and load, factory presets, an "Init" preset.
- 27. Polish: look, resizing, high-DPI, envelope and filter displays.

**Phase 8: Full wavetables.** Milestone: a wavetable synth.
- 28. Multi-frame tables and a Position control.
- 29. A built-in wavetable library.
- 30. Modulating Position from envelopes and the LFO.
- 31. A wavetable display.
- 32. Importing wavetables from `.wav` files, loaded and band-limited on a background thread.
- 33. Optional: a modulation matrix.

**Phase 9: Ready to ship**
- 34. Steinberg's VST3 validator; pluginval and the tests on every push (GitHub Actions); several DAWs; CPU measurement at 16 voices; installers and signed releases.

## Not planned yet

These could each be a phase after Phase 7: a second oscillator, unison, glide and mono mode, built-in effects (chorus, delay, reverb), a Polyphony parameter, the CLAP format, and running pluginval from CTest.
