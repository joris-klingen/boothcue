#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ChordChart.h"
#include "Prompter.h"
#include "SetWatcher.h"
#include "Song.h"
#include "Transport.h"

#include <atomic>

#include <memory>

namespace boothcue
{

class DisplayWindow;
class LiveFeed;

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
        juce::String chordSummary;  // where the chords come from, the key; empty: no MIDI
    };

    void linkSet (const juce::File&);
    void unlinkSet();
    void reloadSet();
    const LinkStatus& getLinkStatus() const noexcept                    { return linkStatus; }

    // ---- the linked song folder (message thread) ---------------------------------
    // A song's folder in the band's songbook (bovenburen_manager). Its
    // lyrics.txt and chords.txt are read whenever they change and shown as
    // they are; BoothCue never writes those two, the songbook is their master.
    // The starts tapped here are written next to them as timings.txt (the
    // timed lyrics, as Save writes them) and laid over the text when it is read.
    void linkSongFolder (const juce::File&);
    void unlinkSongFolder();
    juce::File getSongFolder() const                                    { return songFolder; }
    juce::String getSongFolderStatus() const                            { return songFolderStatus; }
    // Looks at the folder's files and writes the timings when they changed.
    // The timer calls it once a second.
    void pollSongFolder();

    // ---- the chords database (message thread) -----------------------------------
    // The chords shown are BoothCue's own: detected from the set's MIDI after
    // every save, until they are edited or imported as a chart. From then on
    // they are kept as they are (detection still runs; useDetectedChords()
    // switches back). BoothCue never writes to the set.
    bool areChordsEdited() const noexcept                               { return chordsEdited; }
    bool hasDetectedChords() const noexcept                             { return ! detectedChords.empty(); }
    juce::String getChordChart() const;                // the chords as a chart (export / editing)
    ChartImport setChordChart (const juce::String&);   // import / apply a chart: the chords become "edited"
    void setImportedChords (std::vector<ChordSpan>, Key); // e.g. from a MIDI file: the chords become "edited"
    void useDetectedChords();
    bool canUndoChords() const noexcept                                 { return hasChordUndo; }
    void undoChords();

    // The lyrics with their timings, for export ("@bar.beat.16th" per tapped line).
    juce::String getTimedLyrics() const;

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
    // Whether it is open, where, and full screen or not is saved with the set.
    void showDisplay();
    void hideDisplay();
    bool isDisplayVisible() const;

    // The plugin window's size, saved with the set (0 = the default).
    juce::Point<int> getEditorSize() const noexcept                     { return editorSize; }
    void setEditorSize (juce::Point<int>);

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

    // The live view for phones (LiveFeed.h): the scene, sent from the timer.
    std::unique_ptr<LiveFeed> liveFeed;
    juce::String lastLiveJson;
    double lastLiveMs = 0.0;
    void postLiveScene();
    juce::String displayPlacement;   // ResizableWindow state string, kept while it is closed too
    juce::Rectangle<int> windowedBounds;
    juce::Point<int> editorSize;

    SetWatcher watcher;
    LinkStatus linkStatus;

    // One text file of the song folder. It is small, so it is simply read at
    // every poll (a file's time stamp can be too coarse to show a quick second
    // save); a new text is taken once two polls in a row read the same, so a
    // half-written file isn't.
    struct WatchedText
    {
        juce::String text;                 // as taken
        juce::String seen;                 // as read at the last poll
        bool poll (const juce::File&);     // true: `text` is new
        void read (const juce::File&);
    };
    void applyFolderLyrics();
    void applyFolderChords();
    void updateSongFolderStatus();
    juce::File songFolder;
    WatchedText folderLyrics, folderChords;
    juce::String songFolderStatus, timingsWritten;
    juce::Time timingsWrittenAt;
    bool timingsDirty = false;
    int folderTick = 0;

    std::vector<ChordSpan> detectedChords;
    Key detectedKey;
    bool chordsEdited = false;
    std::vector<ChordSpan> chordUndo;
    bool chordUndoEdited = false, hasChordUndo = false;

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
