# BoringSynth

A minimal VST3 instrument plug-in. Press a key and it plays a sine wave at that key's pitch. Release the key and it stops, with a 5 ms fade so it doesn't click. There is no envelope, filter, or editor window. It can play up to 16 notes at once. The pitch wheel bends notes, and the sustain pedal holds them after you release the keys. Other MIDI controllers, including the sostenuto and soft pedals, are ignored.

It has three parameters. With no editor window, your DAW shows them in its own generic controls for the plug-in. All of them can be automated and are saved with your project.

- **Volume**, from -inf (silence) to +6 dB, default 0 dB. Changes glide over 20 ms, so fast moves and automation don't crackle.
- **Velocity Sensitivity**, from 0 to 100 %, default 0 %. At 0 % every note plays at the same level however hard you hit the key. At 100 %, a note's level follows the standard General MIDI curve, (velocity / 127)², so velocity 64 is about 12 dB quieter. The hardest hit (velocity 127) is always full level. A note's level is set when it starts, so changing the sensitivity affects the next notes you play, not ones already sounding.
- **Pitch Bend Range**, from 0 to 24 semitones, default 2. How far the pitch wheel bends notes, up or down. At 0 the wheel is ignored. Bends glide over 10 ms, so a keyboard that sends the wheel in coarse steps still bends smoothly. A note bent above half the sample rate (where it would distort) goes silent until it's bent back down.

It's built with [JUCE](https://juce.com) 9.0.3 and CMake. The code is in `src`:

| File | What it does |
|------|--------------|
| [`BoringSynthProcessor`](src/BoringSynthProcessor.cpp) | The plug-in itself: the audio callback, the Volume control, the output safety check, and saving and loading settings |
| [`Parameters`](src/Parameters.cpp) | Every parameter's ID, range, default and display text |
| [`Synth.h`](src/Synth.h) | JUCE's synthesiser, which turns MIDI into notes, with the sostenuto pedal switched off |
| [`SynthVoice`](src/SynthVoice.cpp) | One playing note: its pitch, pitch bend, velocity and release fade |
| [`Oscillator.h`](src/Oscillator.h) | The waveform itself, one sample at a time, read from a wave table |
| [`WaveTable`](src/WaveTable.cpp) | One cycle of a waveform, stored as 2048 samples and read with interpolation; built once when the plug-in loads |

## How to build the plug-in

### 1. Install the tools

You need:

- **Git**: https://git-scm.com/downloads
- **CMake 3.22 or later**: https://cmake.org/download/ (on Windows, choose the option to add CMake to the PATH during installation)
- **A C++ compiler**:
  - **Windows:** [Visual Studio](https://visualstudio.microsoft.com/) 2022 or later. The free Community edition is fine. In the Visual Studio Installer, tick the **Desktop development with C++** workload.
  - **macOS:** Xcode from the App Store, then run `xcode-select --install` in Terminal.
  - **Linux:** see [Building on Linux](#building-on-linux) below.

To check that Git and CMake are installed, open a terminal (PowerShell on Windows, Terminal on macOS) and run:

```bash
git --version
cmake --version
```

Both should print a version number.

### 2. Download the code

JUCE is included as a git submodule, which is a link to JUCE's own repository. The `--recursive` option downloads it too. JUCE is large, so this can take a few minutes.

```bash
git clone --recursive https://github.com/christj6/vstTest.git
cd vstTest
```

If you already cloned without `--recursive`, the `libs/JUCE` folder will be empty. Fix that with:

```bash
git submodule update --init --recursive
```

### 3. Build

#### Building on Windows

Run these from the `vstTest` folder:

```bash
cmake -B build
cmake --build build --config Release --target BoringSynth_VST3
```

The first command generates a Visual Studio project in the `build` folder, using the newest Visual Studio installed. It takes a few minutes the first time. The second command compiles the plug-in.

The finished plug-in is at:

```
build\BoringSynth_artefacts\Release\VST3\BoringSynth.vst3
```

If you'd rather work in the Visual Studio IDE, open the solution file in the `build` folder after the first command (`BoringSynth.slnx` with Visual Studio 2026, `BoringSynth.sln` with Visual Studio 2022), choose **Release** in the toolbar, and build the **BoringSynth_VST3** project.

#### Building on macOS

> Not yet tested on macOS. These are the standard JUCE/CMake steps.

```bash
cmake -B build -G Xcode -D"CMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build --config Release --target BoringSynth_VST3
```

The plug-in is at `build/BoringSynth_artefacts/Release/VST3/BoringSynth.vst3`. You can also open `build/BoringSynth.xcodeproj` in Xcode and build the **BoringSynth_VST3** target there.

#### Building on Linux

> Not yet tested on Linux. These are the standard JUCE/CMake steps for Ubuntu.

Install a compiler, the build tools, and the libraries JUCE needs:

```bash
sudo apt update
sudo apt install build-essential ninja-build libasound2-dev libjack-jackd2-dev ladspa-sdk libfontconfig1-dev libfreetype-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libxi-dev
```

The package list comes from JUCE's [`docs/Linux Dependencies.md`](libs/JUCE/docs/Linux%20Dependencies.md). Package names can differ on other distributions.

Then build:

```bash
cmake -B build -G "Ninja Multi-Config"
cmake --build build --config Release --target BoringSynth_VST3
```

The plug-in is at `build/BoringSynth_artefacts/Release/VST3/BoringSynth.vst3`.

### Debug vs. Release

Replace `Release` with `Debug` in the build command for a build that is easier to step through in a debugger, but slower. The output then goes to `build/BoringSynth_artefacts/Debug/...` instead.

## Running the tests

The `tests` folder has a small test program, `Tests`. Its render tests play MIDI into the plug-in's audio code and check what comes out: levels, silence for notes the sample rate can't represent, no NaN output, the release fade, the sustain pedal, note timing, the effect of Volume, Velocity Sensitivity and pitch bend, and that the output doesn't depend on the host's block size. Its parameter tests check that parameters keep the IDs saved projects rely on, and that the plug-in's settings survive saving and reloading. Its oscillator tests check the wave table and oscillator on their own against `std::sin`, so any loss of accuracy shows up as a number. Build it and run it with CTest, which comes with CMake:

```bash
cmake --build build --config Release --target Tests
ctest --test-dir build -C Release --output-on-failure
```

It prints `PASS` or `FAIL` for each check. CTest reports the whole program as failed if any check fails. Run the tests after every change to the plug-in.

## Checking commits for secrets

The repo includes a Git hook, [`.githooks/pre-commit`](.githooks/pre-commit), that scans each commit for secrets (API keys, tokens, private keys) with [gitleaks](https://github.com/gitleaks/gitleaks) and blocks the commit if it finds one. To use it, install gitleaks (Windows: `scoop install gitleaks`, macOS: `brew install gitleaks`), then turn the hook on once in your clone:

```bash
git config core.hooksPath .githooks
```

If gitleaks reports something that isn't really a secret, see its documentation for how to allow it. If something real ever does reach GitHub, revoke or replace it right away. Removing it in a later commit doesn't help, because it stays in the history.

## How to install the plug-in

The build does not install the plug-in. Copy the whole `BoringSynth.vst3` folder (on Windows it's a folder, not a single file) to your system's VST3 folder:

| System  | VST3 folder |
|---------|-------------|
| Windows | `C:\Program Files\Common Files\VST3` (needs administrator rights) |
| macOS   | `/Library/Audio/Plug-Ins/VST3` or `~/Library/Audio/Plug-Ins/VST3` |
| Linux   | `~/.vst3` |

Then rescan plug-ins in your DAW and look for **Jack > BoringSynth**. It's an instrument, so load it where you'd load a synth.

**FL Studio on Windows:** FL only finds VST3 plug-ins in `C:\Program Files\Common Files\VST3`. Adding the build folder as a custom search path doesn't work, because FL only scans custom paths for VST2 plug-ins. Go to **Options > Manage plugins > Find installed plugins** to rescan.

### Tip for development on Windows: link instead of copying

If you're changing the code and rebuilding often, you can make a *junction* (a folder shortcut) in the VST3 folder that points at your build output. Then every rebuild shows up in your DAW without copying. Run this once in PowerShell **as administrator** (right-click PowerShell > Run as administrator), changing the second path to wherever you cloned the repo:

```bash
New-Item -ItemType Junction -Path "C:\Program Files\Common Files\VST3\BoringSynth.vst3" -Target "C:\path\to\vstTest\build\BoringSynth_artefacts\Release\VST3\BoringSynth.vst3"
```

Close the plug-in in your DAW before rebuilding, because Windows won't let the build overwrite a file that's in use. Rescan afterwards if the DAW doesn't pick up the change.

## Troubleshooting

- **`libs/JUCE` is empty, or CMake says it can't find `libs/JUCE`:** the submodule wasn't downloaded. Run `git submodule update --init --recursive`.
- **`No CMAKE_CXX_COMPILER could be found` (Windows):** Visual Studio's C++ tools aren't installed. Open the Visual Studio Installer, click **Modify**, and tick **Desktop development with C++**.
- **`'cmake' is not recognized`:** CMake isn't on your PATH. Reinstall it and choose the PATH option, or close and reopen the terminal after installing.
- **The build fails with "cannot open file ... BoringSynth.vst3" or "access denied":** the plug-in is loaded in your DAW. Close it there (or close the DAW) and build again.
- **The plug-in doesn't show up in the DAW:** check that the whole `BoringSynth.vst3` folder is in the VST3 folder listed above, then rescan.
- **Weird errors after changing CMake settings or Visual Studio versions:** delete the `build` folder and run both commands again.

## Code style

The code follows the [JUCE coding standards](https://juce.com/blog/coding-standards/).
