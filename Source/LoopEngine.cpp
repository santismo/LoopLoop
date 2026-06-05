#include "LoopEngine.h"

namespace
{
    float dbToGain (float db) noexcept
    {
        return std::pow (10.0f, db / 20.0f);
    }

    int autoStopBars (looper::AutoStopMode mode) noexcept
    {
        switch (mode)
        {
            case looper::AutoStopMode::oneBar: return 1;
            case looper::AutoStopMode::twoBars: return 2;
            case looper::AutoStopMode::fourBars: return 4;
            case looper::AutoStopMode::eightBars: return 8;
            case looper::AutoStopMode::sixteenBars: return 16;
            case looper::AutoStopMode::manual: return 0;
            default: return 0;
        }
    }
}

void LoopEngine::prepare (double newSampleRate, int)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    maxLoopSamples = juce::jmax (1, static_cast<int> (std::ceil (sampleRate * maxLoopSeconds)));
    maxPreBufferSamples = juce::jmax (1, static_cast<int> (std::ceil (sampleRate * maxPreBufferSeconds)));

    for (auto& slot : slots)
    {
        slot.loop.setSize (looper::maxChannels, maxLoopSamples, false, true, true);
        slot.preBuffer.setSize (looper::maxChannels, maxPreBufferSamples, false, true, true);
        clearSlot (slot);
    }

    commandQueue.reset();
    pendingCommands = {};
}

void LoopEngine::reset() noexcept
{
    for (auto& slot : slots)
        clearSlot (slot);

    commandQueue.reset();
    pendingCommands = {};
}

bool LoopEngine::pushCommand (const looper::ControlCommand& command) noexcept
{
    return commandQueue.push (command);
}

void LoopEngine::setSlotSettings (int slot, const looper::SlotSettings& settings)
{
    if (slot < 0 || slot >= looper::maxSlots)
        return;

    slots[static_cast<size_t> (slot)].settings = settings;
}

looper::SlotSettings LoopEngine::getSlotSettings (int slot) const
{
    if (slot < 0 || slot >= looper::maxSlots)
        return {};

    return slots[static_cast<size_t> (slot)].settings;
}

void LoopEngine::setMasterSlot (int slot) noexcept
{
    masterSlot = juce::jlimit (0, activeSlotCount - 1, slot);
}

void LoopEngine::setActiveSlotCount (int count) noexcept
{
    activeSlotCount = juce::jlimit (1, looper::maxSlots, count);
    masterSlot = juce::jlimit (0, activeSlotCount - 1, masterSlot);
}

looper::EngineSnapshot LoopEngine::getSnapshot() const noexcept
{
    looper::EngineSnapshot snapshot;
    snapshot.host = lastHost;
    snapshot.quantize = lastQuantize;
    snapshot.masterSlot = masterSlot;
    snapshot.masterHasAudio = masterHasAudio();
    snapshot.detectedMasterBpm = estimateMasterBpm();
    snapshot.barCountdown = countdownToHostBar (lastHost);
    snapshot.masterCountdown = countdownToMasterStart();
    snapshot.inputLevel = globalInputLevel;
    snapshot.syncMode = syncMode;

    for (int i = 0; i < activeSlotCount; ++i)
    {
        const auto& slot = slots[static_cast<size_t> (i)];
        auto& target = snapshot.slots[static_cast<size_t> (i)];
        target.state = slot.state;
        target.muted = slot.muted;
        target.hasAudio = slot.lengthSamples > 0;
        target.pendingStart = slot.pendingRecordOnMasterStart || slot.pendingPlayOnMasterStart;
        target.pendingStop = slot.pendingStopOnMasterStart;
        target.inputLevel = slot.inputLevel;
        target.threshold = dbToGain (slot.settings.thresholdDb);
        target.lengthBeats = slot.lengthBeats;
        target.midiNote = slot.settings.midiNote;
        target.progress = slot.lengthSamples > 0
            ? static_cast<double> (slot.playPosition) / static_cast<double> (slot.lengthSamples)
            : 0.0;
        snapshot.anyActive = snapshot.anyActive
            || slot.state == looper::SlotState::armed
            || slot.state == looper::SlotState::recording
            || slot.state == looper::SlotState::playing
            || slot.state == looper::SlotState::overdubbing
            || slot.pendingRecordOnMasterStart
            || slot.pendingPlayOnMasterStart
            || slot.pendingStopOnMasterStart;
    }

    if (activeSlotCount > 0)
        snapshot.threshold = dbToGain (slots[static_cast<size_t> (masterSlot)].settings.thresholdDb);

    return snapshot;
}

void LoopEngine::process (juce::AudioBuffer<float>& buffer,
                          const juce::MidiBuffer& midi,
                          const looper::HostTiming& host,
                          looper::QuantizeMode quantize,
                          bool passInputThrough)
{
    lastHost = host;
    lastQuantize = quantize;
    drainCommandQueue (host, quantize);

    juce::ignoreUnused (midi);

    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;
    const auto numSamples = buffer.getNumSamples();

    for (int sample = 0; sample < numSamples; ++sample)
    {
        for (auto& pending : pendingCommands)
        {
            if (shouldTriggerPending (pending, host, sample, numSamples))
            {
                executeCommand (pending.command, sample, host);
                pending.active = false;
            }
        }

        triggerMasterBoundary (host);

        auto outL = passInputThrough ? left[sample] : 0.0f;
        auto outR = passInputThrough ? right[sample] : 0.0f;
        const auto inL = left + sample;
        const auto inR = right + sample;

        for (int slotIndex = 0; slotIndex < activeSlotCount; ++slotIndex)
            processSlotSample (slots[static_cast<size_t> (slotIndex)], slotIndex, inL, inR, outL, outR, host);

        left[sample] = juce::jlimit (-1.0f, 1.0f, outL);
        right[sample] = juce::jlimit (-1.0f, 1.0f, outR);
    }
}

void LoopEngine::drainCommandQueue (const looper::HostTiming& host, looper::QuantizeMode quantize) noexcept
{
    looper::ControlCommand command;
    while (commandQueue.pop (command))
    {
        const auto actionNeedsSync = command.action == looper::SlotAction::arm
            || command.action == looper::SlotAction::record
            || command.action == looper::SlotAction::playStop
            || command.action == looper::SlotAction::playAll
            || command.action == looper::SlotAction::playStopAll
            || command.action == looper::SlotAction::stopAll;
        const auto effectiveQuantize = syncMode == looper::SyncMode::dawSync && actionNeedsSync
            ? looper::QuantizeMode::nextBar
            : (command.quantize == looper::QuantizeMode::immediate ? command.quantize : quantize);
        scheduleCommand (command, host, effectiveQuantize);
    }
}

void LoopEngine::scheduleCommand (const looper::ControlCommand& command,
                                  const looper::HostTiming& host,
                                  looper::QuantizeMode quantize) noexcept
{
    if (shouldSlaveToMaster (command.slot)
        && (command.action == looper::SlotAction::arm
            || command.action == looper::SlotAction::record
            || command.action == looper::SlotAction::playStop))
    {
        executeCommand (command, 0, host);
        return;
    }

    if (command.action == looper::SlotAction::clear || command.action == looper::SlotAction::clearAll || quantize == looper::QuantizeMode::immediate || ! host.hasPosition)
    {
        executeCommand (command, 0, host);
        return;
    }

    for (auto& pending : pendingCommands)
    {
        if (! pending.active)
        {
            pending.command = command;
            pending.targetPpq = targetPpqFor (quantize, command.slot, host);
            pending.active = true;
            return;
        }
    }

    executeCommand (command, 0, host);
}

void LoopEngine::executeCommand (const looper::ControlCommand& command, int, const looper::HostTiming& host) noexcept
{
    if (command.action == looper::SlotAction::clearAll)
    {
        for (auto& slot : slots)
            clearSlot (slot);
        return;
    }

    if (command.action == looper::SlotAction::playStopAll)
    {
        bool anyActive = false;
        for (int i = 0; i < activeSlotCount; ++i)
        {
            const auto& current = slots[static_cast<size_t> (i)];
            anyActive = anyActive
                || current.state == looper::SlotState::armed
                || current.state == looper::SlotState::recording
                || current.state == looper::SlotState::playing
                || current.state == looper::SlotState::overdubbing
                || current.pendingRecordOnMasterStart
                || current.pendingPlayOnMasterStart
                || current.pendingStopOnMasterStart;
        }

        executeCommand ({ anyActive ? looper::SlotAction::stopAll : looper::SlotAction::playAll, 0, command.quantize, command.requestedPpq }, 0, host);
        return;
    }

    if (command.action == looper::SlotAction::stopAll)
    {
        for (auto& slot : slots)
        {
            slot.pendingRecordOnMasterStart = false;
            slot.pendingPlayOnMasterStart = false;
            slot.pendingStopOnMasterStart = false;
            if (slot.state != looper::SlotState::empty)
                slot.state = looper::SlotState::stopped;
        }
        return;
    }

    if (command.action == looper::SlotAction::playAll)
    {
        for (int i = 0; i < activeSlotCount; ++i)
        {
            auto& current = slots[static_cast<size_t> (i)];
            if (current.lengthSamples <= 0)
                continue;

            if (shouldSlaveToMaster (i))
            {
                current.pendingPlayOnMasterStart = true;
                current.pendingStopOnMasterStart = false;
                current.state = looper::SlotState::armed;
            }
            else
            {
                current.playPosition = phasePositionFor (current, host);
                current.state = looper::SlotState::playing;
                current.fadeTarget = current.muted ? 0.0f : 1.0f;
            }
        }
        return;
    }

    if (command.slot < 0 || command.slot >= activeSlotCount)
        return;

    auto& slot = slots[static_cast<size_t> (command.slot)];
    const auto slaveToMaster = shouldSlaveToMaster (command.slot);
    switch (command.action)
    {
        case looper::SlotAction::arm:
            if (slaveToMaster && slot.lengthSamples == 0)
            {
                const auto shouldQueue = ! slot.pendingRecordOnMasterStart;
                slot.pendingRecordOnMasterStart = shouldQueue;
                slot.pendingPlayOnMasterStart = false;
                slot.pendingStopOnMasterStart = false;
                slot.state = shouldQueue ? looper::SlotState::armed : looper::SlotState::empty;
            }
            else if (slot.state == looper::SlotState::armed)
                slot.state = slot.lengthSamples > 0 ? looper::SlotState::stopped : looper::SlotState::empty;
            else
                slot.state = looper::SlotState::armed;
            break;

        case looper::SlotAction::record:
            if (slot.state == looper::SlotState::recording)
            {
                if (slaveToMaster && isAutoMaster (command.slot))
                    slot.pendingStopOnMasterStart = true;
                else
                    finishRecording (command.slot, host);
            }
            else if (slaveToMaster)
            {
                slot.pendingRecordOnMasterStart = true;
                slot.pendingPlayOnMasterStart = false;
                slot.pendingStopOnMasterStart = false;
                slot.state = looper::SlotState::armed;
            }
            else
                beginRecording (command.slot, host, true);
            break;

        case looper::SlotAction::overdub:
            if (slot.lengthSamples > 0)
                slot.state = slot.state == looper::SlotState::overdubbing ? looper::SlotState::playing : looper::SlotState::overdubbing;
            break;

        case looper::SlotAction::playStop:
            if (slot.pendingRecordOnMasterStart || slot.pendingPlayOnMasterStart || slot.pendingStopOnMasterStart)
            {
                slot.pendingRecordOnMasterStart = false;
                slot.pendingPlayOnMasterStart = false;
                slot.pendingStopOnMasterStart = false;
                slot.state = slot.lengthSamples > 0 ? looper::SlotState::stopped : looper::SlotState::empty;
            }
            else if (slot.state == looper::SlotState::recording)
            {
                if (slaveToMaster && isAutoMaster (command.slot))
                    slot.pendingStopOnMasterStart = true;
                else
                    finishRecording (command.slot, host);
            }
            else if (slot.lengthSamples > 0)
            {
                const auto wasPlaying = slot.state == looper::SlotState::playing || slot.state == looper::SlotState::overdubbing;
                if (wasPlaying)
                    slot.state = looper::SlotState::stopped;
                else if (slaveToMaster)
                {
                    slot.pendingPlayOnMasterStart = true;
                    slot.state = looper::SlotState::armed;
                }
                else
                {
                    slot.state = looper::SlotState::playing;
                    slot.playPosition = phasePositionFor (slot, host);
                }
            }
            else if (slaveToMaster)
            {
                slot.pendingRecordOnMasterStart = true;
                slot.pendingPlayOnMasterStart = false;
                slot.pendingStopOnMasterStart = false;
                slot.state = looper::SlotState::armed;
            }
            else
                beginRecording (command.slot, host, true);
            break;

        case looper::SlotAction::mute:
            slot.muted = ! slot.muted;
            slot.fadeTarget = slot.muted ? 0.0f : 1.0f;
            break;

        case looper::SlotAction::clear:
            clearSlot (slot);
            break;

        case looper::SlotAction::clearAll:
        case looper::SlotAction::stopAll:
        case looper::SlotAction::playAll:
        case looper::SlotAction::playStopAll:
        default:
            break;
    }
}

void LoopEngine::processSlotSample (Slot& slot, int slotIndex, const float* inL, const float* inR, float& outL, float& outR, const looper::HostTiming& host) noexcept
{
    const auto inputL = *inL;
    const auto inputR = *inR;
    const auto peak = juce::jmax (std::abs (inputL), std::abs (inputR));
    globalInputLevel = globalInputLevel * 0.995f + peak * 0.005f;
    slot.inputLevel = slot.inputLevel * 0.995f + peak * 0.005f;
    updatePreBuffer (slot, inputL, inputR);

    if (slot.state == looper::SlotState::armed
        && ! slot.pendingRecordOnMasterStart
        && ! shouldSlaveToMaster (slotIndex)
        && canRecordNow (host)
        && peak >= dbToGain (slot.settings.thresholdDb))
        beginRecording (slotIndex, host, true);

    if (slot.state == looper::SlotState::recording)
    {
        if (slot.recordPosition < maxLoopSamples)
        {
            slot.loop.setSample (0, slot.recordPosition, inputL);
            slot.loop.setSample (1, slot.recordPosition, inputR);
            ++slot.recordPosition;
        }

        if ((slot.autoStopSamples > 0 && slot.recordPosition >= slot.autoStopSamples) || slot.recordPosition >= maxLoopSamples)
            finishRecording (slotIndex, host);
    }

    if (slot.lengthSamples <= 0)
        return;

    if (slot.state == looper::SlotState::playing || slot.state == looper::SlotState::overdubbing)
    {
        const auto suppressStartCrossfade = slot.suppressStartCrossfadeSamples > 0;
        const auto loopL = crossfadedSample (slot, 0, slot.playPosition, suppressStartCrossfade);
        const auto loopR = crossfadedSample (slot, 1, slot.playPosition, suppressStartCrossfade);

        slot.fadeGain += (slot.fadeTarget - slot.fadeGain) * 0.0015f;
        outL += loopL * slot.fadeGain;
        outR += loopR * slot.fadeGain;

        if (slot.state == looper::SlotState::overdubbing)
        {
            slot.loop.setSample (0, slot.playPosition, juce::jlimit (-1.0f, 1.0f, loopL * 0.96f + inputL * 0.70f));
            slot.loop.setSample (1, slot.playPosition, juce::jlimit (-1.0f, 1.0f, loopR * 0.96f + inputR * 0.70f));
        }

        slot.playPosition = (slot.playPosition + 1) % slot.lengthSamples;
        if (slot.suppressStartCrossfadeSamples > 0)
            --slot.suppressStartCrossfadeSamples;
    }
}

void LoopEngine::beginRecording (int slotIndex, const looper::HostTiming& host, bool usePreBuffer) noexcept
{
    if (slotIndex < 0 || slotIndex >= activeSlotCount)
        return;

    if (! canRecordNow (host))
        return;

    auto& slot = slots[static_cast<size_t> (slotIndex)];
    slot.loop.clear();
    slot.recordPosition = 0;
    const auto preSamples = usePreBuffer ? juce::jmin (slot.preFill, preBufferSamplesFor (slot)) : 0;
    const auto start = (slot.preWrite - preSamples + maxPreBufferSamples) % maxPreBufferSamples;

    for (int i = 0; i < preSamples && i < maxLoopSamples; ++i)
    {
        const auto source = (start + i) % maxPreBufferSamples;
        slot.loop.setSample (0, i, slot.preBuffer.getSample (0, source));
        slot.loop.setSample (1, i, slot.preBuffer.getSample (1, source));
        ++slot.recordPosition;
    }

    const auto masterLength = masterLengthSamplesFor (slotIndex);
    const auto bars = slot.settings.lengthMode == looper::LengthMode::fixedBars
        ? slot.settings.fixedBars
        : autoStopBars (slot.settings.autoStop);
    slot.autoStopSamples = isAutoMaster (slotIndex) ? 0 : (masterLength > 0 ? masterLength : (bars > 0 ? samplesForBars (bars, host) : 0));
    slot.pendingRecordOnMasterStart = false;
    slot.pendingPlayOnMasterStart = false;
    slot.state = looper::SlotState::recording;
    slot.muted = false;
    slot.fadeGain = 1.0f;
    slot.fadeTarget = 1.0f;
    slot.suppressStartCrossfadeSamples = 0;
}

void LoopEngine::finishRecording (int slotIndex, const looper::HostTiming& host) noexcept
{
    if (slotIndex < 0 || slotIndex >= activeSlotCount)
        return;

    auto& slot = slots[static_cast<size_t> (slotIndex)];
    const auto minSamples = static_cast<int> (sampleRate * static_cast<double> (slot.settings.minRecordMs) / 1000.0);
    if (slot.recordPosition < juce::jmax (1, minSamples))
    {
        clearSlot (slot);
        return;
    }

    slot.lengthSamples = juce::jlimit (1, maxLoopSamples, slot.recordPosition);
    if (slot.autoStopSamples > 0)
        slot.lengthSamples = juce::jlimit (1, maxLoopSamples, slot.autoStopSamples);
    slot.playPosition = 0;
    slot.suppressStartCrossfadeSamples = crossfadeSamplesFor (slot);
    slot.pendingRecordOnMasterStart = false;
    slot.pendingPlayOnMasterStart = false;
    slot.pendingStopOnMasterStart = false;
    slot.lengthBeats = host.bpm > 0.0 ? (static_cast<double> (slot.lengthSamples) / sampleRate) * host.bpm / 60.0 : 0.0;
    if (shouldSlaveToMaster (slotIndex))
    {
        slot.pendingPlayOnMasterStart = true;
        slot.state = looper::SlotState::armed;
    }
    else
    {
        slot.state = looper::SlotState::playing;
    }
}

void LoopEngine::clearSlot (Slot& slot) noexcept
{
    slot.loop.clear();
    slot.preBuffer.clear();
    slot.state = looper::SlotState::empty;
    slot.muted = false;
    slot.lengthSamples = 0;
    slot.recordPosition = 0;
    slot.playPosition = 0;
    slot.preWrite = 0;
    slot.preFill = 0;
    slot.autoStopSamples = 0;
    slot.pendingRecordOnMasterStart = false;
    slot.pendingPlayOnMasterStart = false;
    slot.pendingStopOnMasterStart = false;
    slot.inputLevel = 0.0f;
    slot.fadeGain = 1.0f;
    slot.fadeTarget = 1.0f;
    slot.lengthBeats = 0.0;
    slot.suppressStartCrossfadeSamples = 0;
}

void LoopEngine::updatePreBuffer (Slot& slot, float left, float right) noexcept
{
    slot.preBuffer.setSample (0, slot.preWrite, left);
    slot.preBuffer.setSample (1, slot.preWrite, right);
    slot.preWrite = (slot.preWrite + 1) % maxPreBufferSamples;
    slot.preFill = juce::jmin (maxPreBufferSamples, slot.preFill + 1);
}

void LoopEngine::triggerMasterBoundary (const looper::HostTiming& host) noexcept
{
    if (! masterHasAudio())
        return;

    const auto& master = slots[static_cast<size_t> (masterSlot)];
    const auto masterPlaying = master.state == looper::SlotState::playing || master.state == looper::SlotState::overdubbing;
    if (! masterPlaying || master.playPosition != 0)
        return;

    for (int i = 0; i < activeSlotCount; ++i)
    {
        if (i == masterSlot || ! shouldSlaveToMaster (i))
            continue;

        auto& slot = slots[static_cast<size_t> (i)];
        if (slot.pendingStopOnMasterStart && slot.state == looper::SlotState::recording)
        {
            finishRecording (i, host);
            if (slot.lengthSamples > 0)
            {
                slot.pendingPlayOnMasterStart = false;
                slot.pendingStopOnMasterStart = false;
                slot.playPosition = 0;
                slot.state = looper::SlotState::playing;
                slot.fadeGain = 1.0f;
                slot.fadeTarget = slot.muted ? 0.0f : 1.0f;
            }
        }
        else if (slot.pendingRecordOnMasterStart)
            beginRecording (i, host, false);
        else if (slot.pendingPlayOnMasterStart && slot.lengthSamples > 0)
        {
            slot.pendingPlayOnMasterStart = false;
            slot.playPosition = 0;
            slot.state = looper::SlotState::playing;
            slot.fadeGain = 1.0f;
            slot.fadeTarget = slot.muted ? 0.0f : 1.0f;
        }
    }
}

int LoopEngine::phasePositionFor (const Slot& slot, const looper::HostTiming& host) const noexcept
{
    if (! host.hasPosition || slot.lengthSamples <= 0 || slot.lengthBeats <= 0.0)
        return 0;

    const auto phaseBeats = std::fmod (std::fmod (host.ppq, slot.lengthBeats) + slot.lengthBeats, slot.lengthBeats);
    return juce::jlimit (0, slot.lengthSamples - 1, static_cast<int> (std::floor (phaseBeats / slot.lengthBeats * static_cast<double> (slot.lengthSamples))));
}

int LoopEngine::phasePositionForMaster (const Slot& slot) const noexcept
{
    if (! masterHasAudio() || slot.lengthSamples <= 0)
        return 0;

    const auto& master = slots[static_cast<size_t> (masterSlot)];
    const auto phase = static_cast<double> (master.playPosition) / static_cast<double> (master.lengthSamples);
    return juce::jlimit (0, slot.lengthSamples - 1, static_cast<int> (std::floor (phase * static_cast<double> (slot.lengthSamples))));
}

int LoopEngine::masterLengthSamplesFor (int slotIndex) const noexcept
{
    if (slotIndex == masterSlot || ! masterHasAudio())
        return 0;

    const auto length = slots[static_cast<size_t> (masterSlot)].lengthSamples;
    switch (slots[static_cast<size_t> (slotIndex)].settings.lengthMode)
    {
        case looper::LengthMode::sameAsMaster: return length;
        case looper::LengthMode::autoMaster: return 0;
        case looper::LengthMode::halfMaster: return juce::jmax (1, length / 2);
        case looper::LengthMode::quarterMaster: return juce::jmax (1, length / 4);
        case looper::LengthMode::doubleMaster: return juce::jmin (maxLoopSamples, length * 2);
        case looper::LengthMode::quadrupleMaster: return juce::jmin (maxLoopSamples, length * 4);
        case looper::LengthMode::free:
        case looper::LengthMode::fixedBars:
        default: return 0;
    }
}

bool LoopEngine::masterHasAudio() const noexcept
{
    return masterSlot >= 0
        && masterSlot < activeSlotCount
        && slots[static_cast<size_t> (masterSlot)].lengthSamples > 0;
}

bool LoopEngine::isMasterDerived (looper::LengthMode mode) const noexcept
{
    return mode == looper::LengthMode::sameAsMaster
        || mode == looper::LengthMode::autoMaster
        || mode == looper::LengthMode::halfMaster
        || mode == looper::LengthMode::quarterMaster
        || mode == looper::LengthMode::doubleMaster
        || mode == looper::LengthMode::quadrupleMaster;
}

bool LoopEngine::isAutoMaster (int slotIndex) const noexcept
{
    return slotIndex >= 0
        && slotIndex < activeSlotCount
        && slots[static_cast<size_t> (slotIndex)].settings.lengthMode == looper::LengthMode::autoMaster;
}

bool LoopEngine::shouldSlaveToMaster (int slotIndex) const noexcept
{
    return slotIndex >= 0
        && slotIndex < activeSlotCount
        && slotIndex != masterSlot
        && isMasterDerived (slots[static_cast<size_t> (slotIndex)].settings.lengthMode);
}

bool LoopEngine::canRecordNow (const looper::HostTiming& host) const noexcept
{
    return syncMode == looper::SyncMode::standalone || host.isPlaying;
}

float LoopEngine::estimateMasterBpm() const noexcept
{
    if (! masterHasAudio())
        return 0.0f;

    const auto seconds = static_cast<double> (slots[static_cast<size_t> (masterSlot)].lengthSamples) / sampleRate;
    if (seconds <= 0.0)
        return 0.0f;

    auto best = 240.0 / seconds;
    while (best < 70.0)
        best *= 2.0;
    while (best > 180.0)
        best *= 0.5;
    return static_cast<float> (best);
}

int LoopEngine::countdownToHostBar (const looper::HostTiming& host) const noexcept
{
    if (! host.hasPosition || ! host.isPlaying)
        return 0;

    const auto barBeats = static_cast<double> (juce::jmax (1, host.numerator));
    const auto beatInBar = std::fmod (std::fmod (host.ppq, barBeats) + barBeats, barBeats);
    const auto remaining = barBeats - beatInBar;
    return juce::jlimit (1, juce::jmax (1, host.numerator), static_cast<int> (std::ceil (remaining)));
}

int LoopEngine::countdownToMasterStart() const noexcept
{
    if (! masterHasAudio())
        return 0;

    const auto& master = slots[static_cast<size_t> (masterSlot)];
    if (master.lengthSamples <= 0 || sampleRate <= 0.0)
        return 0;

    const auto samplesRemaining = master.playPosition == 0 ? 0 : master.lengthSamples - master.playPosition;
    return static_cast<int> (std::ceil (static_cast<double> (samplesRemaining) / sampleRate));
}

bool LoopEngine::shouldTriggerPending (const PendingCommand& pending,
                                       const looper::HostTiming& host,
                                       int sampleOffset,
                                       int numSamples) const noexcept
{
    if (! pending.active)
        return false;

    if (! host.hasPosition || ! host.isPlaying)
        return sampleOffset == 0;

    const auto ppqPerSample = (host.bpm / 60.0) / sampleRate;
    const auto samplePpq = host.ppq + static_cast<double> (sampleOffset) * ppqPerSample;
    const auto nextPpq = host.ppq + static_cast<double> (numSamples) * ppqPerSample;
    return samplePpq >= pending.targetPpq || (pending.targetPpq >= host.ppq && pending.targetPpq < nextPpq && sampleOffset == juce::jlimit (0, numSamples - 1, static_cast<int> ((pending.targetPpq - host.ppq) / ppqPerSample)));
}

double LoopEngine::targetPpqFor (looper::QuantizeMode mode, int slot, const looper::HostTiming& host) const noexcept
{
    switch (mode)
    {
        case looper::QuantizeMode::immediate:
            return host.ppq;

        case looper::QuantizeMode::nextBeat:
            return std::floor (host.ppq) + 1.0;

        case looper::QuantizeMode::nextLoop:
        {
            if (slot >= 0 && slot < activeSlotCount && slots[static_cast<size_t> (slot)].lengthBeats > 0.0)
            {
                const auto length = slots[static_cast<size_t> (slot)].lengthBeats;
                return std::floor (host.ppq / length + 1.0) * length;
            }
            [[fallthrough]];
        }

        case looper::QuantizeMode::nextBar:
        {
            const auto barBeats = static_cast<double> (juce::jmax (1, host.numerator));
            return std::floor (host.ppq / barBeats + 1.0) * barBeats;
        }

        default:
            return host.ppq;
    }
}

int LoopEngine::samplesForBars (int bars, const looper::HostTiming& host) const noexcept
{
    const auto beats = static_cast<double> (juce::jmax (1, bars)) * static_cast<double> (juce::jmax (1, host.numerator));
    return juce::jlimit (1, maxLoopSamples, static_cast<int> (std::round (beats * 60.0 * sampleRate / juce::jlimit (20.0, 320.0, host.bpm))));
}

int LoopEngine::preBufferSamplesFor (const Slot& slot) const noexcept
{
    return juce::jlimit (0, maxPreBufferSamples, static_cast<int> (sampleRate * static_cast<double> (slot.settings.preBufferMs) / 1000.0));
}

int LoopEngine::crossfadeSamplesFor (const Slot& slot) const noexcept
{
    if (slot.lengthSamples < 32)
        return 0;

    const auto fadeSamples = static_cast<int> (std::round (sampleRate * 0.005));
    return juce::jlimit (0, slot.lengthSamples / 2, fadeSamples);
}

float LoopEngine::crossfadedSample (const Slot& slot, int channel, int position, bool suppressStartCrossfade) const noexcept
{
    const auto fadeSamples = crossfadeSamplesFor (slot);
    const auto sample = slot.loop.getSample (channel, position);
    if (fadeSamples <= 1 || slot.lengthSamples <= fadeSamples * 2)
        return sample;

    if (position < fadeSamples && ! suppressStartCrossfade)
    {
        const auto tailPosition = slot.lengthSamples - fadeSamples + position;
        const auto blend = static_cast<float> (position) / static_cast<float> (fadeSamples);
        return slot.loop.getSample (channel, tailPosition) * (1.0f - blend) + sample * blend;
    }

    if (position >= slot.lengthSamples - fadeSamples)
    {
        const auto headPosition = position - (slot.lengthSamples - fadeSamples);
        const auto blend = static_cast<float> (headPosition) / static_cast<float> (fadeSamples);
        return sample * (1.0f - blend) + slot.loop.getSample (channel, headPosition) * blend;
    }

    return sample;
}
