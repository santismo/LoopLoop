# looploop - Technical Design

## Recommended Stack

Use JUCE/C++ for the MVP and ship as a macOS AU audio effect for Logic Pro. JUCE gives one codebase for DSP, UI, preset state, host transport, MIDI events, and later VST3/standalone builds. Native AUv3 is feasible, but it adds app-extension packaging, sandboxing, and IPC complexity before the loop engine is proven. The engine in this scaffold is framework-neutral enough to add an AUv3 target later.

## Logic Pro Constraints

- Logic AU audio effects can process audio and may receive MIDI, but MIDI routing into an audio FX insert is less direct than instrument/MIDI FX workflows.
- The practical MVP should support MIDI events when Logic delivers them to the insert, plus UI keyboard shortcuts while the plugin editor has focus.
- A future companion MIDI FX or instrument plugin can provide cleaner control routing and send control messages to the looper through a shared mapping protocol or virtual MIDI.
- Host tempo, transport, PPQ position, and time signature are read through `AudioPlayHead::PositionInfo`. These values may be missing or discontinuous during stop, cycle jumps, or scrubbing, so the engine must degrade safely.

## MVP Scope

- 8 stereo loops by default, with a preallocated maximum of 32 loops for expansion.
- AU audio effect loads in Logic.
- Input monitoring plus loop playback.
- Per-loop record, arm/listen, play/stop, mute, clear.
- Threshold recording with pre-buffer.
- Basic host tempo sync and launch quantization.
- MIDI note/CC and keyboard mapping with learn mode.
- Settings persistence. Recorded audio persistence is intentionally deferred.

## Architecture

- `PluginProcessor`: owns JUCE plugin lifecycle, APVTS parameters, state serialization, host timing readout, and MIDI/control event translation.
- `LoopEngine`: real-time audio engine. Owns loop buffers, pre-buffers, loop state machines, command queue, launch scheduling, and rendering.
- `SpscCommandQueue`: fixed-size lock-free queue for UI-to-audio commands. No allocation or locks on the audio thread.
- `PluginEditor`: simple original grid UI with circular loop controls and global controls.

## Real-Time Safety Rules

- Allocate loop buffers only in `prepareToPlay`.
- Do not allocate, lock, log, or call file APIs in `processBlock`.
- UI sends small command structs through a fixed-size atomic ring buffer.
- Audio thread writes snapshots into fixed arrays for UI polling.
- Loop state changes use short fades to avoid clicks.

## Loop State Machine

`Empty -> Armed -> Recording -> Playing`

Additional states/flags:
- `Overdubbing`: existing loop plays while incoming audio is mixed into it.
- `Muted`: loop phase continues, output suppressed.
- `Stopped`: loop keeps audio but playback inactive.

Core actions:
- Arm/listen: waits for threshold crossing.
- Record: starts recording now or at the selected launch boundary.
- Stop while recording: closes loop and starts playback.
- Play/stop: toggles playback.
- Overdub: toggles overdub.
- Mute: toggles output mute.
- Clear: wipes state and recorded length.

## Timing, Launch, and Sync

The processor reads:
- BPM
- PPQ position
- time signature
- transport playing state

Launch commands are converted to a target PPQ:
- Now: current block/sample
- Beat: next integer PPQ
- Bar: next bar start based on numerator
- Loop: current loop cycle if known, otherwise next bar

The engine checks whether the target PPQ falls inside the current audio block and triggers at the matching sample offset where possible.

Project Sync Length is a separate control from Launch. Launch decides when button actions fire; Project Sync Length decides the intended tempo-synced loop duration, such as 1, 2, 4, 8, or 16 bars.

## Threshold Recording

Each loop continuously maintains a stereo pre-buffer. When armed:
- Calculate input peak.
- If peak crosses threshold, begin recording.
- Copy the configured pre-buffer tail into the loop buffer first.
- Continue recording incoming audio.
- Auto-stop after configured bars if enabled, otherwise stop manually.

## Length Relationships

The MVP implements free/manual and fixed bar lengths. The design includes enum space for:
- same as master
- divisions
- multipliers

Once a master loop exists, dependent loops can derive their fixed sample length from the master loop’s sample length and phase.

## Loop Count

The engine preallocates buffers for up to 32 stereo loops and exposes an active loop count. This keeps the audio thread allocation-free while letting the UI/session choose 1-32 visible loops. Raising the maximum later is possible, but memory use grows quickly because every loop owns fixed loop buffers.

## Persistence

MVP stores settings and mappings only. Audio buffer persistence is deferred because embedding long loop audio in plugin state can make DAW projects huge and slow to save. A later version should offer explicit project audio persistence with compression and size limits.

## Test Plan

- Insert plugin on stereo audio channel in Logic and confirm pass-through.
- Record a loop manually, stop, and verify loop repeats without clicks.
- Arm threshold mode and confirm pre-buffer preserves transients.
- Test quantized record start on next beat/bar against Logic metronome.
- Stop/start Logic transport and confirm loops remain phase stable.
- Change BPM moderately and confirm no crash; MVP may not time-stretch existing loops.
- Verify MIDI note mapping when Logic routes MIDI to the audio FX.
- Enable Map mode, click a loop action, then press a keyboard key or send a MIDI note/CC and verify the action is remapped.
- Save/reload Logic project and confirm settings/mappings persist.
