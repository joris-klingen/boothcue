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
}

BoothCueAudioProcessor::~BoothCueAudioProcessor()
{
    // The window is a GUI object: hosts delete plugins on the message thread.
    JUCE_ASSERT_MESSAGE_THREAD
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
// Only the envelope for now; the linked set, lyrics, timings and display
// settings are added in TODO step 6.

void BoothCueAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state (kStateType);
    state.setProperty (kVersion, kStateVersion, nullptr);
    juce::MemoryOutputStream out (destData, false);
    state.writeToStream (out);
}

void BoothCueAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto state = juce::ValueTree::readFromData (data, static_cast<size_t> (sizeInBytes));
    if (! state.hasType (kStateType))
        return;   // not ours, or damaged: keep the defaults
}

}  // namespace boothcue

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new boothcue::BoothCueAudioProcessor();
}
