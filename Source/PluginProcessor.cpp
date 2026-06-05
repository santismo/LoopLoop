#include "PluginProcessor.h"
#include "PluginEditor.h"

LogicPerformanceLooperAudioProcessor::LogicPerformanceLooperAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      parameters (*this, nullptr, "Parameters", createParameterLayout())
{
    resetDefaultMappings();
}

juce::AudioProcessorValueTreeState::ParameterLayout LogicPerformanceLooperAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back (std::make_unique<juce::AudioParameterInt> (ParamIDs::slotCount, "Loop Count", 1, looper::maxSlots, looper::defaultSlots));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (ParamIDs::quantize, "Launch Timing", quantizeNames(), 2));
    params.push_back (std::make_unique<juce::AudioParameterInt> (ParamIDs::masterSlot, "Master Loop", 1, looper::maxSlots, 1));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (ParamIDs::loopLength, "Loop Length", loopLengthNames(), 1));
    params.push_back (std::make_unique<juce::AudioParameterChoice> (ParamIDs::syncMode, "Mode", syncModeNames(), 0));
    params.push_back (std::make_unique<juce::AudioParameterFloat> (ParamIDs::threshold, "Threshold", juce::NormalisableRange<float> (-72.0f, -6.0f, 0.1f), -36.0f));
    params.push_back (std::make_unique<juce::AudioParameterInt> (ParamIDs::preBufferMs, "Pre Buffer", 0, 500, 80));
    params.push_back (std::make_unique<juce::AudioParameterBool> (ParamIDs::wetOnly, "Wet Only", false));
    return { params.begin(), params.end() };
}

juce::StringArray LogicPerformanceLooperAudioProcessor::quantizeNames()
{
    return { "Now", "Beat", "Bar", "Loop" };
}

juce::StringArray LogicPerformanceLooperAudioProcessor::loopLengthNames()
{
    return { "Free Master", "Auto Master", "Same Master", "1/2 Master", "1/4 Master", "2x Master", "4x Master", "1 Bar", "2 Bars", "4 Bars", "8 Bars", "16 Bars" };
}

juce::StringArray LogicPerformanceLooperAudioProcessor::syncModeNames()
{
    return { "Standalone", "DAW Sync" };
}

void LogicPerformanceLooperAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    syncEngineSettings();
}

bool LogicPerformanceLooperAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& input = layouts.getMainInputChannelSet();
    const auto& output = layouts.getMainOutputChannelSet();
    return input == output && (output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo());
}

void LogicPerformanceLooperAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());

    syncEngineSettings();
    handleMidiMappings (midiMessages);
    const auto wetOnly = parameters.getRawParameterValue (ParamIDs::wetOnly) != nullptr
        && parameters.getRawParameterValue (ParamIDs::wetOnly)->load() >= 0.5f;
    engine.process (buffer, midiMessages, readHostTiming(), currentQuantize(), ! wetOnly);
}

juce::AudioProcessorEditor* LogicPerformanceLooperAudioProcessor::createEditor()
{
    return new LogicPerformanceLooperAudioProcessorEditor (*this);
}

void LogicPerformanceLooperAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    auto* mappings = xml->createNewChildElement ("Mappings");
    for (int i = 0; i < mappingCount; ++i)
    {
        mappings->setAttribute ("key" + juce::String (i), keyBindings[static_cast<size_t> (i)].load());
        mappings->setAttribute ("note" + juce::String (i), midiNoteBindings[static_cast<size_t> (i)].load());
        mappings->setAttribute ("cc" + juce::String (i), midiCcBindings[static_cast<size_t> (i)].load());
    }
    for (int i = 0; i < globalMappingCount; ++i)
    {
        mappings->setAttribute ("globalKey" + juce::String (i), globalKeyBindings[static_cast<size_t> (i)].load());
        mappings->setAttribute ("globalNote" + juce::String (i), globalMidiNoteBindings[static_cast<size_t> (i)].load());
        mappings->setAttribute ("globalCc" + juce::String (i), globalMidiCcBindings[static_cast<size_t> (i)].load());
    }
    copyXmlToBinary (*xml, destData);
}

void LogicPerformanceLooperAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (parameters.state.getType()))
    {
        resetDefaultMappings();
        if (auto* mappings = xml->getChildByName ("Mappings"))
        {
            for (int i = 0; i < mappingCount; ++i)
            {
                keyBindings[static_cast<size_t> (i)].store (mappings->getIntAttribute ("key" + juce::String (i), keyBindings[static_cast<size_t> (i)].load()));
                midiNoteBindings[static_cast<size_t> (i)].store (mappings->getIntAttribute ("note" + juce::String (i), midiNoteBindings[static_cast<size_t> (i)].load()));
                midiCcBindings[static_cast<size_t> (i)].store (mappings->getIntAttribute ("cc" + juce::String (i), midiCcBindings[static_cast<size_t> (i)].load()));
            }
            for (int i = 0; i < globalMappingCount; ++i)
            {
                globalKeyBindings[static_cast<size_t> (i)].store (mappings->getIntAttribute ("globalKey" + juce::String (i), globalKeyBindings[static_cast<size_t> (i)].load()));
                globalMidiNoteBindings[static_cast<size_t> (i)].store (mappings->getIntAttribute ("globalNote" + juce::String (i), globalMidiNoteBindings[static_cast<size_t> (i)].load()));
                globalMidiCcBindings[static_cast<size_t> (i)].store (mappings->getIntAttribute ("globalCc" + juce::String (i), globalMidiCcBindings[static_cast<size_t> (i)].load()));
            }
        }
        parameters.replaceState (juce::ValueTree::fromXml (*xml));
    }
}

void LogicPerformanceLooperAudioProcessor::pushSlotCommand (looper::SlotAction action, int slot)
{
    engine.pushCommand ({ action, slot, currentQuantize(), 0.0 });
}

void LogicPerformanceLooperAudioProcessor::pushGlobalCommand (looper::SlotAction action)
{
    engine.pushCommand ({ action, 0, currentQuantize(), 0.0 });
}

void LogicPerformanceLooperAudioProcessor::startMappingLearn (int slot, looper::SlotAction action) noexcept
{
    learningSlot.store (juce::jlimit (0, looper::maxSlots - 1, slot));
    learningAction.store (actionIndex (action));
}

void LogicPerformanceLooperAudioProcessor::startGlobalMappingLearn (looper::SlotAction action) noexcept
{
    learningSlot.store (-2);
    learningAction.store (globalActionIndex (action));
}

bool LogicPerformanceLooperAudioProcessor::learnKeyboardKey (int keyCode) noexcept
{
    const auto slot = learningSlot.exchange (-1);
    const auto action = learningAction.exchange (-1);
    if (slot < 0 || action < 0)
    {
        if (slot == -2 && action >= 0)
        {
            globalKeyBindings[static_cast<size_t> (juce::jlimit (0, globalMappingCount - 1, action))].store (keyCode);
            return true;
        }
        return false;
    }

    keyBindings[static_cast<size_t> (mappingIndex (slot, indexToAction (action)))].store (keyCode);
    return true;
}

bool LogicPerformanceLooperAudioProcessor::handleKeyboardKey (int keyCode)
{
    for (int action = 0; action < globalMappingCount; ++action)
    {
        if (globalKeyBindings[static_cast<size_t> (action)].load() == keyCode)
        {
            pushGlobalCommand (indexToGlobalAction (action));
            return true;
        }
    }

    for (int i = 0; i < engine.getActiveSlotCount(); ++i)
    {
        for (int action = 0; action < actionsPerLoop; ++action)
        {
            const auto index = mappingIndex (i, indexToAction (action));
            if (keyBindings[static_cast<size_t> (index)].load() == keyCode)
            {
                pushSlotCommand (indexToAction (action), i);
                return true;
            }
        }
    }

    return false;
}

juce::String LogicPerformanceLooperAudioProcessor::getMappingDescription (int slot, looper::SlotAction action) const
{
    const auto index = mappingIndex (slot, action);
    const auto key = keyBindings[static_cast<size_t> (index)].load();
    const auto note = midiNoteBindings[static_cast<size_t> (index)].load();
    const auto cc = midiCcBindings[static_cast<size_t> (index)].load();
    juce::StringArray parts;
    if (key >= 0)
        parts.add ("Key " + juce::String (key));
    if (note >= 0)
        parts.add ("N" + juce::String (note));
    if (cc >= 0)
        parts.add ("CC" + juce::String (cc));
    return parts.isEmpty() ? "-" : parts.joinIntoString (" / ");
}

looper::HostTiming LogicPerformanceLooperAudioProcessor::readHostTiming() const
{
    looper::HostTiming timing;

    if (const auto* playHead = getPlayHead())
    {
        const auto position = playHead->getPosition();
        if (position.hasValue())
        {
            timing.hasPosition = true;
            timing.isPlaying = position->getIsPlaying();

            if (const auto bpm = position->getBpm())
                timing.bpm = juce::jlimit (20.0, 320.0, *bpm);

            if (const auto ppq = position->getPpqPosition())
                timing.ppq = *ppq;

            if (const auto signature = position->getTimeSignature())
            {
                timing.numerator = signature->numerator;
                timing.denominator = signature->denominator;
            }
        }
    }

    return timing;
}

looper::QuantizeMode LogicPerformanceLooperAudioProcessor::currentQuantize() const noexcept
{
    if (const auto* raw = parameters.getRawParameterValue (ParamIDs::quantize))
        return static_cast<looper::QuantizeMode> (juce::jlimit (0, 3, juce::roundToInt (raw->load())));

    return looper::QuantizeMode::nextBar;
}

void LogicPerformanceLooperAudioProcessor::syncEngineSettings()
{
    auto slotCount = looper::defaultSlots;
    if (const auto* raw = parameters.getRawParameterValue (ParamIDs::slotCount))
        slotCount = juce::roundToInt (raw->load());

    engine.setActiveSlotCount (slotCount);

    if (const auto* raw = parameters.getRawParameterValue (ParamIDs::masterSlot))
        engine.setMasterSlot (juce::roundToInt (raw->load()) - 1);

    if (const auto* raw = parameters.getRawParameterValue (ParamIDs::syncMode))
        engine.setSyncMode (juce::roundToInt (raw->load()) == 1 ? looper::SyncMode::dawSync : looper::SyncMode::standalone);

    looper::SlotSettings common;
    if (const auto* raw = parameters.getRawParameterValue (ParamIDs::threshold))
        common.thresholdDb = raw->load();
    if (const auto* raw = parameters.getRawParameterValue (ParamIDs::preBufferMs))
        common.preBufferMs = juce::roundToInt (raw->load());

    const auto loopLengthIndex = parameters.getRawParameterValue (ParamIDs::loopLength) != nullptr
        ? juce::roundToInt (parameters.getRawParameterValue (ParamIDs::loopLength)->load())
        : 1;
    switch (loopLengthIndex)
    {
        case 0: common.lengthMode = looper::LengthMode::free; common.fixedBars = 0; common.autoStop = looper::AutoStopMode::manual; break;
        case 1: common.lengthMode = looper::LengthMode::autoMaster; common.fixedBars = 0; common.autoStop = looper::AutoStopMode::manual; break;
        case 2: common.lengthMode = looper::LengthMode::sameAsMaster; common.fixedBars = 0; common.autoStop = looper::AutoStopMode::manual; break;
        case 3: common.lengthMode = looper::LengthMode::halfMaster; common.fixedBars = 0; common.autoStop = looper::AutoStopMode::manual; break;
        case 4: common.lengthMode = looper::LengthMode::quarterMaster; common.fixedBars = 0; common.autoStop = looper::AutoStopMode::manual; break;
        case 5: common.lengthMode = looper::LengthMode::doubleMaster; common.fixedBars = 0; common.autoStop = looper::AutoStopMode::manual; break;
        case 6: common.lengthMode = looper::LengthMode::quadrupleMaster; common.fixedBars = 0; common.autoStop = looper::AutoStopMode::manual; break;
        case 7: common.lengthMode = looper::LengthMode::fixedBars; common.fixedBars = 1; common.autoStop = looper::AutoStopMode::oneBar; break;
        case 8: common.lengthMode = looper::LengthMode::fixedBars; common.fixedBars = 2; common.autoStop = looper::AutoStopMode::twoBars; break;
        case 10: common.lengthMode = looper::LengthMode::fixedBars; common.fixedBars = 8; common.autoStop = looper::AutoStopMode::eightBars; break;
        case 11: common.lengthMode = looper::LengthMode::fixedBars; common.fixedBars = 16; common.autoStop = looper::AutoStopMode::sixteenBars; break;
        case 9:
        default: common.lengthMode = looper::LengthMode::fixedBars; common.fixedBars = 4; common.autoStop = looper::AutoStopMode::fourBars; break;
    }

    for (int i = 0; i < looper::maxSlots; ++i)
    {
        auto settings = engine.getSlotSettings (i);
        settings.thresholdDb = common.thresholdDb;
        settings.preBufferMs = common.preBufferMs;
        settings.lengthMode = common.lengthMode;
        settings.fixedBars = common.fixedBars;
        settings.autoStop = common.autoStop;
        engine.setSlotSettings (i, settings);
    }
}

void LogicPerformanceLooperAudioProcessor::resetDefaultMappings() noexcept
{
    for (auto& value : keyBindings)
        value.store (-1);
    for (auto& value : midiNoteBindings)
        value.store (-1);
    for (auto& value : midiCcBindings)
        value.store (-1);
    for (auto& value : globalKeyBindings)
        value.store (-1);
    for (auto& value : globalMidiNoteBindings)
        value.store (-1);
    for (auto& value : globalMidiCcBindings)
        value.store (-1);

    globalKeyBindings[static_cast<size_t> (globalActionIndex (looper::SlotAction::playStopAll))].store ('0');
    globalMidiCcBindings[static_cast<size_t> (globalActionIndex (looper::SlotAction::playStopAll))].store (100);
    globalMidiCcBindings[static_cast<size_t> (globalActionIndex (looper::SlotAction::clearAll))].store (101);

    for (int slot = 0; slot < looper::maxSlots; ++slot)
    {
        if (slot < 9)
            keyBindings[static_cast<size_t> (mappingIndex (slot, looper::SlotAction::playStop))].store ('1' + slot);

        midiNoteBindings[static_cast<size_t> (mappingIndex (slot, looper::SlotAction::playStop))].store (36 + slot);
        midiNoteBindings[static_cast<size_t> (mappingIndex (slot, looper::SlotAction::arm))].store (68 + slot);
        midiCcBindings[static_cast<size_t> (mappingIndex (slot, looper::SlotAction::playStop))].store (20 + slot);
        midiCcBindings[static_cast<size_t> (mappingIndex (slot, looper::SlotAction::arm))].store (52 + slot);
        midiCcBindings[static_cast<size_t> (mappingIndex (slot, looper::SlotAction::mute))].store (84 + slot);
    }
}

void LogicPerformanceLooperAudioProcessor::handleMidiMappings (const juce::MidiBuffer& midi)
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const auto learnSlot = learningSlot.load();
        const auto learnAction = learningAction.load();

        if (message.isNoteOn())
        {
            if (learnSlot >= 0 && learnAction >= 0)
            {
                midiNoteBindings[static_cast<size_t> (mappingIndex (learnSlot, indexToAction (learnAction)))].store (message.getNoteNumber());
                learningSlot.store (-1);
                learningAction.store (-1);
                continue;
            }
            if (learnSlot == -2 && learnAction >= 0)
            {
                globalMidiNoteBindings[static_cast<size_t> (juce::jlimit (0, globalMappingCount - 1, learnAction))].store (message.getNoteNumber());
                learningSlot.store (-1);
                learningAction.store (-1);
                continue;
            }

            for (int action = 0; action < globalMappingCount; ++action)
                if (globalMidiNoteBindings[static_cast<size_t> (action)].load() == message.getNoteNumber())
                    pushGlobalCommand (indexToGlobalAction (action));

            for (int slot = 0; slot < engine.getActiveSlotCount(); ++slot)
                for (int action = 0; action < actionsPerLoop; ++action)
                    if (midiNoteBindings[static_cast<size_t> (mappingIndex (slot, indexToAction (action)))].load() == message.getNoteNumber())
                        pushSlotCommand (indexToAction (action), slot);
        }

        if (message.isController() && message.getControllerValue() >= 64)
        {
            if (learnSlot >= 0 && learnAction >= 0)
            {
                midiCcBindings[static_cast<size_t> (mappingIndex (learnSlot, indexToAction (learnAction)))].store (message.getControllerNumber());
                learningSlot.store (-1);
                learningAction.store (-1);
                continue;
            }
            if (learnSlot == -2 && learnAction >= 0)
            {
                globalMidiCcBindings[static_cast<size_t> (juce::jlimit (0, globalMappingCount - 1, learnAction))].store (message.getControllerNumber());
                learningSlot.store (-1);
                learningAction.store (-1);
                continue;
            }

            for (int action = 0; action < globalMappingCount; ++action)
                if (globalMidiCcBindings[static_cast<size_t> (action)].load() == message.getControllerNumber())
                    pushGlobalCommand (indexToGlobalAction (action));

            for (int slot = 0; slot < engine.getActiveSlotCount(); ++slot)
                for (int action = 0; action < actionsPerLoop; ++action)
                    if (midiCcBindings[static_cast<size_t> (mappingIndex (slot, indexToAction (action)))].load() == message.getControllerNumber())
                        pushSlotCommand (indexToAction (action), slot);
        }
    }
}

int LogicPerformanceLooperAudioProcessor::actionIndex (looper::SlotAction action) noexcept
{
    switch (action)
    {
        case looper::SlotAction::arm: return 0;
        case looper::SlotAction::playStop: return 1;
        case looper::SlotAction::overdub: return 2;
        case looper::SlotAction::mute: return 3;
        case looper::SlotAction::clear: return 4;
        case looper::SlotAction::record:
        case looper::SlotAction::clearAll:
        case looper::SlotAction::stopAll:
        case looper::SlotAction::playAll:
        case looper::SlotAction::playStopAll:
        default: return 1;
    }
}

looper::SlotAction LogicPerformanceLooperAudioProcessor::indexToAction (int index) noexcept
{
    switch (index)
    {
        case 0: return looper::SlotAction::arm;
        case 1: return looper::SlotAction::playStop;
        case 2: return looper::SlotAction::overdub;
        case 3: return looper::SlotAction::mute;
        case 4: return looper::SlotAction::clear;
        default: return looper::SlotAction::playStop;
    }
}

int LogicPerformanceLooperAudioProcessor::mappingIndex (int slot, looper::SlotAction action) noexcept
{
    return juce::jlimit (0, looper::maxSlots - 1, slot) * actionsPerLoop + actionIndex (action);
}

int LogicPerformanceLooperAudioProcessor::globalActionIndex (looper::SlotAction action) noexcept
{
    return action == looper::SlotAction::clearAll ? 1 : 0;
}

looper::SlotAction LogicPerformanceLooperAudioProcessor::indexToGlobalAction (int index) noexcept
{
    return index == 1 ? looper::SlotAction::clearAll : looper::SlotAction::playStopAll;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LogicPerformanceLooperAudioProcessor();
}
