#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Prompter.h"
#include "SetWatcher.h"
#include "Song.h"
#include "Transport.h"

#include <atomic>

#include <memory>

namespace boothcue
{

class DisplayWindow;

// BoothCue: shows the singer the lyrics and the song position while tracking.
//
// Shape: an audio effect whose output is its input, untouched, so it can sit on
// any track. Its only job on the audio thread is reading the host's playhead.
//
// Threading:
//   * Audio thread (processBlock / processBlockBypassed) — copies the host's
//     position into a TransportState, counts loop passes, and publishes it via
//     SharedTransport. No allocation, no locks, no parsing, no UI.
//   * Message thread — owns the Song, the display settings, the link status
//     and the display window; the editor and the display read getTransport()
//     on their timers. This object's timer collects SetWatcher reports.
//   * SetWatcher thread — stats and parses the linked .als, never anything else.
//   * get/setStateInformation may come from any thread: they only touch a
//     ValueTree snapshot under stateLock (see updateStateSnapshot).
//
// There are no automatable parameters: everything BoothCue keeps (linked set,
// lyrics, timings, display settings) is plugin state, saved with the Live set.

class BoothCueAudioProcessor final : public juce::AudioProcessor,
                                     private juce::Timer
{
public:
    BoothCueAudioProcessor();
    ~BoothCueAudioProcessor() override;

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

    // ---- message thread ------------------------------------------------------
    const Song& getSong() const noexcept                                { return song; }
    void setSong (Song);
    // New lyric blocks (with their timings) for the current structure.
    void setLyrics (std::vector<LyricBlock>);

    const DisplaySettings& getDisplaySettings() const noexcept          { return displaySettings; }
    void setDisplaySettings (const DisplaySettings&);

    // ---- the linked Live set (message thread) ----------------------------------
    struct LinkStatus
    {
        enum class State { none, reading, ok, missing, error };
        State state = State::none;
        juce::File file;
        juce::String error;         // State::error
        juce::Time readAt;          // last successful read
        int sections = 0;           // in the structure shown
        juce::String trackName;     // the track the sections came from
        bool usedFallback = false;  // no "parts" track: the 2nd track was used
    };

    void linkSet (const juce::File&);
    void unlinkSet();
    void reloadSet();
    const LinkStatus& getLinkStatus() const noexcept                    { return linkStatus; }

    // ---- tap-along (message thread) --------------------------------------------
    struct TapSettings
    {
        double snapBeats = 0.0;   // 0 = off, 0.25 = 16ths, 0.5 = 8ths
        double latencyMs = 0.0;   // subtracted from every tap while playing
    };
    const TapSettings& getTapSettings() const noexcept                  { return tapSettings; }
    void setTapSettings (const TapSettings&);

    // The line the next tap stamps (the editor's selection).
    TapCursor getTapCursor() const noexcept                             { return tapCursor; }
    void setTapCursor (int block, int line);

    // Stamps the cursor's line (see tapAt) at the playhead, as of the moment
    // of the call. Returns false, with the reason in getLastTapMessage(), when
    // nothing was stamped.
    bool tap();
    juce::String getLastTapMessage() const                              { return lastTapMessage; }
    bool wasLastTapOk() const noexcept                                  { return lastTapOk; }
    double getLastTapTimeMs() const noexcept                            { return lastTapMs; }

    void nudgeLine (int block, int line, double beats);
    void clearLine (int block, int line);
    void clearAllTimings();

    // One step back: the last tap, nudge or clear.
    bool canUndo() const noexcept                                       { return hasUndo; }
    juce::String getUndoName() const                                    { return undoName; }
    void undo();

    // Goes up whenever the song, the link status or the display settings
    // change, so views can tell when to repaint.
    juce::uint32 getContentVersion() const noexcept                     { return contentVersion; }

    // The singer's display window (created on first use, then shown / hidden).
    void showDisplay();
    void hideDisplay();
    bool isDisplayVisible() const;

    // The name of the track BoothCue sits on, when the host tells (Live does,
    // for VST3). Any thread.
    juce::String getTrackName() const;
    void updateTrackProperties (const TrackProperties&) override;

    static BusesProperties createBusesProperties();

private:
    // Audio thread: playhead -> TransportState -> SharedTransport.
    void captureTransport() noexcept;

    // Message thread.
    void timerCallback() override;
    void applyReport (const SetWatcher::Report&);
    void applyState (const juce::ValueTree&);
    void setSections (std::vector<Section>);
    void updateStateSnapshot();   // song / link -> stateSnapshot
    void timingsChanged();        // after a tap / nudge / clear / undo
    void pushUndo (const juce::String& name);
    SharedTransport transport;
    LoopCounter loopCounter;

    Song song;
    DisplaySettings displaySettings;
    juce::uint32 contentVersion = 1;
    std::unique_ptr<DisplayWindow> displayWindow;

    SetWatcher watcher;
    LinkStatus linkStatus;

    TapSettings tapSettings;
    TapCursor tapCursor;
    juce::String lastTapMessage;
    bool lastTapOk = false;
    double lastTapMs = 0.0;
    std::vector<LyricBlock> undoBlocks;
    bool hasUndo = false;
    juce::String undoName;

    // What getStateInformation writes, kept current by the message thread.
    // setStateInformation from another thread parks the tree in pendingState
    // for the timer to apply.
    juce::CriticalSection stateLock;
    juce::ValueTree stateSnapshot;
    juce::ValueTree pendingState;
    std::atomic<bool> hasPendingState { false };

    mutable juce::SpinLock trackNameLock;
    juce::String trackName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoothCueAudioProcessor)
};

}  // namespace boothcue
