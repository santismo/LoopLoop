#pragma once

#include <JuceHeader.h>

#include <array>

namespace looper
{
    constexpr int defaultSlots = 8;
    constexpr int maxSlots = 32;
    constexpr int maxChannels = 2;

    enum class SlotState : uint8_t
    {
        empty,
        armed,
        recording,
        playing,
        overdubbing,
        stopped
    };

    enum class SlotAction : uint8_t
    {
        arm,
        record,
        overdub,
        playStop,
        mute,
        clear,
        clearAll,
        stopAll,
        playAll,
        playStopAll
    };

    enum class QuantizeMode : int
    {
        immediate = 0,
        nextBeat,
        nextBar,
        nextLoop
    };

    enum class AutoStopMode : int
    {
        manual = 0,
        oneBar,
        twoBars,
        fourBars,
        eightBars,
        sixteenBars
    };

    enum class LengthMode : int
    {
        free = 0,
        fixedBars,
        sameAsMaster,
        autoMaster,
        halfMaster,
        quarterMaster,
        doubleMaster,
        quadrupleMaster
    };

    enum class SyncMode : int
    {
        standalone = 0,
        dawSync
    };

    struct HostTiming
    {
        double bpm = 120.0;
        double ppq = 0.0;
        int numerator = 4;
        int denominator = 4;
        bool isPlaying = false;
        bool hasPosition = false;
    };

    struct SlotSettings
    {
        float thresholdDb = -36.0f;
        int preBufferMs = 80;
        int minRecordMs = 250;
        AutoStopMode autoStop = AutoStopMode::manual;
        LengthMode lengthMode = LengthMode::free;
        int fixedBars = 1;
        int midiNote = -1;
    };

    struct ControlCommand
    {
        SlotAction action = SlotAction::playStop;
        int slot = 0;
        QuantizeMode quantize = QuantizeMode::nextBar;
        double requestedPpq = 0.0;
    };

    struct SlotSnapshot
    {
        SlotState state = SlotState::empty;
        bool muted = false;
        bool hasAudio = false;
        bool pendingStart = false;
        bool pendingStop = false;
        float inputLevel = 0.0f;
        float threshold = 0.015f;
        double progress = 0.0;
        double lengthBeats = 0.0;
        int midiNote = -1;
    };

    struct EngineSnapshot
    {
        std::array<SlotSnapshot, maxSlots> slots {};
        HostTiming host {};
        QuantizeMode quantize = QuantizeMode::nextBar;
        int masterSlot = 0;
        float detectedMasterBpm = 0.0f;
        int barCountdown = 0;
        int masterCountdown = 0;
        bool masterHasAudio = false;
        bool anyActive = false;
        float inputLevel = 0.0f;
        float threshold = 0.015f;
        SyncMode syncMode = SyncMode::standalone;
    };
}
