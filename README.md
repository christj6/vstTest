# Test

A minimal VST3 instrument plug-in. Press a key and it plays a sine wave at that key's pitch. Release the key and it stops, with a 5 ms fade so it doesn't click. There is no envelope, filter, parameter, or editor window. It can play up to 16 notes at once, and ignores the sustain pedal and other MIDI controllers.

The whole plug-in is in [`src/TestProcessor.cpp`](src/TestProcessor.cpp), built with [JUCE](https://juce.com) 9.0.3 and CMake.

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
cmake --build build --config Release --target Test_VST3
```

The first command generates a Visual Studio project in the `build` folder, using the newest Visual Studio installed. It takes a few minutes the first time. The second command compiles the plug-in.

The finished plug-in is at:

```
build\Test_artefacts\Release\VST3\Test.vst3
```

If you'd rather work in the Visual Studio IDE, open the solution file in the `build` folder after the first command (`Test.slnx` with Visual Studio 2026, `Test.sln` with Visual Studio 2022), choose **Release** in the toolbar, and build the **Test_VST3** project.

#### Building on macOS

> Not yet tested on macOS. These are the standard JUCE/CMake steps.

```bash
cmake -B build -G Xcode -D"CMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build --config Release --target Test_VST3
```

The plug-in is at `build/Test_artefacts/Release/VST3/Test.vst3`. You can also open `build/Test.xcodeproj` in Xcode and build the **Test_VST3** target there.

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
cmake --build build --config Release --target Test_VST3
```

The plug-in is at `build/Test_artefacts/Release/VST3/Test.vst3`.

### Debug vs. Release

Replace `Release` with `Debug` in the build command for a build that is easier to step through in a debugger, but slower. The output then goes to `build/Test_artefacts/Debug/...` instead.

## Running the tests

The `tests` folder has a small program, `RenderTests`, that plays MIDI into the plug-in's audio code and checks what comes out. It checks levels, silence for notes the sample rate can't represent, no NaN output, the release fade, note timing, and that the output doesn't depend on the host's block size. Build it and run it with CTest, which comes with CMake:

```bash
cmake --build build --config Release --target RenderTests
ctest --test-dir build -C Release --output-on-failure
```

It prints `PASS` or `FAIL` for each check. CTest reports the whole program as failed if any check fails. Run the tests after every change to the plug-in.

## How to install the plug-in

The build does not install the plug-in. Copy the whole `Test.vst3` folder (on Windows it's a folder, not a single file) to your system's VST3 folder:

| System  | VST3 folder |
|---------|-------------|
| Windows | `C:\Program Files\Common Files\VST3` (needs administrator rights) |
| macOS   | `/Library/Audio/Plug-Ins/VST3` or `~/Library/Audio/Plug-Ins/VST3` |
| Linux   | `~/.vst3` |

Then rescan plug-ins in your DAW and look for **Jack > Test**. It's an instrument, so load it where you'd load a synth.

**FL Studio on Windows:** FL only finds VST3 plug-ins in `C:\Program Files\Common Files\VST3`. Adding the build folder as a custom search path doesn't work, because FL only scans custom paths for VST2 plug-ins. Go to **Options > Manage plugins > Find installed plugins** to rescan.

### Tip for development on Windows: link instead of copying

If you're changing the code and rebuilding often, you can make a *junction* (a folder shortcut) in the VST3 folder that points at your build output. Then every rebuild shows up in your DAW without copying. Run this once in PowerShell **as administrator** (right-click PowerShell > Run as administrator), changing the second path to wherever you cloned the repo:

```bash
New-Item -ItemType Junction -Path "C:\Program Files\Common Files\VST3\Test.vst3" -Target "C:\path\to\vstTest\build\Test_artefacts\Release\VST3\Test.vst3"
```

Close the plug-in in your DAW before rebuilding, because Windows won't let the build overwrite a file that's in use. Rescan afterwards if the DAW doesn't pick up the change.

## Troubleshooting

- **`libs/JUCE` is empty, or CMake says it can't find `libs/JUCE`:** the submodule wasn't downloaded. Run `git submodule update --init --recursive`.
- **`No CMAKE_CXX_COMPILER could be found` (Windows):** Visual Studio's C++ tools aren't installed. Open the Visual Studio Installer, click **Modify**, and tick **Desktop development with C++**.
- **`'cmake' is not recognized`:** CMake isn't on your PATH. Reinstall it and choose the PATH option, or close and reopen the terminal after installing.
- **The build fails with "cannot open file ... Test.vst3" or "access denied":** the plug-in is loaded in your DAW. Close it there (or close the DAW) and build again.
- **The plug-in doesn't show up in the DAW:** check that the whole `Test.vst3` folder is in the VST3 folder listed above, then rescan.
- **Weird errors after changing CMake settings or Visual Studio versions:** delete the `build` folder and run both commands again.

## Code style

The code follows the [JUCE coding standards](https://juce.com/blog/coding-standards/).
