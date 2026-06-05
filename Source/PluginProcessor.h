#pragma once

#include <JuceHeader.h>
#include "LoopEngine.h"

#include <array>
#include <atomic>

namespace ParamIDs
{
    static constexpr auto slotCount = "slotCount";
    static constexpr auto quantize = "quantize";
    static constexpr auto masterSlot = "masterSlot";
    static constexpr auto loopLength = "loopLength";
    static constexpr auto syncMode = "syncMode";
    static constexpr auto threshold = "threshold";
    static constexpr auto preBufferMs = "preBufferMs";
    static constexpr auto wetOnly = "wetOnly";
}

class LogicPerformanceLooperAudioProcessor final : public juce::AudioProcessor
{
public:
    LogicPerformanceLooperAudioProcessor();
    ~LogicPerformanceLooperAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getValueTreeState() noexcept { return parameters; }
    LoopEngine& getLoopEngine() noexcept { return engine; }
    looper::EngineSnapshot getEngineSnapshot() const noexcept { return engine.getSnapshot(); }
    void pushSlotCommand (looper::SlotAction action, int slot);
    void pushGlobalCommand (looper::SlotAction action);
    void startMappingLearn (int slot, looper::SlotAction action) noexcept;
    void startGlobalMappingLearn (looper::SlotAction action) noexcept;
    bool learnKeyboardKey (int keyCode) noexcept;
    bool handleKeyboardKey (int keyCode);
    bool isLearningMapping() const noexcept { return learningSlot.load() != -1; }
    juce::String getMappingDescription (int slot, looper::SlotAction action) const;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    static juce::StringArray quantizeNames();
    static juce::StringArray loopLengthNames();
    static juce::StringArray syncModeNames();

private:
    looper::HostTiming readHostTiming() const;
    looper::QuantizeMode currentQuantize() const noexcept;
    void syncEngineSettings();
    void resetDefaultMappings() noexcept;
    void handleMidiMappings (const juce::MidiBuffer& midi);
    static int actionIndex (looper::SlotAction action) noexcept;
    static looper::SlotAction indexToAction (int index) noexcept;
    static int globalActionIndex (looper::SlotAction action) noexcept;
    static looper::SlotAction indexToGlobalAction (int index) noexcept;

    static constexpr int actionsPerLoop = 5;
    static constexpr int mappingCount = looper::maxSlots * actionsPerLoop;
    static constexpr int globalMappingCount = 2;
    static int mappingIndex (int slot, looper::SlotAction action) noexcept;

    juce::AudioProcessorValueTreeState parameters;
    LoopEngine engine;
    std::array<std::atomic<int>, mappingCount> keyBindings {};
    std::array<std::atomic<int>, mappingCount> midiNoteBindings {};
    std::array<std::atomic<int>, mappingCount> midiCcBindings {};
    std::array<std::atomic<int>, globalMappingCount> globalKeyBindings {};
    std::array<std::atomic<int>, globalMappingCount> globalMidiNoteBindings {};
    std::array<std::atomic<int>, globalMappingCount> globalMidiCcBindings {};
    std::atomic<int> learningSlot { -1 };
    std::atomic<int> learningAction { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LogicPerformanceLooperAudioProcessor)
};
