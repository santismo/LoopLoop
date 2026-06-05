# LoopLoop

LoopLoop is a JUCE/C++ audio FX looper plugin for Logic Pro. It builds as a macOS Audio Unit effect named `looploop`.

## Features

- Stereo audio effect plugin for Logic Pro
- 8 visible loop slots by default, with engine support for up to 32
- Record, arm/listen, play/stop, overdub, mute, and clear actions
- Threshold recording with pre-buffer
- Host tempo/transport readout for DAW sync and launch quantization
- MIDI note/CC and keyboard mapping with learn mode
- Settings and mappings saved through plugin state

## Requirements

- macOS
- CMake 3.22 or newer
- Xcode command line tools or Xcode
- A local JUCE checkout or extracted JUCE folder
- Logic Pro for loading the AU plugin

## Build

Pass the JUCE folder path when configuring:

```sh
cmake -B build -S . -DJUCE_DIR=/path/to/JUCE
cmake --build build --config Release
```

The AU component is generated under:

```text
build/LogicPerformanceLooper_artefacts/Release/AU/looploop.component
```

Depending on your generator/configuration, the build output may also appear under `build/LogicPerformanceLooper_artefacts/AU/looploop.component`.

## Install for Logic Pro

Copy the built component into your user Audio Units folder:

```sh
mkdir -p "$HOME/Library/Audio/Plug-Ins/Components"
cp -R build/LogicPerformanceLooper_artefacts/Release/AU/looploop.component "$HOME/Library/Audio/Plug-Ins/Components/"
```

If your build output does not include the `Release` folder, copy this path instead:

```sh
cp -R build/LogicPerformanceLooper_artefacts/AU/looploop.component "$HOME/Library/Audio/Plug-Ins/Components/"
```

Then restart Logic Pro or rescan Audio Units in Logic's Plug-in Manager.

## Notes

LoopLoop is currently configured as an Audio Unit audio effect:

- Product name: `looploop`
- Bundle ID: `com.santiagotrejo.looploop`
- AU type: `kAudioUnitType_Effect`
- MIDI input enabled
- MIDI output disabled

The deeper architecture and test notes are in `DESIGN.md`.
