#include "PluginProcessor.h"

#include "DisplayWindow.h"
#include "PluginEditor.h"

namespace boothcue
{

namespace
{
    // Root of the saved state. Bump kStateVersion when the layout changes in a
    // way older builds can't read.
    const juce::Identifier kStateType { "BoothCue" };
    const juce::Identifier kVersion   { "version" };
    constexpr int kStateVersion = 1;

    // <Set path="/…/Song.als" track="Parts" fallback="0">
    //   <Section name="Intro" start="0" end="16"/> …    the last structure read,
    // </Set>                                           so the display still works
    //                                                  where the file is missing
    const juce::Identifier kSet      { "Set" };
    const juce::Identifier kPath     { "path" };
    const juce::Identifier kTrack    { "track" };
    const juce::Identifier kFallback { "fallback" };
    const juce::Identifier kSection  { "Section" };
    const juce::Identifier kName     { "name" };
    const juce::Identifier kStart    { "start" };
    const juce::Identifier kEnd      { "end" };

    constexpr int kTimerHz = 10;
}

BoothCueAudioProcessor::BusesProperties BoothCueAudioProcessor::createBusesProperties()
{
    return BusesProperties()
        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

BoothCueAudioProcessor::BoothCueAudioProcessor()
    : AudioProcessor (createBusesProperties())
{
    updateStateSnapshot();
    startTimerHz (kTimerHz);
}

BoothCueAudioProcessor::~BoothCueAudioProcessor()
{
    // The window is a GUI object: hosts delete plugins on the message thread.
    JUCE_ASSERT_MESSAGE_THREAD
    stopTimer();
    displayWindow.reset();
}

void BoothCueAudioProcessor::prepareToPlay (double, int)
{
}

bool BoothCueAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == mainOut;
}

// ---- audio --------------------------------------------------------------------------
// The input buses ARE the output buses here (same channel count), so leaving the
// buffer alone is the pass-through.

void BoothCueAudioProcessor::processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    captureTransport();
}

void BoothCueAudioProcessor::processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&)
{
    captureTransport();
}

void BoothCueAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&)
{
    captureTransport();
}

void BoothCueAudioProcessor::processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&)
{
    captureTransport();
}

void BoothCueAudioProcessor::captureTransport() noexcept
{
    TransportState s;

    if (auto* host = getPlayHead())
    {
        if (const auto pos = host->getPosition())
        {
            if (const auto ppq = pos->getPpqPosition())
            {
                s.valid = true;
                s.ppq   = *ppq;
            }
            if (const auto bpm = pos->getBpm())
                s.bpm = *bpm;
            if (const auto sig = pos->getTimeSignature())
            {
                if (sig->numerator > 0 && sig->denominator > 0)
                {
                    s.sigNum = sig->numerator;
                    s.sigDen = sig->denominator;
                }
            }
            if (const auto barStart = pos->getPpqPositionOfLastBarStart())
            {
                s.hasLastBarStart = true;
                s.lastBarStart    = *barStart;
            }
            if (const auto loop = pos->getLoopPoints())
            {
                s.loopStart = loop->ppqStart;
                s.loopEnd   = loop->ppqEnd;
            }
            s.playing   = pos->getIsPlaying();
            s.recording = pos->getIsRecording();
            s.looping   = pos->getIsLooping();
        }
    }

    s.loopPass = loopCounter.update (s.valid && s.playing, s.looping, s.loopStart, s.loopEnd, s.ppq);
    transport.write (s);
}

// ---- the linked set (message thread) ------------------------------------------------------

void BoothCueAudioProcessor::linkSet (const juce::File& file)
{
    JUCE_ASSERT_MESSAGE_THREAD
    linkStatus = LinkStatus();
    linkStatus.state = LinkStatus::State::reading;
    linkStatus.file = file;
    linkStatus.sections = static_cast<int> (song.sections().size());
    watcher.watch (file);
    ++contentVersion;
    updateStateSnapshot();
}

void BoothCueAudioProcessor::unlinkSet()
{
    JUCE_ASSERT_MESSAGE_THREAD
    watcher.watch ({});
    linkStatus = LinkStatus();
    setSections ({});
    updateStateSnapshot();
}

void BoothCueAudioProcessor::reloadSet()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (linkStatus.file != juce::File())
        watcher.rescan();
}

void BoothCueAudioProcessor::timerCallback()
{
    if (hasPendingState.exchange (false))
    {
        juce::ValueTree state;
        {
            const juce::ScopedLock sl (stateLock);
            state = pendingState;
            pendingState = {};
        }
        if (state.isValid())
            applyState (state);
    }

    SetWatcher::Report report;
    if (watcher.takeReport (report) && report.file == linkStatus.file)
        applyReport (report);
}

void BoothCueAudioProcessor::applyReport (const SetWatcher::Report& report)
{
    if (report.missing)
    {
        linkStatus.state = LinkStatus::State::missing;
    }
    else if (! report.result.ok)
    {
        linkStatus.state = LinkStatus::State::error;
        linkStatus.error = juce::String::fromUTF8 (report.result.error.c_str());
    }
    else
    {
        std::vector<Section> sections;
        for (const auto& s : report.result.sections)
            sections.push_back ({ s.name, s.start, s.end });

        linkStatus.state        = LinkStatus::State::ok;
        linkStatus.error        = {};
        linkStatus.readAt       = juce::Time::getCurrentTime();
        linkStatus.trackName    = juce::String::fromUTF8 (report.result.trackName.c_str());
        linkStatus.usedFallback = report.result.usedFallback;
        setSections (std::move (sections));
    }
    // A failed read keeps the structure from before: say how much is shown.
    linkStatus.sections = static_cast<int> (song.sections().size());
    ++contentVersion;
    updateStateSnapshot();
}

void BoothCueAudioProcessor::setSections (std::vector<Section> sections)
{
    song.setSections (std::move (sections));
    ++contentVersion;
}

// ---- song, settings, display (message thread) ------------------------------------------

void BoothCueAudioProcessor::setSong (Song newSong)
{
    JUCE_ASSERT_MESSAGE_THREAD
    song = std::move (newSong);
    ++contentVersion;
}

void BoothCueAudioProcessor::setDisplaySettings (const DisplaySettings& s)
{
    JUCE_ASSERT_MESSAGE_THREAD
    displaySettings = s;
    displaySettings.fontScale = juce::jlimit (0.5f, 2.0f, s.fontScale);
    ++contentVersion;
}

void BoothCueAudioProcessor::showDisplay()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (displayWindow == nullptr)
        displayWindow = std::make_unique<DisplayWindow> (*this);
    displayWindow->setTrackName (getTrackName());
    displayWindow->setVisible (true);
    displayWindow->toFront (true);
}

void BoothCueAudioProcessor::hideDisplay()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (displayWindow != nullptr)
        displayWindow->closeButtonPressed();
}

bool BoothCueAudioProcessor::isDisplayVisible() const
{
    return displayWindow != nullptr && displayWindow->isVisible();
}

juce::String BoothCueAudioProcessor::getTrackName() const
{
    const juce::SpinLock::ScopedLockType lock (trackNameLock);
    return trackName;
}

void BoothCueAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    // Hosts may call this on any thread; the display window picks the name up
    // on its own timer.
    const juce::SpinLock::ScopedLockType lock (trackNameLock);
    trackName = properties.name.value_or (juce::String());
}

// ---- editor -------------------------------------------------------------------------

juce::AudioProcessorEditor* BoothCueAudioProcessor::createEditor()
{
    return new BoothCueAudioProcessorEditor (*this);
}

// ---- state --------------------------------------------------------------------------
// The linked set and the last structure read from it. Lyrics, timings and
// display settings are added in TODO step 6.

void BoothCueAudioProcessor::updateStateSnapshot()
{
    juce::ValueTree state (kStateType);
    state.setProperty (kVersion, kStateVersion, nullptr);

    if (linkStatus.file != juce::File() || ! song.sections().empty())
    {
        juce::ValueTree set (kSet);
        set.setProperty (kPath, linkStatus.file.getFullPathName(), nullptr);
        set.setProperty (kTrack, linkStatus.trackName, nullptr);
        set.setProperty (kFallback, linkStatus.usedFallback, nullptr);
        for (const auto& sec : song.sections())
        {
            juce::ValueTree t (kSection);
            t.setProperty (kName, juce::String::fromUTF8 (sec.name.c_str()), nullptr);
            t.setProperty (kStart, sec.start, nullptr);
            t.setProperty (kEnd, sec.end, nullptr);
            set.appendChild (t, nullptr);
        }
        state.appendChild (set, nullptr);
    }

    const juce::ScopedLock sl (stateLock);
    stateSnapshot = state;
}

void BoothCueAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state;
    {
        const juce::ScopedLock sl (stateLock);
        // A state set but not applied yet is what the host should get back.
        state = hasPendingState.load() && pendingState.isValid() ? pendingState : stateSnapshot;
        state = state.createCopy();
    }
    juce::MemoryOutputStream out (destData, false);
    state.writeToStream (out);
}

void BoothCueAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto state = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes));
    if (! state.hasType (kStateType))
        return;   // not ours, or damaged: keep what we have

    if (juce::MessageManager::existsAndIsCurrentThread())
    {
        applyState (state);
        return;
    }
    const juce::ScopedLock sl (stateLock);
    pendingState = state;
    hasPendingState = true;
}

void BoothCueAudioProcessor::applyState (const juce::ValueTree& state)
{
    JUCE_ASSERT_MESSAGE_THREAD

    const auto set = state.getChildWithName (kSet);
    std::vector<Section> sections;
    for (const auto& t : set)
        if (t.hasType (kSection))
            sections.push_back ({ t[kName].toString().toStdString(),
                                  static_cast<double> (t[kStart]), static_cast<double> (t[kEnd]) });

    // Show the saved structure at once; the watcher re-reads the file (if it
    // is there) and replaces it.
    setSections (std::move (sections));

    const juce::String path = set[kPath].toString();
    linkStatus = LinkStatus();
    linkStatus.sections = static_cast<int> (song.sections().size());
    linkStatus.trackName = set[kTrack].toString();
    linkStatus.usedFallback = static_cast<bool> (set[kFallback]);
    if (juce::File::isAbsolutePath (path))
    {
        linkStatus.state = LinkStatus::State::reading;
        linkStatus.file = juce::File (path);
    }
    watcher.watch (linkStatus.file);
    ++contentVersion;
    updateStateSnapshot();
}

}  // namespace boothcue

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new boothcue::BoothCueAudioProcessor();
}
