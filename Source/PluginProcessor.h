#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Transport.h"

namespace boothcue
{

// BoothCue: shows the singer the lyrics and the song position while tracking.
//
// Shape: an audio effect whose output is its input, untouched, so it can sit on
// any track. Its only job on the audio thread is reading the host's playhead.
//
// Threading:
//   * Audio thread (processBlock / processBlockBypassed) — copies the host's
//     position into a TransportState, counts loop passes, and publishes it via
//     SharedTransport. No allocation, no locks, no parsing, no UI.
//   * Message thread — the editor and (later) the display window read
//     getTransport() on their timers. Song structure, lyrics and the .als
//     parsing live there or on a background thread, never on the audio thread.
//
// There are no automatable parameters: everything BoothCue keeps (linked set,
// lyrics, timings, display settings) is plugin state, saved with the Live set.

class BoothCueAudioProcessor final : public juce::AudioProcessor
{
public:
    BoothCueAudioProcessor();
    ~BoothCueAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlock (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    // Bypassed in the host: still follow the transport, so the singer's display
    // keeps running.
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<double>&, juce::MidiBuffer&) override;
    bool supportsDoublePrecisionProcessing() const override             { return true; }

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                                     { return true; }

    const juce::String getName() const override                         { return JucePlugin_Name; }
    bool acceptsMidi() const override                                   { return false; }
    bool producesMidi() const override                                  { return false; }
    bool isMidiEffect() const override                                  { return false; }
    double getTailLengthSeconds() const override                        { return 0.0; }

    int getNumPrograms() override                                       { return 1; }
    int getCurrentProgram() override                                    { return 0; }
    void setCurrentProgram (int) override                               {}
    const juce::String getProgramName (int) override                    { return {}; }
    void changeProgramName (int, const juce::String&) override          {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // The host position as of the last audio block. Any thread.
    TransportState getTransport() const noexcept                        { return transport.read(); }

    static BusesProperties createBusesProperties();

private:
    // Audio thread: playhead -> TransportState -> SharedTransport.
    void captureTransport() noexcept;

    SharedTransport transport;
    LoopCounter loopCounter;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoothCueAudioProcessor)
};

}  // namespace boothcue
