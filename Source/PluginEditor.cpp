#include "PluginEditor.h"

#include <limits>

namespace
{
    juce::Colour bg() { return juce::Colour (0xff101316); }
    juce::Colour panel() { return juce::Colour (0xff1d2328); }
    juce::Colour line() { return juce::Colour (0xff55616b); }
    juce::Colour accent() { return juce::Colour (0xff45d483); }

    juce::Colour stateColour (looper::SlotState state)
    {
        switch (state)
        {
            case looper::SlotState::empty: return juce::Colour (0xff303840);
            case looper::SlotState::armed: return juce::Colour (0xffffc857);
            case looper::SlotState::recording: return juce::Colour (0xffff4d5a);
            case looper::SlotState::playing: return juce::Colour (0xff45d483);
            case looper::SlotState::overdubbing: return juce::Colour (0xff7f7cff);
            case looper::SlotState::stopped: return juce::Colour (0xff8b98a5);
            default: return juce::Colour (0xff303840);
        }
    }
}

SlotPad::SlotPad()
{
    setRepaintsOnMouseActivity (true);
}

void SlotPad::setSlotIndex (int index)
{
    slotIndex = index;
    repaint();
}

void SlotPad::setSnapshot (looper::SlotSnapshot snapshot)
{
    current = snapshot;
    repaint();
}

void SlotPad::setSelected (bool shouldBeSelected)
{
    selected = shouldBeSelected;
    repaint();
}

void SlotPad::setMappingMode (bool shouldMap)
{
    mappingMode = shouldMap;
    repaint();
}

juce::String SlotPad::stateName (looper::SlotState state)
{
    switch (state)
    {
        case looper::SlotState::empty: return "Empty";
        case looper::SlotState::armed: return "Listening";
        case looper::SlotState::recording: return "Recording";
        case looper::SlotState::playing: return "Playing";
        case looper::SlotState::overdubbing: return "Overdub";
        case looper::SlotState::stopped: return "Stopped";
        default: return "Empty";
    }
}

void SlotPad::paint (juce::Graphics& g)
{
    const auto bounds = circleBounds();
    const auto colour = stateColour (current.state);

    g.setColour (panel().withAlpha (0.96f));
    g.fillEllipse (bounds);
    g.setColour ((selected ? juce::Colours::white : colour).withAlpha (current.muted ? 0.42f : 0.92f));
    g.drawEllipse (bounds, selected ? 3.2f : (current.state == looper::SlotState::empty ? 1.4f : 2.4f));

    auto progress = bounds.reduced (5.0f);
    juce::Path arc;
    const auto start = -juce::MathConstants<float>::halfPi;
    const auto end = start + static_cast<float> (current.progress) * juce::MathConstants<float>::twoPi;
    arc.addCentredArc (progress.getCentreX(), progress.getCentreY(), progress.getWidth() * 0.5f, progress.getHeight() * 0.5f, 0.0f, start, end, true);
    g.setColour (colour.withAlpha (0.90f));
    g.strokePath (arc, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    drawZone (g, 0, "Arm", juce::Colour (0xffffc857));
    drawZone (g, 1, current.state == looper::SlotState::recording ? "Stop" : (current.pendingStart ? "Wait" : "Play"), colour);
    drawZone (g, 2, "Dub", juce::Colour (0xff7f7cff));
    drawZone (g, 3, "Mute", juce::Colour (0xff8b98a5));
    drawZone (g, 4, "Clear", juce::Colour (0xffff6b6b));

    const auto badge = juce::Rectangle<float> (bounds.getX() + bounds.getWidth() * 0.13f,
                                               bounds.getY() + bounds.getHeight() * 0.13f,
                                               bounds.getWidth() * 0.16f,
                                               bounds.getWidth() * 0.16f);
    g.setColour (juce::Colours::black.withAlpha (0.30f));
    g.fillEllipse (badge);
    g.setFont (juce::FontOptions (juce::jmax (10.0f, badge.getHeight() * 0.42f), juce::Font::bold));
    g.setColour (juce::Colours::white);
    g.drawText (juce::String (slotIndex + 1), badge.toNearestInt(), juce::Justification::centred);

    const auto status = current.pendingStop ? "Ending"
                       : current.pendingStart ? "Queued"
                       : current.state == looper::SlotState::empty ? ""
                       : stateName (current.state);
    if (status.isNotEmpty() || current.muted)
    {
        const auto statusText = status + (current.muted ? " Muted" : "");
        const auto statusArea = juce::Rectangle<float> (bounds.getX() + bounds.getWidth() * 0.24f,
                                                        bounds.getBottom() - bounds.getHeight() * 0.27f,
                                                        bounds.getWidth() * 0.52f,
                                                        bounds.getHeight() * 0.11f);
        g.setColour (juce::Colours::black.withAlpha (0.22f));
        g.fillRoundedRectangle (statusArea, statusArea.getHeight() * 0.5f);
        g.setFont (juce::FontOptions (juce::jmax (8.0f, statusArea.getHeight() * 0.48f), juce::Font::bold));
        g.setColour (colour);
        g.drawFittedText (statusText, statusArea.toNearestInt().reduced (4, 0), juce::Justification::centred, 1);
    }

    if (mappingMode)
    {
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.fillEllipse (bounds.reduced (10.0f));
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.setColour (juce::Colours::white);
        g.drawText ("MAP", bounds.toNearestInt().removeFromTop (28), juce::Justification::centred);
    }
}

void SlotPad::resized()
{
}

void SlotPad::mouseUp (const juce::MouseEvent& event)
{
    if (onSelect != nullptr)
        onSelect();

    const auto zone = zoneForPoint (event.position);
    if (mappingMode && zone >= 0)
    {
        if (onLearn != nullptr)
            onLearn (zone == 0 ? looper::SlotAction::arm
                     : zone == 1 ? looper::SlotAction::playStop
                     : zone == 2 ? looper::SlotAction::overdub
                     : zone == 3 ? looper::SlotAction::mute
                                 : looper::SlotAction::clear);
        return;
    }

    switch (zone)
    {
        case 0: if (onArm != nullptr) onArm(); break;
        case 1: if (onPlay != nullptr) onPlay(); break;
        case 2: if (onOverdub != nullptr) onOverdub(); break;
        case 3: if (onMute != nullptr) onMute(); break;
        case 4: if (onClear != nullptr) onClear(); break;
        default: break;
    }
}

juce::Rectangle<float> SlotPad::circleBounds() const
{
    auto area = getLocalBounds().toFloat().reduced (6.0f);
    const auto size = juce::jmin (area.getWidth(), area.getHeight());
    return juce::Rectangle<float> (area.getCentreX() - size * 0.5f, area.getCentreY() - size * 0.5f, size, size);
}

juce::Rectangle<float> SlotPad::zoneFor (int zone) const
{
    const auto circle = circleBounds();
    const auto button = circle.getWidth() * 0.19f;
    const auto centre = circle.getCentre();
    const auto inset = circle.getWidth() * 0.18f;
    switch (zone)
    {
        case 0: return { centre.x - button * 0.5f, circle.getY() + inset, button, button };
        case 1: return { centre.x - button * 0.60f, centre.y - button * 0.60f, button * 1.2f, button * 1.2f };
        case 2: return { circle.getRight() - inset - button, centre.y - button * 0.5f, button, button };
        case 3: return { circle.getX() + inset, centre.y - button * 0.5f, button, button };
        case 4: return { centre.x - button * 0.5f, circle.getBottom() - inset - button, button, button };
        default: return {};
    }
}

void SlotPad::drawZone (juce::Graphics& g, int zone, const juce::String& text, juce::Colour colour)
{
    const auto area = zoneFor (zone);
    g.setColour (colour.withAlpha (0.16f));
    g.fillEllipse (area);
    g.setColour (colour.withAlpha (0.72f));
    g.drawEllipse (area, 1.4f);
    g.setFont (juce::FontOptions (juce::jmax (8.5f, area.getWidth() * 0.20f), juce::Font::bold));
    g.setColour (juce::Colours::white.withAlpha (0.86f));
    g.drawFittedText (text, area.toNearestInt().reduced (2), juce::Justification::centred, 1);
}

int SlotPad::zoneForPoint (juce::Point<float> point) const
{
    if (! circleBounds().contains (point))
        return -1;

    auto bestZone = -1;
    auto bestDistance = std::numeric_limits<float>::max();
    for (int zone = 0; zone < 5; ++zone)
    {
        const auto area = zoneFor (zone);
        if (area.contains (point))
            return zone;

        const auto distance = point.getDistanceFrom (area.getCentre());
        if (distance < bestDistance)
        {
            bestDistance = distance;
            bestZone = zone;
        }
    }

    return bestZone;
}

LogicPerformanceLooperAudioProcessorEditor::LogicPerformanceLooperAudioProcessorEditor (LogicPerformanceLooperAudioProcessor& owner)
    : AudioProcessorEditor (&owner),
      processorRef (owner)
{
    titleLabel.setText ("looploop", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible (titleLabel);

    statusLabel.setFont (juce::FontOptions (13.0f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.70f));
    addAndMakeVisible (statusLabel);

    syncModeBox.addItemList (LogicPerformanceLooperAudioProcessor::syncModeNames(), 1);
    syncModeBox.setColour (juce::ComboBox::backgroundColourId, panel());
    syncModeBox.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    syncModeBox.setColour (juce::ComboBox::outlineColourId, line());
    addAndMakeVisible (syncModeBox);

    quantizeBox.addItemList (LogicPerformanceLooperAudioProcessor::quantizeNames(), 1);
    quantizeBox.setColour (juce::ComboBox::backgroundColourId, panel());
    quantizeBox.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    quantizeBox.setColour (juce::ComboBox::outlineColourId, line());
    addAndMakeVisible (quantizeBox);

    loopLengthBox.addItemList (LogicPerformanceLooperAudioProcessor::loopLengthNames(), 1);
    loopLengthBox.setColour (juce::ComboBox::backgroundColourId, panel());
    loopLengthBox.setColour (juce::ComboBox::textColourId, juce::Colours::white);
    loopLengthBox.setColour (juce::ComboBox::outlineColourId, line());
    addAndMakeVisible (loopLengthBox);

    slotCountLabel.setText ("Loops", juce::dontSendNotification);
    modeLabel.setText ("Mode", juce::dontSendNotification);
    launchLabel.setText ("Launch", juce::dontSendNotification);
    lengthLabel.setText ("Length", juce::dontSendNotification);
    thresholdLabel.setText ("Threshold", juce::dontSendNotification);
    preBufferLabel.setText ("Prebuf", juce::dontSendNotification);
    for (auto* label : { &slotCountLabel, &modeLabel, &launchLabel, &lengthLabel, &thresholdLabel, &preBufferLabel })
    {
        label->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.72f));
        addAndMakeVisible (*label);
    }

    configureSlider (slotCountSlider);
    configureSlider (thresholdSlider);
    configureSlider (preBufferSlider);
    configureButton (clearAllButton);
    configureButton (stopAllButton);
    configureButton (mapButton);
    configureButton (wetOnlyButton);
    wetOnlyButton.setClickingTogglesState (true);
    wetOnlyButton.setColour (juce::TextButton::buttonOnColourId, accent().withAlpha (0.85f));
    wetOnlyButton.setColour (juce::TextButton::textColourOnId, juce::Colours::black);

    clearAllButton.onClick = [this]
    {
        if (mappingMode)
            processorRef.startGlobalMappingLearn (looper::SlotAction::clearAll);
        else
            processorRef.pushGlobalCommand (looper::SlotAction::clearAll);
        grabKeyboardFocus();
    };
    stopAllButton.onClick = [this]
    {
        if (mappingMode)
            processorRef.startGlobalMappingLearn (looper::SlotAction::playStopAll);
        else
            processorRef.pushGlobalCommand (looper::SlotAction::playStopAll);
        grabKeyboardFocus();
    };
    mapButton.onClick = [this]
    {
        mappingMode = ! mappingMode;
        mapButton.setButtonText (mappingMode ? "Mapping" : "Map");
        repaint();
    };

    for (int i = 0; i < looper::maxSlots; ++i)
    {
        auto pad = std::make_unique<SlotPad>();
        pad->setSlotIndex (i);
        pad->onSelect = [this, i]
        {
            selectedLoop = i;
            grabKeyboardFocus();
        };
        pad->onArm = [this, i] { processorRef.pushSlotCommand (looper::SlotAction::arm, i); };
        pad->onPlay = [this, i] { processorRef.pushSlotCommand (looper::SlotAction::playStop, i); };
        pad->onOverdub = [this, i] { processorRef.pushSlotCommand (looper::SlotAction::overdub, i); };
        pad->onMute = [this, i] { processorRef.pushSlotCommand (looper::SlotAction::mute, i); };
        pad->onClear = [this, i] { processorRef.pushSlotCommand (looper::SlotAction::clear, i); };
        pad->onLearn = [this, i] (looper::SlotAction action)
        {
            selectedLoop = i;
            processorRef.startMappingLearn (i, action);
            grabKeyboardFocus();
        };

        addAndMakeVisible (*pad);
        pads[static_cast<size_t> (i)] = std::move (pad);
    }

    auto& state = processorRef.getValueTreeState();
    syncModeAttachment = std::make_unique<ComboBoxAttachment> (state, ParamIDs::syncMode, syncModeBox);
    quantizeAttachment = std::make_unique<ComboBoxAttachment> (state, ParamIDs::quantize, quantizeBox);
    loopLengthAttachment = std::make_unique<ComboBoxAttachment> (state, ParamIDs::loopLength, loopLengthBox);
    slotCountAttachment = std::make_unique<SliderAttachment> (state, ParamIDs::slotCount, slotCountSlider);
    thresholdAttachment = std::make_unique<SliderAttachment> (state, ParamIDs::threshold, thresholdSlider);
    preBufferAttachment = std::make_unique<SliderAttachment> (state, ParamIDs::preBufferMs, preBufferSlider);
    wetOnlyAttachment = std::make_unique<ButtonAttachment> (state, ParamIDs::wetOnly, wetOnlyButton);

    setWantsKeyboardFocus (true);
    setResizable (true, true);
    setResizeLimits (760, 520, 1800, 1200);
    setSize (980, 680);
    startTimerHz (30);
}

void LogicPerformanceLooperAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg());

    const auto snapshot = processorRef.getEngineSnapshot();
    auto meter = getLocalBounds().reduced (16).removeFromTop (84).removeFromBottom (8).toFloat();
    meter.setWidth (260.0f);
    g.setColour (juce::Colours::black.withAlpha (0.40f));
    g.fillRoundedRectangle (meter, 3.0f);
    g.setColour (snapshot.inputLevel >= snapshot.threshold ? juce::Colour (0xffffc857) : accent());
    g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * juce::jlimit (0.0f, 1.0f, snapshot.inputLevel * 3.0f)), 3.0f);
}

void LogicPerformanceLooperAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (16);
    auto header = bounds.removeFromTop (40);
    titleLabel.setBounds (header.removeFromLeft (160));
    statusLabel.setBounds (header.removeFromLeft (280));
    mapButton.setBounds (header.removeFromRight (78).reduced (2));
    wetOnlyButton.setBounds (header.removeFromRight (92).reduced (2));
    const auto snapshot = processorRef.getEngineSnapshot();
    stopAllButton.setButtonText (snapshot.anyActive ? "Stop All" : "Play All");
    stopAllButton.setBounds (header.removeFromRight (94).reduced (2));
    clearAllButton.setBounds (header.removeFromRight (96).reduced (2));
    loopLengthBox.setBounds (header.removeFromRight (136).reduced (2));
    lengthLabel.setBounds (header.removeFromRight (54));
    quantizeBox.setBounds (header.removeFromRight (106).reduced (2));
    launchLabel.setBounds (header.removeFromRight (58));
    syncModeBox.setBounds (header.removeFromRight (118).reduced (2));
    modeLabel.setBounds (header.removeFromRight (48));

    auto controls = bounds.removeFromTop (36);
    slotCountLabel.setBounds (controls.removeFromLeft (44));
    slotCountSlider.setBounds (controls.removeFromLeft (130).reduced (0, 4));
    thresholdLabel.setBounds (controls.removeFromLeft (76));
    thresholdSlider.setBounds (controls.removeFromLeft (170).reduced (0, 4));
    preBufferLabel.setBounds (controls.removeFromLeft (62));
    preBufferSlider.setBounds (controls.removeFromLeft (150).reduced (0, 4));
    bounds.removeFromTop (10);

    juce::String status;
    if (snapshot.host.hasPosition)
        status << (snapshot.syncMode == looper::SyncMode::dawSync ? "DAW Sync " : "DAW ")
               << juce::String (snapshot.host.bpm, 1) << " BPM  Bar " << snapshot.barCountdown;
    else
        status << "DAW --";

    if (snapshot.masterHasAudio)
        status << "  Master " << juce::String (snapshot.detectedMasterBpm, 1) << " BPM  " << snapshot.masterCountdown << "s";
    else
        status << "  Master --";
    statusLabel.setText (status, juce::dontSendNotification);

    const auto count = juce::jlimit (1, looper::maxSlots, processorRef.getLoopEngine().getActiveSlotCount());
    const auto columns = count <= 8 ? 4 : (count <= 16 ? 4 : 8);
    const auto rows = (count + columns - 1) / columns;
    auto grid = bounds;
    const auto cellW = grid.getWidth() / columns;
    const auto cellH = grid.getHeight() / rows;

    for (int i = 0; i < looper::maxSlots; ++i)
    {
        if (pads[static_cast<size_t> (i)] == nullptr)
            continue;

        const auto visible = i < count;
        pads[static_cast<size_t> (i)]->setVisible (visible);
        pads[static_cast<size_t> (i)]->setSelected (i == selectedLoop);
        pads[static_cast<size_t> (i)]->setMappingMode (mappingMode);
        if (! visible)
            continue;

        pads[static_cast<size_t> (i)]->setSnapshot (snapshot.slots[static_cast<size_t> (i)]);
        pads[static_cast<size_t> (i)]->setBounds (grid.getX() + (i % columns) * cellW,
                                                  grid.getY() + (i / columns) * cellH,
                                                  cellW - 8,
                                                  cellH - 8);
    }
}

bool LogicPerformanceLooperAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    const auto text = key.getTextCharacter();
    if (processorRef.isLearningMapping())
    {
        processorRef.learnKeyboardKey (key.getKeyCode());
        mappingMode = false;
        mapButton.setButtonText ("Map");
        return true;
    }

    if (text >= '1' && text <= '9')
    {
        selectedLoop = juce::jlimit (0, processorRef.getLoopEngine().getActiveSlotCount() - 1, text - '1');
        triggerSelected (looper::SlotAction::playStop);
        return true;
    }

    if (key == juce::KeyPress::backspaceKey)
    {
        triggerSelected (looper::SlotAction::clear);
        return true;
    }

    if (processorRef.handleKeyboardKey (key.getKeyCode()))
        return true;

    if (key == juce::KeyPress::spaceKey)
    {
        triggerSelected (looper::SlotAction::playStop);
        return true;
    }

    if (text == 'a' || text == 'A')
    {
        triggerSelected (looper::SlotAction::arm);
        return true;
    }

    if (text == 'd' || text == 'D')
    {
        triggerSelected (looper::SlotAction::overdub);
        return true;
    }

    if (text == 'm' || text == 'M')
    {
        triggerSelected (looper::SlotAction::mute);
        return true;
    }

    if (text == 'c' || text == 'C')
    {
        triggerSelected (looper::SlotAction::clear);
        return true;
    }

    return false;
}

void LogicPerformanceLooperAudioProcessorEditor::timerCallback()
{
    resized();
    repaint();
}

void LogicPerformanceLooperAudioProcessorEditor::configureSlider (juce::Slider& slider)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 20);
    slider.setColour (juce::Slider::trackColourId, accent());
    slider.setColour (juce::Slider::thumbColourId, juce::Colours::white);
    addAndMakeVisible (slider);
}

void LogicPerformanceLooperAudioProcessorEditor::configureButton (juce::TextButton& button)
{
    button.setColour (juce::TextButton::buttonColourId, panel());
    button.setColour (juce::TextButton::textColourOffId, juce::Colours::white);
    addAndMakeVisible (button);
}

void LogicPerformanceLooperAudioProcessorEditor::triggerSelected (looper::SlotAction action)
{
    const auto count = processorRef.getLoopEngine().getActiveSlotCount();
    selectedLoop = juce::jlimit (0, juce::jmax (0, count - 1), selectedLoop);
    processorRef.pushSlotCommand (action, selectedLoop);
}
