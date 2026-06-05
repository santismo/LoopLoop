#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

class SlotPad final : public juce::Component
{
public:
    SlotPad();
    std::function<void()> onSelect;
    std::function<void()> onArm;
    std::function<void()> onPlay;
    std::function<void()> onOverdub;
    std::function<void()> onMute;
    std::function<void()> onClear;
    std::function<void(looper::SlotAction)> onLearn;

    void setSlotIndex (int index);
    void setSnapshot (looper::SlotSnapshot snapshot);
    void setSelected (bool shouldBeSelected);
    void setMappingMode (bool shouldMap);
    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent& event) override;

private:
    static juce::String stateName (looper::SlotState state);
    juce::Rectangle<float> circleBounds() const;
    juce::Rectangle<float> zoneFor (int zone) const;
    void drawZone (juce::Graphics& g, int zone, const juce::String& text, juce::Colour colour);
    int zoneForPoint (juce::Point<float> point) const;
    looper::SlotSnapshot current;
    int slotIndex = 0;
    bool selected = false;
    bool mappingMode = false;
};

class LogicPerformanceLooperAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                                         private juce::Timer
{
public:
    explicit LogicPerformanceLooperAudioProcessorEditor (LogicPerformanceLooperAudioProcessor& processor);
    ~LogicPerformanceLooperAudioProcessorEditor() override = default;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    void timerCallback() override;
    void configureSlider (juce::Slider& slider);
    void configureButton (juce::TextButton& button);
    void triggerSelected (looper::SlotAction action);

    LogicPerformanceLooperAudioProcessor& processorRef;
    juce::Label titleLabel;
    juce::Label statusLabel;
    juce::ComboBox syncModeBox;
    juce::ComboBox quantizeBox;
    juce::ComboBox loopLengthBox;
    juce::Slider slotCountSlider;
    juce::Slider thresholdSlider;
    juce::Slider preBufferSlider;
    juce::Label slotCountLabel;
    juce::Label modeLabel;
    juce::Label launchLabel;
    juce::Label lengthLabel;
    juce::Label thresholdLabel;
    juce::Label preBufferLabel;
    juce::TextButton clearAllButton { "Clear All" };
    juce::TextButton stopAllButton { "Stop All" };
    juce::TextButton mapButton { "Map" };
    juce::TextButton wetOnlyButton { "Wet Only" };
    std::array<std::unique_ptr<SlotPad>, looper::maxSlots> pads;
    int selectedLoop = 0;
    bool mappingMode = false;

    std::unique_ptr<ComboBoxAttachment> quantizeAttachment;
    std::unique_ptr<ComboBoxAttachment> loopLengthAttachment;
    std::unique_ptr<ComboBoxAttachment> syncModeAttachment;
    std::unique_ptr<SliderAttachment> slotCountAttachment;
    std::unique_ptr<SliderAttachment> thresholdAttachment;
    std::unique_ptr<SliderAttachment> preBufferAttachment;
    std::unique_ptr<ButtonAttachment> wetOnlyAttachment;
};
