#include "PluginProcessor.h"

#include "DisplayWindow.h"
#include "LiveFeed.h"
#include "LyricImport.h"
#include "PluginEditor.h"

namespace boothcue
{

namespace
{
    // Root of the saved state. Bump kStateVersion when the layout changes in a
    // way older builds can't read.
    const juce::Identifier kStateType { "BoothCue" };
    const juce::Identifier kVersion   { "version" };
    const juce::Identifier kSongFolder { "songFolder" };
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
    const juce::Identifier kColour   { "colour" };   // Live clip colour (palette index)

    // <Lyrics>
    //   <Block header="Chorus"><Line text="Hold on" offset="2.5"/> … </Block> …
    // </Lyrics>                    offset: beats into the section, -1 = not tapped
    const juce::Identifier kLyrics   { "Lyrics" };
    const juce::Identifier kBlock    { "Block" };
    const juce::Identifier kHeader   { "header" };
    const juce::Identifier kLine     { "Line" };
    const juce::Identifier kText     { "text" };
    const juce::Identifier kOffset   { "offset" };

    // <Display part lyrics chords bass scale count countBars entryGap open placement/>
    // <Tap snap latency/>   <Editor w h/>
    const juce::Identifier kDisplay   { "Display" };
    const juce::Identifier kShowPart  { "part" };
    const juce::Identifier kShowLyric { "lyrics" };
    const juce::Identifier kShowChord { "chords" };
    const juce::Identifier kShowBass  { "bass" };
    const juce::Identifier kScale     { "scale" };
    const juce::Identifier kCount     { "count" };
    const juce::Identifier kCountBars { "countBars" };
    const juce::Identifier kEntryGap  { "entryGap" };
    const juce::Identifier kOpen      { "open" };
    const juce::Identifier kPlacement { "placement" };
    const juce::Identifier kTap       { "Tap" };
    const juce::Identifier kSnap      { "snap" };
    const juce::Identifier kLatency   { "latency" };
    const juce::Identifier kEditor    { "Editor" };
    // <Chords tonic minor summary><C s e r q b c/>...</Chords>: the last chords
    // detected, so they show where the set's file isn't
    const juce::Identifier kChords    { "Chords" };
    const juce::Identifier kChord     { "C" };
    const juce::Identifier kTonic     { "tonic" };
    const juce::Identifier kMinor     { "minor" };
    const juce::Identifier kSummary   { "summary" };
    const juce::Identifier kEdited    { "edited" };   // the chords were edited / imported: detection doesn't replace them
    const juce::Identifier kWidth     { "w" };
    const juce::Identifier kHeight    { "h" };

    juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

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
    song.setTimeSig (TimeSig());   // until the host says otherwise
    updateStateSnapshot();
    liveFeed = std::make_unique<LiveFeed>();
    startTimerHz (kTimerHz);
}

BoothCueAudioProcessor::~BoothCueAudioProcessor()
{
    // The window is a GUI object: hosts delete plugins on the message thread.
    JUCE_ASSERT_MESSAGE_THREAD
    stopTimer();
    liveFeed.reset();
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
    s.capturedMs = juce::Time::getMillisecondCounterHiRes();
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
    song.setChords ({}, {});
    detectedChords.clear();
    chordsEdited = false;
    hasChordUndo = false;
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
    if (hasPendingState.load())
    {
        juce::ValueTree state;
        {
            const juce::ScopedLock sl (stateLock);
            state = pendingState;
        }
        if (state.isValid())
            applyState (state);

        // It stays parked until it is applied and the snapshot is it: a
        // getStateInformation in between still gets the state that was set,
        // not the one from before. A newer one that came in meanwhile is left
        // for the next tick.
        const juce::ScopedLock sl (stateLock);
        if (pendingState == state)
        {
            pendingState = {};
            hasPendingState = false;
        }
    }

    SetWatcher::Report report;
    if (watcher.takeReport (report) && report.file == linkStatus.file)
        applyReport (report);

    if (++folderTick >= 10)   // once a second
    {
        folderTick = 0;
        pollSongFolder();
    }

    // Untimed lines spread on the host's bar grid; chords are detected on it.
    if (const auto t = transport.read(); t.valid)
    {
        const double bar = TimeSig { t.sigNum, t.sigDen }.barLength();
        if (std::abs (bar - song.barLength()) > 1.0e-9 || song.timeSig().num != t.sigNum || song.timeSig().den != t.sigDen)
        {
            song.setTimeSig (TimeSig { t.sigNum, t.sigDen });
            watcher.setBarBeats (bar);
            if (linkStatus.file != juce::File())
                watcher.rescan();
            ++contentVersion;
        }
    }

    postLiveScene();
}

// The phones get everything the display can show (parts, lyrics, full chord
// names), whatever the singer display's own toggles are. Sent when it changes,
// and once a second anyway so the manager knows BoothCue is still there.
void BoothCueAudioProcessor::postLiveScene()
{
    if (liveFeed == nullptr)
        return;
    auto settings = displaySettings;
    settings.showSongPart = settings.showLyrics = settings.showChords = true;
    settings.showBassNote = false;
    const auto t = transport.read();
    const auto json = LiveFeed::sceneJson (buildScene (song, t, settings), song, t,
                                           songFolder.getFileName(), getTrackName());
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (json != lastLiveJson || now - lastLiveMs > 1000.0)
    {
        liveFeed->post (json);
        lastLiveJson = json;
        lastLiveMs = now;
    }
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
            sections.push_back ({ s.name, s.start, s.end, s.color });

        linkStatus.state        = LinkStatus::State::ok;
        linkStatus.error        = {};
        linkStatus.readAt       = juce::Time::getCurrentTime();
        linkStatus.trackName    = juce::String::fromUTF8 (report.result.trackName.c_str());
        linkStatus.usedFallback = report.result.usedFallback;
        setSections (std::move (sections));

        // Chords from the set's MIDI: shown unless the chords were edited.
        const auto& c = report.chords;
        detectedChords = c.result.spans;
        detectedKey = c.result.key;
        if (! chordsEdited)
            song.setChords (c.result.spans, c.result.key);
        applyFolderChords();   // the songbook's chart, on the parts just read
        if (! c.anyNotes || c.result.spans.empty())
        {
            linkStatus.chordSummary = {};
        }
        else
        {
            // A real set can have a dozen chord tracks: name a few, count the rest,
            // so the status fits the editor.
            auto names = [] (const std::vector<std::string>& v)
            {
                juce::StringArray a;
                for (const auto& n : v)
                    a.addIfNotAlreadyThere (str (n));
                if (a.size() <= 3)
                    return a.joinIntoString (", ");
                const auto more = a.size() - 2;
                a.removeRange (2, a.size());
                return a.joinIntoString (", ") + " and " + juce::String (more) + " more";
            };
            auto text = "Chords from " + (c.chordTracks.empty() ? juce::String ("the bass") : names (c.chordTracks));
            if (! c.bassTracks.empty())
                text << ", bass from " << names (c.bassTracks);
            text << " - key " << str (keyName (c.result.key)) << ", " << juce::String (c.result.spans.size()) << " chords";
            linkStatus.chordSummary = text;
        }
    }
    // A failed read keeps the structure from before: say how much is shown.
    linkStatus.sections = static_cast<int> (song.sections().size());
    ++contentVersion;
    updateStateSnapshot();
}

// ---- the linked song folder (message thread) ----------------------------------------------

namespace
{
const char* const kLyricsFile  = "lyrics.txt";
const char* const kChordsFile  = "chords.txt";
const char* const kTimingsFile = "timings.txt";
}

namespace
{
juce::String readSmallText (const juce::File& file)
{
    // A songbook text is small; anything huge is not one.
    return file.existsAsFile() && file.getSize() < 1024 * 1024 ? file.loadFileAsString() : juce::String();
}
}

void BoothCueAudioProcessor::WatchedText::read (const juce::File& file)
{
    text = seen = readSmallText (file);
}

bool BoothCueAudioProcessor::WatchedText::poll (const juce::File& file)
{
    const auto now = readSmallText (file);
    const bool held = now == seen;
    seen = now;
    if (! held || now == text)
        return false;
    text = now;
    return true;
}

void BoothCueAudioProcessor::linkSongFolder (const juce::File& folder)
{
    JUCE_ASSERT_MESSAGE_THREAD
    songFolder = folder;
    folderLyrics = WatchedText();
    folderChords = WatchedText();
    timingsWritten = {};
    timingsWrittenAt = {};
    folderLyrics.read (folder.getChildFile (kLyricsFile));
    folderChords.read (folder.getChildFile (kChordsFile));
    applyFolderLyrics();
    applyFolderChords();
    // Starts tapped before the folder was linked go into it too.
    timingsDirty = true;
    updateSongFolderStatus();
    ++contentVersion;
    updateStateSnapshot();
}

void BoothCueAudioProcessor::unlinkSongFolder()
{
    JUCE_ASSERT_MESSAGE_THREAD
    // The lyrics and chords stay as they are: they are saved with the set.
    songFolder = juce::File();
    folderLyrics = WatchedText();
    folderChords = WatchedText();
    timingsDirty = false;
    songFolderStatus = {};
    ++contentVersion;
    updateStateSnapshot();
}

void BoothCueAudioProcessor::pollSongFolder()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (songFolder == juce::File())
        return;

    bool changed = false;
    if (folderLyrics.poll (songFolder.getChildFile (kLyricsFile)))
    {
        applyFolderLyrics();
        changed = true;
    }
    if (folderChords.poll (songFolder.getChildFile (kChordsFile)))
    {
        applyFolderChords();
        changed = true;
    }

    if (timingsDirty)
    {
        timingsDirty = false;
        bool anyStart = false;
        for (const auto& b : song.blocks())
            for (const auto& l : b.lines)
                anyStart = anyStart || l.timed() || l.hasAt;
        const auto text = getTimedLyrics();
        const auto file = songFolder.getChildFile (kTimingsFile);
        // Nothing tapped and no file yet: don't make one.
        if (text != timingsWritten && songFolder.isDirectory() && (anyStart || file.existsAsFile()))
        {
            if (file.replaceWithText (text, false, false, "\n"))
            {
                timingsWritten = text;
                timingsWrittenAt = juce::Time::getCurrentTime();
                changed = true;
            }
        }
    }

    const auto before = songFolderStatus;
    updateSongFolderStatus();
    if (changed || before != songFolderStatus)
    {
        ++contentVersion;
        updateStateSnapshot();
    }
}

void BoothCueAudioProcessor::applyFolderLyrics()
{
    if (folderLyrics.text.trim().isEmpty())
        return;   // no lyrics in the songbook (yet): keep what is here

    auto blocks = parseLyrics (folderLyrics.text.toStdString());
    // Starts: those saved in the folder, and over them the ones already here.
    const auto saved = songFolder.getChildFile (kTimingsFile);
    keepStarts (parseLyrics (readSmallText (saved).toStdString()), blocks);
    keepStarts (song.blocks(), blocks);

    // The same text with the same starts: leave the cursor and the undo alone.
    const auto& now = song.blocks();
    bool same = now.size() == blocks.size();
    for (std::size_t b = 0; same && b < blocks.size(); ++b)
    {
        same = now[b].header == blocks[b].header && now[b].lines.size() == blocks[b].lines.size();
        for (std::size_t l = 0; same && l < blocks[b].lines.size(); ++l)
        {
            const auto& x = now[b].lines[l];
            const auto& y = blocks[b].lines[l];
            same = x.text == y.text && juce::exactlyEqual (x.offset, y.offset) && x.hasAt == y.hasAt
                   && (! x.hasAt || (x.atBar == y.atBar && x.atBeat == y.atBeat && x.atSixteenth == y.atSixteenth));
        }
    }
    if (same)
        return;

    song.setBlocks (std::move (blocks));
    hasUndo = false;
    tapCursor = TapCursor();
}

void BoothCueAudioProcessor::applyFolderChords()
{
    if (songFolder == juce::File() || folderChords.text.trim().isEmpty() || song.sections().empty())
        return;   // no chart, or no parts yet to lay it on (it is applied again when the set is read)
    auto result = chartToChords (folderChords.text.toStdString(), song.sections(), song.timeSig());
    if (result.chords.empty())
        return;
    song.setChords (std::move (result.chords), song.key());
    chordsEdited = true;
    hasChordUndo = false;
}

void BoothCueAudioProcessor::updateSongFolderStatus()
{
    if (songFolder == juce::File())
    {
        songFolderStatus = {};
        return;
    }
    if (! songFolder.isDirectory())
    {
        songFolderStatus = "Song folder \"" + songFolder.getFileName() + "\" not found.";
        return;
    }
    juce::StringArray has;
    if (folderLyrics.text.trim().isNotEmpty())
        has.add ("lyrics");
    if (folderChords.text.trim().isNotEmpty())
        has.add ("chords");
    songFolderStatus = "Song \"" + songFolder.getFileName() + "\": "
                       + (has.isEmpty() ? juce::String ("no lyrics or chords in it yet") : has.joinIntoString (" and ") + " follow it");
    if (timingsWrittenAt != juce::Time())
        songFolderStatus << ". Timings saved " << timingsWrittenAt.toString (false, true, false, true) << ".";
    else
        songFolderStatus << ".";
}

// ---- the chords database (message thread) -------------------------------------------------

juce::String BoothCueAudioProcessor::getChordChart() const
{
    return str (chordsToChart (song.sections(), song.chords(), song.key(), song.timeSig()));
}

ChartImport BoothCueAudioProcessor::setChordChart (const juce::String& text)
{
    JUCE_ASSERT_MESSAGE_THREAD
    auto result = chartToChords (text.toStdString(), song.sections(), song.timeSig());
    chordUndo = song.chords();
    chordUndoEdited = chordsEdited;
    hasChordUndo = true;
    song.setChords (result.chords, song.key());
    chordsEdited = true;
    ++contentVersion;
    updateStateSnapshot();
    return result;
}

void BoothCueAudioProcessor::setImportedChords (std::vector<ChordSpan> chords, Key key)
{
    JUCE_ASSERT_MESSAGE_THREAD
    chordUndo = song.chords();
    chordUndoEdited = chordsEdited;
    hasChordUndo = true;
    song.setChords (std::move (chords), key);
    chordsEdited = true;
    ++contentVersion;
    updateStateSnapshot();
}

void BoothCueAudioProcessor::useDetectedChords()
{
    JUCE_ASSERT_MESSAGE_THREAD
    chordUndo = song.chords();
    chordUndoEdited = chordsEdited;
    hasChordUndo = true;
    chordsEdited = false;
    song.setChords (detectedChords, detectedKey);
    ++contentVersion;
    updateStateSnapshot();
}

void BoothCueAudioProcessor::undoChords()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (! hasChordUndo)
        return;
    song.setChords (chordUndo, song.key());
    chordsEdited = chordUndoEdited;
    hasChordUndo = false;
    ++contentVersion;
    updateStateSnapshot();
}

juce::String BoothCueAudioProcessor::getTimedLyrics() const
{
    return str (lyricsToTimedText (song));
}

// ---- tap-along (message thread) ----------------------------------------------------------

void BoothCueAudioProcessor::setTapSettings (const TapSettings& s)
{
    JUCE_ASSERT_MESSAGE_THREAD
    tapSettings.snapBeats = juce::jlimit (0.0, 4.0, s.snapBeats);
    tapSettings.latencyMs = juce::jlimit (0.0, 500.0, s.latencyMs);
    ++contentVersion;
    updateStateSnapshot();
}

void BoothCueAudioProcessor::setEditorSize (juce::Point<int> size)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (size == editorSize)
        return;
    editorSize = size;
    updateStateSnapshot();
}

void BoothCueAudioProcessor::setTapCursor (int block, int line)
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (block == tapCursor.block && line == tapCursor.line)
        return;
    tapCursor = TapCursor { block, line, -1 };
    ++contentVersion;
}

void BoothCueAudioProcessor::pushUndo (const juce::String& name)
{
    undoBlocks = song.blocks();
    hasUndo = true;
    undoName = name;
}

void BoothCueAudioProcessor::timingsChanged()
{
    timingsDirty = true;
    ++contentVersion;
    updateStateSnapshot();
}

bool BoothCueAudioProcessor::tap()
{
    JUCE_ASSERT_MESSAGE_THREAD
    const double now = juce::Time::getMillisecondCounterHiRes();
    lastTapMs = now;

    const auto t = transport.read();
    if (! t.valid)
    {
        lastTapOk = false;
        lastTapMessage = "No host position: start Live's transport";
        ++contentVersion;
        return false;
    }

    const double ppq = tapPosition (t, now, tapSettings.latencyMs, tapSettings.snapBeats);
    auto before = song.blocks();
    const auto r = tapAt (song, tapCursor, ppq);
    lastTapOk = r.ok;
    if (! r.ok)
    {
        lastTapMessage = str (r.error);
        ++contentVersion;
        return false;
    }

    undoBlocks = std::move (before);
    hasUndo = true;
    undoName = "tap";
    const auto& text = song.blocks()[static_cast<std::size_t> (r.block)].lines[static_cast<std::size_t> (r.line)].text;
    lastTapMessage = "\"" + str (text) + "\" at "
                     + str (formatPosition (ppq, TimeSig { t.sigNum, t.sigDen }));
    timingsChanged();
    return true;
}

void BoothCueAudioProcessor::nudgeLine (int block, int line, double beats)
{
    JUCE_ASSERT_MESSAGE_THREAD
    auto before = song.blocks();
    if (song.nudge (block, line, beats))
    {
        undoBlocks = std::move (before);
        hasUndo = true;
        undoName = "nudge";
        timingsChanged();
    }
}

void BoothCueAudioProcessor::clearLine (int block, int line)
{
    JUCE_ASSERT_MESSAGE_THREAD
    pushUndo ("clear");
    song.clearTiming (block, line);
    timingsChanged();
}

void BoothCueAudioProcessor::clearAllTimings()
{
    JUCE_ASSERT_MESSAGE_THREAD
    pushUndo ("clear all");
    song.clearAllTimings();
    tapCursor = TapCursor();
    timingsChanged();
}

void BoothCueAudioProcessor::undo()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (! hasUndo)
        return;
    song.setBlocks (undoBlocks);
    hasUndo = false;
    // Put the cursor back on the line that was undone, so it can be tapped again.
    if (undoName == "tap" && tapCursor.line > 0)
        --tapCursor.line;
    lastTapMessage = "Undid the " + undoName;
    lastTapOk = true;
    lastTapMs = juce::Time::getMillisecondCounterHiRes();
    timingsChanged();
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
    updateStateSnapshot();
}

void BoothCueAudioProcessor::setLyrics (std::vector<LyricBlock> blocks)
{
    JUCE_ASSERT_MESSAGE_THREAD
    song.setBlocks (std::move (blocks));
    hasUndo = false;   // an undo would bring back the old text
    tapCursor = TapCursor();
    ++contentVersion;
    updateStateSnapshot();
}

void BoothCueAudioProcessor::setDisplaySettings (const DisplaySettings& s)
{
    JUCE_ASSERT_MESSAGE_THREAD
    displaySettings = s;
    displaySettings.fontScale = juce::jlimit (0.5f, 2.0f, s.fontScale);
    displaySettings.count.countInBars = juce::jlimit (0, 8, s.count.countInBars);
    displaySettings.count.entryGapBars = juce::jlimit (0.5, 32.0, s.count.entryGapBars);
    ++contentVersion;
    updateStateSnapshot();
}

void BoothCueAudioProcessor::showDisplay()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (displayWindow == nullptr)
    {
        displayWindow = std::make_unique<DisplayWindow> (*this);
        if (displayPlacement.isNotEmpty())
            displayWindow->restoreWindowStateFromString (displayPlacement);
        displayWindow->onPlacementChanged = [this]
        {
            // "fs x y w h" / "x y w h" (ResizableWindow's format; its restore
            // keeps the window on a screen that exists). While full screen, the
            // windowed bounds from before are kept.
            if (displayWindow != nullptr && displayWindow->isVisible())
            {
                const bool fullScreen = displayWindow->isFullScreen();
                if (! fullScreen)
                    windowedBounds = displayWindow->getBounds();
                if (! windowedBounds.isEmpty())
                    displayPlacement = (fullScreen ? "fs " : "") + windowedBounds.toString();
            }
            updateStateSnapshot();
        };
    }
    displayWindow->setTrackName (getTrackName());
    // The window is made off the desktop (so a restored placement applies
    // before it appears); it needs a native window of its own to be seen.
    if (! displayWindow->isOnDesktop())
        displayWindow->addToDesktop();
    displayWindow->setVisible (true);
    displayWindow->toFront (true);
    // Full screen is only possible once the window is on screen.
    if (displayPlacement.startsWithIgnoreCase ("fs") && ! displayWindow->isFullScreen())
        displayWindow->setFullScreen (true);
    updateStateSnapshot();
}

void BoothCueAudioProcessor::hideDisplay()
{
    JUCE_ASSERT_MESSAGE_THREAD
    if (displayWindow != nullptr)
        displayWindow->closeButtonPressed();
}

bool BoothCueAudioProcessor::isDisplayVisible() const
{
    return displayWindow != nullptr && displayWindow->isOnDesktop() && displayWindow->isVisible();
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
// The linked set, the last structure read from it, and the lyrics with their
// timings. Display settings and the display window are added in TODO step 6.

void BoothCueAudioProcessor::updateStateSnapshot()
{
    juce::ValueTree state (kStateType);
    state.setProperty (kVersion, kStateVersion, nullptr);
    if (songFolder != juce::File())
        state.setProperty (kSongFolder, songFolder.getFullPathName(), nullptr);

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
            if (sec.color >= 0)
                t.setProperty (kColour, sec.color, nullptr);
            set.appendChild (t, nullptr);
        }
        state.appendChild (set, nullptr);
    }

    {
        juce::ValueTree display (kDisplay);
        const auto& d = displaySettings;
        display.setProperty (kShowPart, d.showSongPart, nullptr);
        display.setProperty (kShowLyric, d.showLyrics, nullptr);
        display.setProperty (kShowChord, d.showChords, nullptr);
        display.setProperty (kShowBass, d.showBassNote, nullptr);
        display.setProperty (kScale, d.fontScale, nullptr);
        display.setProperty (kCount, d.count.enabled, nullptr);
        display.setProperty (kCountBars, d.count.countInBars, nullptr);
        display.setProperty (kEntryGap, d.count.entryGapBars, nullptr);
        display.setProperty (kOpen, isDisplayVisible(), nullptr);
        display.setProperty (kPlacement, displayPlacement, nullptr);
        state.appendChild (display, nullptr);

        juce::ValueTree tap (kTap);
        tap.setProperty (kSnap, tapSettings.snapBeats, nullptr);
        tap.setProperty (kLatency, tapSettings.latencyMs, nullptr);
        state.appendChild (tap, nullptr);

        if (editorSize.x > 0 && editorSize.y > 0)
        {
            juce::ValueTree editor (kEditor);
            editor.setProperty (kWidth, editorSize.x, nullptr);
            editor.setProperty (kHeight, editorSize.y, nullptr);
            state.appendChild (editor, nullptr);
        }
    }

    if (! song.chords().empty())
    {
        juce::ValueTree chords (kChords);
        chords.setProperty (kTonic, song.key().tonic, nullptr);
        chords.setProperty (kMinor, song.key().minor, nullptr);
        chords.setProperty (kSummary, linkStatus.chordSummary, nullptr);
        chords.setProperty (kEdited, chordsEdited, nullptr);
        for (const auto& c : song.chords())
        {
            juce::ValueTree t (kChord);
            t.setProperty ("s", c.start, nullptr);
            t.setProperty ("e", c.end, nullptr);
            t.setProperty ("r", c.chord.root, nullptr);
            t.setProperty ("q", static_cast<int> (c.chord.quality), nullptr);
            t.setProperty ("b", c.chord.bass, nullptr);
            t.setProperty ("c", c.confidence, nullptr);
            chords.appendChild (t, nullptr);
        }
        state.appendChild (chords, nullptr);
    }

    if (! song.blocks().empty())
    {
        juce::ValueTree lyrics (kLyrics);
        for (const auto& b : song.blocks())
        {
            juce::ValueTree block (kBlock);
            block.setProperty (kHeader, str (b.header), nullptr);
            for (const auto& l : b.lines)
            {
                juce::ValueTree line (kLine);
                line.setProperty (kText, str (l.text), nullptr);
                line.setProperty (kOffset, l.offset, nullptr);
                block.appendChild (line, nullptr);
            }
            lyrics.appendChild (block, nullptr);
        }
        state.appendChild (lyrics, nullptr);
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
        {
            // An older state still parked for the timer must not come back over this one.
            const juce::ScopedLock sl (stateLock);
            pendingState = {};
            hasPendingState = false;
        }
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
                                  static_cast<double> (t[kStart]), static_cast<double> (t[kEnd]),
                                  static_cast<int> (t.getProperty (kColour, -1)) });

    std::vector<LyricBlock> blocks;
    for (const auto& b : state.getChildWithName (kLyrics))
    {
        if (! b.hasType (kBlock))
            continue;
        LyricBlock block;
        block.header = b[kHeader].toString().toStdString();
        for (const auto& l : b)
            if (l.hasType (kLine))
                block.lines.push_back ({ l[kText].toString().toStdString(),
                                         l.hasProperty (kOffset) ? static_cast<double> (l[kOffset]) : -1.0 });
        blocks.push_back (std::move (block));
    }
    song.setBlocks (std::move (blocks));
    hasUndo = false;
    tapCursor = TapCursor();

    {
        const auto c = state.getChildWithName (kChords);
        std::vector<ChordSpan> spans;
        for (const auto& t : c)
        {
            if (! t.hasType (kChord))
                continue;
            ChordSpan span;
            span.start = t["s"];
            span.end = t["e"];
            span.chord.root = t["r"];
            span.chord.quality = static_cast<Quality> (juce::jlimit (0, static_cast<int> (Quality::power), static_cast<int> (t["q"])));
            span.chord.bass = t["b"];
            span.confidence = t["c"];
            spans.push_back (span);
        }
        song.setChords (std::move (spans), Key { static_cast<int> (c.getProperty (kTonic, 0)), static_cast<bool> (c[kMinor]) });
        chordsEdited = static_cast<bool> (c[kEdited]) && ! song.chords().empty();
        hasChordUndo = false;
    }

    // Show the saved structure at once; the watcher re-reads the file (if it
    // is there) and replaces it.
    setSections (std::move (sections));

    const juce::String path = set[kPath].toString();
    linkStatus = LinkStatus();
    linkStatus.sections = static_cast<int> (song.sections().size());
    linkStatus.trackName = set[kTrack].toString();
    linkStatus.usedFallback = static_cast<bool> (set[kFallback]);
    linkStatus.chordSummary = state.getChildWithName (kChords)[kSummary].toString();
    if (juce::File::isAbsolutePath (path))
    {
        linkStatus.state = LinkStatus::State::reading;
        linkStatus.file = juce::File (path);
    }
    // Display and tap settings, and the windows (missing: the defaults).
    {
        const auto d = state.getChildWithName (kDisplay);
        DisplaySettings ds;
        ds.showSongPart = d.getProperty (kShowPart, ds.showSongPart);
        ds.showLyrics   = d.getProperty (kShowLyric, ds.showLyrics);
        ds.showChords   = d.getProperty (kShowChord, ds.showChords);
        ds.showBassNote = d.getProperty (kShowBass, ds.showBassNote);
        ds.fontScale    = static_cast<float> (d.getProperty (kScale, ds.fontScale));
        ds.count.enabled      = d.getProperty (kCount, ds.count.enabled);
        ds.count.countInBars  = d.getProperty (kCountBars, ds.count.countInBars);
        ds.count.entryGapBars = d.getProperty (kEntryGap, ds.count.entryGapBars);
        displaySettings = ds;
        displaySettings.fontScale = juce::jlimit (0.5f, 2.0f, ds.fontScale);
        displaySettings.count.countInBars = juce::jlimit (0, 8, ds.count.countInBars);
        displaySettings.count.entryGapBars = juce::jlimit (0.5, 32.0, ds.count.entryGapBars);

        const auto t = state.getChildWithName (kTap);
        tapSettings.snapBeats = juce::jlimit (0.0, 4.0, static_cast<double> (t.getProperty (kSnap, 0.0)));
        tapSettings.latencyMs = juce::jlimit (0.0, 500.0, static_cast<double> (t.getProperty (kLatency, 0.0)));

        const auto e = state.getChildWithName (kEditor);
        editorSize = { static_cast<int> (e.getProperty (kWidth, 0)), static_cast<int> (e.getProperty (kHeight, 0)) };

        displayPlacement = d[kPlacement].toString();
        {
            auto tokens = juce::StringArray::fromTokens (displayPlacement, false);
            if (tokens.size() > 0 && tokens[0].startsWithIgnoreCase ("fs"))
                tokens.remove (0);
            windowedBounds = tokens.size() >= 4 ? juce::Rectangle<int> (tokens[0].getIntValue(), tokens[1].getIntValue(),
                                                                          tokens[2].getIntValue(), tokens[3].getIntValue())
                                                : juce::Rectangle<int>();
        }
        if (displayWindow != nullptr && displayPlacement.isNotEmpty())
            displayWindow->restoreWindowStateFromString (displayPlacement);
        if (static_cast<bool> (d[kOpen]))
            showDisplay();
        else if (isDisplayVisible())
            displayWindow->closeButtonPressed();
    }

    // The song folder: its texts are read again (the state's lyrics keep their
    // starts where the text is the same).
    {
        const juce::String folder = state[kSongFolder].toString();
        songFolder = juce::File::isAbsolutePath (folder) ? juce::File (folder) : juce::File();
        folderLyrics = WatchedText();
        folderChords = WatchedText();
        timingsWritten = {};
        timingsWrittenAt = {};
        timingsDirty = false;
        if (songFolder != juce::File())
        {
            folderLyrics.read (songFolder.getChildFile (kLyricsFile));
            folderChords.read (songFolder.getChildFile (kChordsFile));
            applyFolderLyrics();
            applyFolderChords();
        }
        updateSongFolderStatus();
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
