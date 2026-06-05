#pragma once

#include <JuceHeader.h>
#include "LooperTypes.h"
#include "SpscCommandQueue.h"

class LoopEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    void process (juce::AudioBuffer<float>& buffer,
                  const juce::MidiBuffer& midi,
                  const looper::HostTiming& host,
                  looper::QuantizeMode quantize,
                  bool passInputThrough);

    bool pushCommand (const looper::ControlCommand& command) noexcept;
    looper::EngineSnapshot getSnapshot() const noexcept;
    void setSlotSettings (int slot, const looper::SlotSettings& settings);
    looper::SlotSettings getSlotSettings (int slot) const;
    void setMasterSlot (int slot) noexcept;
    void setActiveSlotCount (int count) noexcept;
    void setSyncMode (looper::SyncMode mode) noexcept { syncMode = mode; }
    int getActiveSlotCount() const noexcept { return activeSlotCount; }

private:
    struct PendingCommand
    {
        looper::ControlCommand command;
        double targetPpq = 0.0;
        bool active = false;
    };

    struct Slot
    {
        juce::AudioBuffer<float> loop;
        juce::AudioBuffer<float> preBuffer;
        looper::SlotSettings settings;
        looper::SlotState state = looper::SlotState::empty;
        bool muted = false;
        int lengthSamples = 0;
        int recordPosition = 0;
        int playPosition = 0;
        int preWrite = 0;
        int preFill = 0;
        int autoStopSamples = 0;
        bool pendingRecordOnMasterStart = false;
        bool pendingPlayOnMasterStart = false;
        bool pendingStopOnMasterStart = false;
        float inputLevel = 0.0f;
        float fadeGain = 1.0f;
        float fadeTarget = 1.0f;
        double lengthBeats = 0.0;
        int suppressStartCrossfadeSamples = 0;
    };

    static constexpr int commandQueueSize = 128;
    static constexpr int maxPendingCommands = 32;
    static constexpr double maxLoopSeconds = 64.0;
    static constexpr double maxPreBufferSeconds = 2.0;

    void drainCommandQueue (const looper::HostTiming& host, looper::QuantizeMode quantize) noexcept;
    void scheduleCommand (const looper::ControlCommand& command,
                          const looper::HostTiming& host,
                          looper::QuantizeMode quantize) noexcept;
    void executeCommand (const looper::ControlCommand& command, int sampleOffset, const looper::HostTiming& host) noexcept;
    void processSlotSample (Slot& slot, int slotIndex, const float* inL, const float* inR, float& outL, float& outR, const looper::HostTiming& host) noexcept;
    void beginRecording (int slotIndex, const looper::HostTiming& host, bool usePreBuffer) noexcept;
    void finishRecording (int slotIndex, const looper::HostTiming& host) noexcept;
    void clearSlot (Slot& slot) noexcept;
    void updatePreBuffer (Slot& slot, float left, float right) noexcept;
    void triggerMasterBoundary (const looper::HostTiming& host) noexcept;
    int phasePositionFor (const Slot& slot, const looper::HostTiming& host) const noexcept;
    int phasePositionForMaster (const Slot& slot) const noexcept;
    int masterLengthSamplesFor (int slotIndex) const noexcept;
    bool masterHasAudio() const noexcept;
    bool isMasterDerived (looper::LengthMode mode) const noexcept;
    bool isAutoMaster (int slotIndex) const noexcept;
    bool shouldSlaveToMaster (int slotIndex) const noexcept;
    bool canRecordNow (const looper::HostTiming& host) const noexcept;
    float estimateMasterBpm() const noexcept;
    int countdownToHostBar (const looper::HostTiming& host) const noexcept;
    int countdownToMasterStart() const noexcept;
    bool shouldTriggerPending (const PendingCommand& pending,
                               const looper::HostTiming& host,
                               int sampleOffset,
                               int numSamples) const noexcept;
    double targetPpqFor (looper::QuantizeMode mode, int slot, const looper::HostTiming& host) const noexcept;
    int samplesForBars (int bars, const looper::HostTiming& host) const noexcept;
    int preBufferSamplesFor (const Slot& slot) const noexcept;
    int crossfadeSamplesFor (const Slot& slot) const noexcept;
    float crossfadedSample (const Slot& slot, int channel, int position, bool suppressStartCrossfade) const noexcept;

    std::array<Slot, looper::maxSlots> slots;
    std::array<PendingCommand, maxPendingCommands> pendingCommands {};
    SpscCommandQueue<looper::ControlCommand, commandQueueSize> commandQueue;
    double sampleRate = 44100.0;
    int maxLoopSamples = 0;
    int maxPreBufferSamples = 0;
    int masterSlot = 0;
    int activeSlotCount = looper::defaultSlots;
    float globalInputLevel = 0.0f;
    looper::SyncMode syncMode = looper::SyncMode::standalone;
    looper::HostTiming lastHost;
    looper::QuantizeMode lastQuantize = looper::QuantizeMode::nextBar;
};
