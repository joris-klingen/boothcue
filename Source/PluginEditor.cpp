#include "PluginEditor.h"

#include "ChordChart.h"
#include "ChordMidi.h"
#include "LyricImport.h"
#include "PhonesPanel.h"
#include "Song.h"
#include "Theme.h"

namespace boothcue
{

namespace
{
    juce::String beats (double ppq) { return juce::String (ppq, 2); }
    juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    void sectionTitle (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
    {
        g.setColour (theme::dim);
        g.setFont (font (12.0f, true));
        g.drawText (text.toUpperCase(), area, juce::Justification::bottomLeft);
    }

    // Count-in choices: { enabled, bars announced }.
    struct CountChoice { const char* name; bool enabled; int bars; };
    const CountChoice kCountChoices[] = {
        { "Off", false, 0 },
        { "Last bar only (4-3-2-1)", true, 0 },
        { "1 bar, then 4-3-2-1", true, 1 },
        { "2 bars, then 4-3-2-1", true, 2 },
        { "4 bars, then 4-3-2-1", true, 4 },
    };
    const double kEntryChoices[] = { 1.0, 2.0, 4.0, 8.0 };

    struct LeadChoice { const char* name; double bars; };
    const LeadChoice kLeadChoices[] = {
        { "Nothing (at its first word)", 0.0 },
        { "A quarter bar", 0.25 },
        { "Half a bar", 0.5 },
        { "1 bar", 1.0 },
        { "2 bars", 2.0 },
    };

    constexpr int kLeftWidth = 330;
}

// ---- line list ----------------------------------------------------------------------------

void BoothCueAudioProcessorEditor::LineList::rebuild()
{
    rows.clear();
    const auto& song = processor.getSong();

    // Sections that no block links to: usually a header typo.
    if (! song.blocks().empty())
    {
        juce::StringArray missing;
        for (std::size_t i = 0; i < song.sections().size(); ++i)
            if (song.blockForSection (static_cast<int> (i)) < 0)
                missing.addIfNotAlreadyThere (str (song.sections()[i].name));
        if (! missing.isEmpty())
            rows.push_back ({ Row::Kind::note, -1, -1, "No lyrics for: " + missing.joinIntoString (", ") });
    }

    for (std::size_t b = 0; b < song.blocks().size(); ++b)
    {
        rows.push_back ({ Row::Kind::header, static_cast<int> (b), -1, {} });
        for (std::size_t l = 0; l < song.blocks()[b].lines.size(); ++l)
            rows.push_back ({ Row::Kind::line, static_cast<int> (b), static_cast<int> (l), {} });
    }
}

int BoothCueAudioProcessorEditor::LineList::rowFor (int block, int line) const
{
    for (std::size_t i = 0; i < rows.size(); ++i)
        if (rows[i].kind == Row::Kind::line && rows[i].block == block && rows[i].line == line)
            return static_cast<int> (i);
    return -1;
}

const BoothCueAudioProcessorEditor::LineList::Row* BoothCueAudioProcessorEditor::LineList::rowAt (int index) const
{
    return index >= 0 && index < static_cast<int> (rows.size()) ? &rows[static_cast<std::size_t> (index)] : nullptr;
}

void BoothCueAudioProcessorEditor::LineList::paintListBoxItem (int index, juce::Graphics& g, int width, int height,
                                                               bool selected)
{
    if (index < 0 || index >= static_cast<int> (rows.size()))
        return;
    const auto& row = rows[static_cast<std::size_t> (index)];
    const auto& song = processor.getSong();
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (8, 0);

    if (row.kind == Row::Kind::note)
    {
        g.setColour (theme::loop);
        g.setFont (font (13.0f));
        g.drawText (row.text, area, juce::Justification::centredLeft, true);
        return;
    }

    if (row.kind == Row::Kind::header)
    {
        const auto& block = song.blocks()[static_cast<std::size_t> (row.block)];
        g.setColour (theme::sectionFuture.withAlpha (0.6f));
        g.fillRect (0, 0, width, height);

        g.setFont (font (14.0f, true));
        const auto header = block.header.empty() ? juce::String ("(no header)") : "[" + str (block.header) + "]";
        g.setColour (theme::text);
        const int w = juce::roundToInt (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), header)) + 12;
        g.drawText (header, area.removeFromLeft (w), juce::Justification::centredLeft, false);

        juce::StringArray where;
        for (std::size_t i = 0; i < song.sections().size(); ++i)
            if (song.blockForSection (static_cast<int> (i)) == row.block)
                where.add (str (song.sections()[i].name) + " (bar " + juce::String (barBeatAt (song.sections()[i].start, {}).bar) + ")");

        g.setFont (font (13.0f));
        if (where.isEmpty())
        {
            g.setColour (theme::loop);
            g.drawText (song.sections().empty() ? juce::String ("not linked yet: link a Live set")
                        : block.header.empty() ? juce::String ("not linked: add a [Section] header above these lines")
                                               : "not linked: no section named \"" + str (block.header) + "\"",
                        area, juce::Justification::centredLeft, true);
        }
        else
        {
            g.setColour (theme::dim);
            g.drawText ("-> " + where.joinIntoString (", "), area, juce::Justification::centredLeft, true);
        }
        return;
    }

    // A lyric line: its start (tapped, or "~" for the even spread) and its text.
    const bool sung = row.block == sungBlock && row.line == sungLine;
    if (sung)
    {
        g.setColour (theme::sectionNow.withAlpha (0.35f));
        g.fillRect (0, 0, width, height);
    }
    if (selected)
    {
        g.setColour (theme::text.withAlpha (0.12f));
        g.fillRect (0, 0, width, height);
    }

    // The tap cursor: the line the next tap stamps.
    const auto cursor = processor.getTapCursor();
    auto marker = area.removeFromLeft (16);
    if (cursor.block == row.block && cursor.line == row.line)
    {
        juce::Path tri;
        const auto m = marker.toFloat().withSizeKeepingCentre (9.0f, 11.0f);
        tri.addTriangle (m.getX(), m.getY(), m.getX(), m.getBottom(), m.getRight(), m.getCentreY());
        g.setColour (theme::count);
        g.fillPath (tri);
    }

    const auto& line = song.blocks()[static_cast<std::size_t> (row.block)].lines[static_cast<std::size_t> (row.line)];
    const int section = song.firstSectionForBlock (row.block);
    auto posArea = area.removeFromLeft (84);
    if (section >= 0)
    {
        const double ppq = song.sections()[static_cast<std::size_t> (section)].start + song.resolvedOffset (row.block, row.line);
        const auto t = processor.getTransport();
        const TimeSig sig { t.sigNum > 0 ? t.sigNum : 4, t.sigDen > 0 ? t.sigDen : 4 };
        g.setFont (font (13.0f, line.timed()));
        g.setColour (line.timed() ? theme::count : theme::dim);
        g.drawText ((line.timed() ? "" : "~ ") + str (formatPosition (ppq, sig)), posArea,
                    juce::Justification::centredLeft, false);
    }
    g.setFont (font (14.0f));
    g.setColour (sung ? theme::text : theme::text.withAlpha (0.85f));
    g.drawText (str (line.text), area, juce::Justification::centredLeft, true);
}

// ---- editor -----------------------------------------------------------------------------------

BoothCueAudioProcessorEditor::BoothCueAudioProcessorEditor (BoothCueAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    // Set.
    linkButton.onClick = [this] { chooseSet(); };
    reloadButton.onClick = [this] { processor.reloadSet(); };
    unlinkButton.onClick = [this] { processor.unlinkSet(); };
    linkButton.setTooltip ("Pick this song's Live set (.als). BoothCue reads the sections from its \"parts\" "
                           "track and reads them again every time the set is saved.");
    songButton.onClick = [this] { chooseSongFolder(); };
    songUnlinkButton.onClick = [this] { processor.unlinkSongFolder(); };
    songButton.setTooltip ("Pick this song's folder in the songbook. Its lyrics.txt and chords.txt are shown and "
                           "followed when they change; the starts you tap are saved next to them as timings.txt.");
    for (auto* b : { &linkButton, &reloadButton, &unlinkButton, &songButton, &songUnlinkButton })
        addAndMakeVisible (*b);

    // Display.
    displayButton.onClick = [this]
    {
        if (processor.isDisplayVisible())
            processor.hideDisplay();
        else
            processor.showDisplay();
        timerCallback();
    };
    addAndMakeVisible (displayButton);

    phonesButton.onClick = [this]
    {
        juce::CallOutBox::launchAsynchronously (std::make_unique<PhonesPanel>(), phonesButton.getBounds(), this);
    };
    phonesButton.setTooltip ("Show a QR code for the band's phones: it opens the songbook page from the "
                             "Bovenburen manager on this Mac (started with phone access, --lan).");
    addAndMakeVisible (phonesButton);

    for (auto* t : { &showPart, &showLyrics, &showChords, &showBass })
    {
        t->onClick = [this] { pushSettings(); };
        addAndMakeVisible (*t);
    }
    showChords.setTooltip ("Previous, current and next chord, detected from the set's MIDI tracks");
    showBass.setTooltip ("The bass note under the chord, on its own");

    sizeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sizeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    sizeSlider.setRange (0.5, 2.0, 0.05);
    sizeSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
    sizeSlider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() / 100.0; };
    sizeSlider.onValueChange = [this] { pushSettings(); };

    int id = 1;
    for (const auto& c : kCountChoices)
        countBox.addItem (c.name, id++);
    countBox.onChange = [this] { pushSettings(); };
    for (const auto bars : kEntryChoices)
        entryBox.addItem (juce::String (static_cast<int> (bars)) + (bars > 1.0 ? " bars" : " bar"),
                          static_cast<int> (bars));
    entryBox.setTooltip ("Only lines that come after a rest at least this long get a count-in");
    entryBox.onChange = [this] { pushSettings(); };
    id = 1;
    for (const auto& c : kLeadChoices)
        leadBox.addItem (c.name, id++);
    leadBox.setTooltip ("How long before its first word a line takes the big position, so it can be read ahead. "
                        "Never more than half the time since the line before it; counted-in lines follow the count");
    leadBox.onChange = [this] { pushSettings(); };

    for (auto* l : { &sizeLabel, &countLabel, &entryLabel, &leadLabel })
    {
        l->setColour (juce::Label::textColourId, theme::dim);
        addAndMakeVisible (*l);
    }
    addAndMakeVisible (sizeSlider);
    addAndMakeVisible (countBox);
    addAndMakeVisible (entryBox);
    addAndMakeVisible (leadBox);

    // Lyrics.
    lyricsEditor.setMultiLine (true, true);
    lyricsEditor.setReturnKeyStartsNewLine (true);
    lyricsEditor.setTabKeyUsedAsCharacter (false);
    lyricsEditor.setScrollbarsShown (true);
    lyricsEditor.setFont (font (15.0f));
    lyricsEditor.setColour (juce::TextEditor::backgroundColourId, theme::sectionPast);
    lyricsEditor.setColour (juce::TextEditor::textColourId, theme::text);
    lyricsEditor.setColour (juce::TextEditor::outlineColourId, theme::faint);
    lyricsEditor.setTextToShowWhenEmpty ("Paste the lyrics here, with a [Section] header above each part:\n\n"
                                         "[Verse 1]\nFirst line\nSecond line\n\n[Chorus]\n...",
                                         theme::dim);
    lyricsEditor.onTextChange = [this] { updateLyricButtons(); };
    addAndMakeVisible (lyricsEditor);

    // Tabs.
    for (auto* t : { &lyricsTab, &chordsTab })
    {
        t->setClickingTogglesState (false);
        t->setColour (juce::TextButton::buttonOnColourId, theme::sectionNow.darker (0.3f));
        addAndMakeVisible (*t);
    }
    lyricsTab.onClick = [this] { showTab (false); };
    chordsTab.onClick = [this] { showTab (true); };

    // Chords.
    chordsEditor.setMultiLine (true, false);
    chordsEditor.setReturnKeyStartsNewLine (true);
    chordsEditor.setScrollbarsShown (true);
    chordsEditor.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 14.5f, juce::Font::plain)));
    chordsEditor.setColour (juce::TextEditor::backgroundColourId, theme::sectionPast);
    chordsEditor.setColour (juce::TextEditor::textColourId, theme::text);
    chordsEditor.setColour (juce::TextEditor::outlineColourId, theme::faint);
    chordsEditor.setTextToShowWhenEmpty ("Paste or type a chord chart, e.g.\n\n[verse]\n| Bbm | Ab | Eb . . Db | Bbm |\n\n"
                                         "or a progression per kind of part:\nVerse/Chorus: ||: Am | F | C | G :||",
                                         theme::dim);
    chordsEditor.onTextChange = [this] { updateChordButtons(); };
    addChildComponent (chordsEditor);
    chordLoad.onClick = [this] { loadChordsFile(); };
    chordSave.onClick = [this] { saveTextFile ("Save the chords as a chart", " - chords.txt", chordsEditor.getText()); };
    chordApply.onClick = [this] { applyChords(); };
    chordRevert.onClick = [this] { revertChords(); };
    chordUndo.onClick = [this] { processor.undoChords(); chordNote = "Undone."; chordWarning = false; revertChords(); };
    chordDetected.onClick = [this] { processor.useDetectedChords(); chordNote = {}; revertChords(); };
    chordApply.setTooltip ("Use this chart: the chords become yours, detection after a save no longer replaces them");
    chordDetected.setTooltip ("Back to the chords detected from the set's MIDI (follow every save again)");
    chordSave.setTooltip ("Save the chart as a text file (import it with Load, or paste it)");
    for (auto* b : { &chordLoad, &chordSave, &chordApply, &chordRevert, &chordUndo, &chordDetected })
        addChildComponent (*b);
    chordMidi.makeFile = [this] { return writeChordsMidiForDrag(); };
    chordMidi.onClick = [this] { saveChordsMidi(); };
    chordMidi.setTooltip ("Drag into Live (drop it at bar 1): a MIDI clip with these chords, to use as a chord track. "
                          "Click to save the .mid instead.");
    addChildComponent (chordMidi);

    saveLyricsButton.onClick = [this] { saveTextFile ("Save the lyrics (with their timings)", " - lyrics.txt",
                                                      processor.getTimedLyrics()); };
    saveLyricsButton.setTooltip ("Save the lyrics as text, with \"@bar.beat\" in front of every tapped line (Load reads it back)");
    addAndMakeVisible (saveLyricsButton);

    loadButton.onClick = [this] { loadLyricsFile(); };
    applyButton.onClick = [this] { applyLyrics(); };
    revertButton.onClick = [this] { revertLyrics(); };
    applyButton.setTooltip ("Use this text. Lines you didn't change keep their timing.");
    for (auto* b : { &loadButton, &applyButton, &revertButton })
        addAndMakeVisible (*b);

    // Tap-along.
    tapButton.onClick = [this] { processor.tap(); timerCallback(); };
    tapButton.setTooltip ("Stamp the marked line at the playhead (T or space in the singer display)");
    tapButton.setColour (juce::TextButton::buttonColourId, theme::count.withAlpha (0.85f));
    tapButton.setColour (juce::TextButton::textColourOffId, theme::background);
    undoButton.onClick = [this] { processor.undo(); };
    earlierButton.onClick = [this] { int b, l; if (selectedLine (b, l)) processor.nudgeLine (b, l, -0.25); };
    laterButton.onClick   = [this] { int b, l; if (selectedLine (b, l)) processor.nudgeLine (b, l, +0.25); };
    earlierButton.setTooltip ("Move the selected line a quarter beat earlier");
    laterButton.setTooltip ("Move the selected line a quarter beat later");
    clearButton.onClick = [this] { int b, l; if (selectedLine (b, l)) processor.clearLine (b, l); };
    clearButton.setTooltip ("Forget the selected line's tap: it goes back to the even spread");
    clearAllButton.onClick = [this] { processor.clearAllTimings(); };
    clearAllButton.setTooltip ("Forget every tap (Undo brings them back)");
    for (auto* b : { &tapButton, &undoButton, &earlierButton, &laterButton, &clearButton, &clearAllButton })
        addAndMakeVisible (*b);

    snapBox.addItem ("No snap", 1);
    snapBox.addItem ("16th notes", 2);
    snapBox.addItem ("8th notes", 3);
    snapBox.onChange = [this] { pushTapSettings(); };
    latencySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    latencySlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
    latencySlider.setRange (0.0, 200.0, 5.0);
    latencySlider.setTextValueSuffix (" ms");
    latencySlider.setTooltip ("Taps come in a little late; this much is taken off each one while playing");
    latencySlider.onValueChange = [this] { pushTapSettings(); };
    for (auto* l : { &snapLabel, &latencyLabel })
    {
        l->setColour (juce::Label::textColourId, theme::dim);
        addAndMakeVisible (*l);
    }
    addAndMakeVisible (snapBox);
    addAndMakeVisible (latencySlider);

    lineModel.onSelect = [this] (int index)
    {
        if (syncingSelection)
            return;
        if (const auto* row = lineModel.rowAt (index); row != nullptr && row->kind == LineList::Row::Kind::line)
            processor.setTapCursor (row->block, row->line);
        else if (row != nullptr && row->kind == LineList::Row::Kind::header)
            processor.setTapCursor (row->block, 0);
    };

    lineList.setRowHeight (26);
    lineList.setColour (juce::ListBox::backgroundColourId, theme::background);
    lineList.setColour (juce::ListBox::outlineColourId, theme::faint);
    lineList.setOutlineThickness (1);
    addAndMakeVisible (lineList);

    revertLyrics();   // the song's lyrics into the text box
    revertChords();   // and its chords into the chart box
    pullSettings();
    showTab (false);

    setResizable (true, true);
    setResizeLimits (860, 700, 2400, 1600);
    const auto saved = processor.getEditorSize();
    if (saved.x >= 860 && saved.y >= 700)
        setSize (saved.x, saved.y);
    else
        setSize (1040, 720);
    startTimerHz (30);
    timerCallback();
}

BoothCueAudioProcessorEditor::~BoothCueAudioProcessorEditor()
{
    stopTimer();
}

// ---- settings ------------------------------------------------------------------------------

void BoothCueAudioProcessorEditor::pullSettings()
{
    const auto& s = processor.getDisplaySettings();
    showPart.setToggleState (s.showSongPart, juce::dontSendNotification);
    showLyrics.setToggleState (s.showLyrics, juce::dontSendNotification);
    showChords.setToggleState (s.showChords, juce::dontSendNotification);
    showBass.setToggleState (s.showBassNote, juce::dontSendNotification);
    sizeSlider.setValue (s.fontScale, juce::dontSendNotification);

    int countId = 1;
    for (int i = 0; i < static_cast<int> (std::size (kCountChoices)); ++i)
    {
        const auto& c = kCountChoices[i];
        if (c.enabled == s.count.enabled && (! c.enabled || c.bars == s.count.countInBars))
            countId = i + 1;
    }
    countBox.setSelectedId (countId, juce::dontSendNotification);

    int entryId = static_cast<int> (kEntryChoices[0]);
    for (const auto bars : kEntryChoices)
        if (s.count.entryGapBars >= bars - 1.0e-6)
            entryId = static_cast<int> (bars);
    entryBox.setSelectedId (entryId, juce::dontSendNotification);
    entryBox.setEnabled (s.count.enabled);

    int leadId = 1;
    for (int i = 0; i < static_cast<int> (std::size (kLeadChoices)); ++i)
        if (s.leadBars >= kLeadChoices[i].bars - 1.0e-6)
            leadId = i + 1;
    leadBox.setSelectedId (leadId, juce::dontSendNotification);

    const auto& t = processor.getTapSettings();
    snapBox.setSelectedId (t.snapBeats >= 0.5 ? 3 : t.snapBeats >= 0.25 ? 2 : 1, juce::dontSendNotification);
    latencySlider.setValue (t.latencyMs, juce::dontSendNotification);
}

void BoothCueAudioProcessorEditor::pushSettings()
{
    auto s = processor.getDisplaySettings();
    s.showSongPart = showPart.getToggleState();
    s.showLyrics   = showLyrics.getToggleState();
    s.showChords   = showChords.getToggleState();
    s.showBassNote = showBass.getToggleState();
    s.fontScale    = static_cast<float> (sizeSlider.getValue());

    const int countIndex = countBox.getSelectedId() - 1;
    if (countIndex >= 0 && countIndex < static_cast<int> (std::size (kCountChoices)))
    {
        s.count.enabled     = kCountChoices[countIndex].enabled;
        s.count.countInBars = kCountChoices[countIndex].bars;
    }
    if (entryBox.getSelectedId() > 0)
        s.count.entryGapBars = entryBox.getSelectedId();
    const int leadIndex = leadBox.getSelectedId() - 1;
    if (leadIndex >= 0 && leadIndex < static_cast<int> (std::size (kLeadChoices)))
        s.leadBars = kLeadChoices[leadIndex].bars;

    entryBox.setEnabled (s.count.enabled);
    processor.setDisplaySettings (s);
}

void BoothCueAudioProcessorEditor::pushTapSettings()
{
    auto t = processor.getTapSettings();
    const int id = snapBox.getSelectedId();
    t.snapBeats = id == 3 ? 0.5 : id == 2 ? 0.25 : 0.0;
    t.latencyMs = latencySlider.getValue();
    processor.setTapSettings (t);
}

bool BoothCueAudioProcessorEditor::selectedLine (int& block, int& line) const
{
    const auto* row = lineModel.rowAt (lineList.getSelectedRow());
    if (row == nullptr || row->kind != LineList::Row::Kind::line)
        return false;
    block = row->block;
    line = row->line;
    return true;
}

void BoothCueAudioProcessorEditor::syncSelectionToCursor()
{
    const auto tapCursor = processor.getTapCursor();
    const int row = lineModel.rowFor (tapCursor.block, tapCursor.line);
    if (row < 0 || row == lineList.getSelectedRow())
        return;
    const juce::ScopedValueSetter<bool> guard (syncingSelection, true);
    lineList.selectRow (row, false, true);
}

bool BoothCueAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    // Only when typing isn't going to the lyrics box. Live passes few keys to
    // plugin windows; the singer display always takes T / space.
    if (lyricsEditor.hasKeyboardFocus (true))
        return false;
    const auto c = key.getTextCharacter();
    if (c == 't' || c == 'T' || key == juce::KeyPress::spaceKey)
    {
        processor.tap();
        timerCallback();
        return true;
    }
    if (key == juce::KeyPress::backspaceKey)
    {
        processor.undo();
        return true;
    }
    return false;
}

// ---- set --------------------------------------------------------------------------------------

void BoothCueAudioProcessorEditor::chooseSet()
{
    const auto current = processor.getLinkStatus().file;
    const auto start = current != juce::File() ? current.getParentDirectory()
                                               : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Link the Live set of this song", start, "*.als");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                                  processor.linkSet (file);
                          });
}

void BoothCueAudioProcessorEditor::chooseSongFolder()
{
    const auto current = processor.getSongFolder();
    const auto set = processor.getLinkStatus().file;
    const auto start = current != juce::File() ? current.getParentDirectory()
                     : set != juce::File()     ? set.getParentDirectory()
                                               : juce::File::getSpecialLocation (juce::File::userMusicDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Link this song's folder in the songbook", start);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto folder = fc.getResult();
                              if (folder.isDirectory())
                                  processor.linkSongFolder (folder);
                          });
}

juce::String BoothCueAudioProcessorEditor::linkText (juce::Colour& colour) const
{
    using State = BoothCueAudioProcessor::LinkStatus::State;
    const auto& st = processor.getLinkStatus();
    const auto name = st.file.getFileName();
    const auto lastRead = st.sections > 0 ? " Showing the last structure read (" + juce::String (st.sections)
                                                + " sections)." : juce::String();
    colour = theme::dim;

    switch (st.state)
    {
        case State::none:
            return st.sections > 0
                       ? "Not linked." + lastRead
                       : "No set linked. Link this song's .als: BoothCue reads the sections from its "
                         "\"parts\" track, and again on every save.";
        case State::reading:
            return "Reading " + name + "...";
        case State::missing:
            colour = theme::loop;
            return name + " not found (moved, renamed, or another computer)." + lastRead;
        case State::error:
            colour = theme::record;
            return name + ": " + st.error + "." + lastRead;
        case State::ok:
        {
            colour = theme::text;
            auto text = name + ": " + juce::String (st.sections) + " sections from \"" + st.trackName + "\", read "
                        + st.readAt.toString (false, true, true, true) + ".";
            if (st.usedFallback)
            {
                colour = theme::loop;
                text << " No track named \"parts\": used the 2nd track.";
            }
            text << "\n" << (st.chordSummary.isNotEmpty() ? st.chordSummary : juce::String ("No MIDI chords in the set."));
            return text;
        }
    }
    return {};
}

// ---- tabs, files, chords ---------------------------------------------------------------------

void BoothCueAudioProcessorEditor::showTab (bool chords)
{
    showingChords = chords;
    lyricsTab.setToggleState (! chords, juce::dontSendNotification);
    chordsTab.setToggleState (chords, juce::dontSendNotification);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &lyricsEditor, &loadButton, &saveLyricsButton,
                                                                        &applyButton, &revertButton, &lineList, &tapButton,
                                                                        &undoButton, &earlierButton, &laterButton,
                                                                        &clearButton, &clearAllButton })
        c->setVisible (! chords);
    for (juce::Component* c : std::initializer_list<juce::Component*> { &chordsEditor, &chordLoad, &chordSave, &chordApply,
                                                                        &chordRevert, &chordUndo, &chordDetected, &chordMidi })
        c->setVisible (chords);
    resized();
    repaint();
}

void BoothCueAudioProcessorEditor::saveTextFile (const juce::String& title, const juce::String& suffix, const juce::String& text)
{
    const auto set = processor.getLinkStatus().file;
    const auto folder = set != juce::File() ? set.getParentDirectory()
                                             : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    const auto name = (set != juce::File() ? set.getFileNameWithoutExtension() : juce::String ("song")) + suffix;
    chooser = std::make_unique<juce::FileChooser> (title, folder.getChildFile (name), "*.txt");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [text] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file != juce::File())
                                  file.replaceWithText (text, false, false, "\n");
                          });
}

void BoothCueAudioProcessorEditor::loadChordsFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a chord chart (text)", processor.getLinkStatus().file.getParentDirectory(),
                                                   "*.txt;*.text;*.md");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              chordsEditor.setText (file.loadFileAsString(), false);
                              applyChords();
                          });
}

void BoothCueAudioProcessorEditor::applyChords()
{
    const auto result = processor.setChordChart (chordsEditor.getText());
    juce::StringArray notes;
    notes.add (juce::String (result.partsFilled) + " part" + (result.partsFilled == 1 ? "" : "s") + " filled, "
               + juce::String (result.chords.size()) + " chords");
    if (! result.unknown.empty())
    {
        juce::StringArray u;
        for (const auto& w : result.unknown)
            u.addIfNotAlreadyThere (str (w));
        notes.add ("not chords: " + u.joinIntoString (" "));
    }
    if (! result.unmatched.empty())
    {
        juce::StringArray u;
        for (const auto& w : result.unmatched)
            u.add (str (w));
        notes.add ("no part for " + u.joinIntoString (", "));
    }
    chordWarning = ! result.unknown.empty() || ! result.unmatched.empty() || result.partsFilled == 0;
    chordNote = notes.joinIntoString ("; ");
    revertChords();   // show the chart as BoothCue now has it (normalised)
}

void BoothCueAudioProcessorEditor::revertChords()
{
    chordsApplied = processor.getSong().chords().empty() && processor.getSong().sections().empty()
                        ? juce::String() : processor.getChordChart();
    chordsEditor.setText (chordsApplied, false);
    updateChordButtons();
}

void BoothCueAudioProcessorEditor::updateChordButtons()
{
    const bool dirty = chordsEditor.getText() != chordsApplied;
    chordApply.setEnabled (dirty);
    chordRevert.setEnabled (dirty);
    chordUndo.setEnabled (processor.canUndoChords());
    chordDetected.setEnabled (processor.areChordsEdited() && processor.hasDetectedChords());
    repaint (noteRow);
}

// ---- chords as MIDI, drops ---------------------------------------------------------------------

juce::String BoothCueAudioProcessorEditor::songName() const
{
    const auto set = processor.getLinkStatus().file;
    return set != juce::File() ? set.getFileNameWithoutExtension() : juce::String ("song");
}

juce::File BoothCueAudioProcessorEditor::writeChordsMidiForDrag()
{
    // A file named after the song: Live names the dropped clip after it.
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("BoothCue");
    dir.createDirectory();
    const auto file = dir.getChildFile (juce::File::createLegalFileName (songName() + " chords.mid"));
    const auto t = processor.getTransport();
    const auto& song = processor.getSong();
    writeChordsMidi (file, song.chords(), song.key(), song.timeSig(), t.valid ? t.bpm : 120.0, songName() + " chords");
    return file;
}

void BoothCueAudioProcessorEditor::saveChordsMidi()
{
    const auto set = processor.getLinkStatus().file;
    const auto folder = set != juce::File() ? set.getParentDirectory()
                                             : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Save the chords as MIDI", folder.getChildFile (songName() + " chords.mid"), "*.mid");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (file == juce::File())
                                  return;
                              const auto t = processor.getTransport();
                              const auto& song = processor.getSong();
                              writeChordsMidi (file, song.chords(), song.key(), song.timeSig(), t.valid ? t.bpm : 120.0,
                                               songName() + " chords");
                          });
}

void BoothCueAudioProcessorEditor::importChordsMidi (const juce::File& file)
{
    const auto r = chordsFromMidiFile (file, processor.getSong().timeSig());
    if (! r.ok)
    {
        chordNote = file.getFileName() + ": " + r.error;
        chordWarning = true;
    }
    else
    {
        processor.setImportedChords (r.chords, r.key);
        chordNote = file.getFileName() + ": " + juce::String (r.chords.size()) + " chords "
                    + (r.fromNames ? "(by their names)" : "(detected from its notes)");
        chordWarning = false;
    }
    showTab (true);
    revertChords();
}

bool BoothCueAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("mid;midi;txt;text;md;als") || juce::File (f).isDirectory())
            return true;
    return false;
}

void BoothCueAudioProcessorEditor::filesDropped (const juce::StringArray& files, int, int)
{
    dropHover = false;
    repaint();
    for (const auto& path : files)
    {
        const juce::File file (path);
        if (file.isDirectory())
            processor.linkSongFolder (file);
        else if (file.hasFileExtension ("mid;midi"))
            importChordsMidi (file);
        else if (file.hasFileExtension ("als"))
            processor.linkSet (file);
        else if (file.hasFileExtension ("txt;text;md"))
        {
            if (showingChords)
            {
                chordsEditor.setText (file.loadFileAsString(), false);
                applyChords();
            }
            else
            {
                lyricsEditor.setText (file.loadFileAsString(), false);
                applyLyrics();
            }
        }
        else
            continue;
        break;   // one file at a time
    }
}

void BoothCueAudioProcessorEditor::paintOverChildren (juce::Graphics& g)
{
    if (! dropHover)
        return;
    g.setColour (theme::sectionNow.withAlpha (0.12f));
    g.fillAll();
    g.setColour (theme::sectionNow);
    g.drawRect (getLocalBounds(), 3);
}

void BoothCueAudioProcessorEditor::MidiDragHandle::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    const bool over = isMouseOver (true);
    g.setColour (theme::count.withAlpha (over ? 0.95f : 0.8f));
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (theme::background);
    // A small "grip" of dots, then the label.
    auto grip = r.removeFromLeft (18.0f).reduced (5.0f, 7.0f);
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
            g.fillEllipse (grip.getX() + j * 5.0f, grip.getY() + i * (grip.getHeight() / 2.5f), 2.5f, 2.5f);
    g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    g.drawText ("Drag MIDI", r, juce::Justification::centred, false);
}

void BoothCueAudioProcessorEditor::MidiDragHandle::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 5 || ! makeFile)
        return;
    dragging = true;
    const auto file = makeFile();
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                               [this] { dragging = false; });
}

void BoothCueAudioProcessorEditor::MidiDragHandle::mouseUp (const juce::MouseEvent& e)
{
    if (! dragging && e.getDistanceFromDragStart() < 5 && onClick)
        onClick();
    dragging = false;
}

// ---- lyrics -----------------------------------------------------------------------------------

void BoothCueAudioProcessorEditor::loadLyricsFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load lyrics (plain text, [Section] headers)",
                                                   processor.getLinkStatus().file.getParentDirectory(),
                                                   "*.txt;*.text;*.md");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto file = fc.getResult();
                              if (! file.existsAsFile())
                                  return;
                              lyricsEditor.setText (file.loadFileAsString(), false);
                              applyLyrics();
                          });
}

void BoothCueAudioProcessorEditor::applyLyrics()
{
    const auto text = lyricsEditor.getText();
    LyricImportReport report;
    auto blocks = parseLyrics (text.toStdString(), &report);
    keepTimings (processor.getSong().blocks(), blocks);

    // Say what the import made of the text.
    juce::StringArray notes;
    std::size_t lines = 0;
    for (const auto& b : blocks)
        lines += b.lines.size();
    importWarning = lines > 0 && ! report.anyHeader;
    if (importWarning)
        notes.add ("no section headers: add [Verse], [Chorus], ... above the lines so they can link");
    if (! report.title.empty())
        notes.add ("title \"" + str (report.title) + "\" skipped");
    if (! report.chordCharts.empty())
        notes.add (juce::String (report.chordCharts.size()) + " chord-chart line"
                   + (report.chordCharts.size() == 1 ? "" : "s") + " set aside");
    if (report.repeatMarkers > 0)
        notes.add (juce::String (report.repeatMarkers) + " repeat marker" + (report.repeatMarkers == 1 ? "" : "s"));
    if (report.stanzasMatched > 0)
        notes.add (juce::String (report.stanzasMatched) + " repeated stanza" + (report.stanzasMatched == 1 ? "" : "s")
                   + " recognised");
    if (report.pickupsJoined > 0)
        notes.add (juce::String (report.pickupsJoined) + " pickup" + (report.pickupsJoined == 1 ? "" : "s") + " joined");
    if (report.linesRepeated > 0)
        notes.add (juce::String (report.linesRepeated) + " line" + (report.linesRepeated == 1 ? "" : "s") + " from x2");
    importNote = notes.joinIntoString (", ");
    processor.setLyrics (std::move (blocks));
    appliedText = text;
    shownBlocksText = str (lyricsToText (processor.getSong().blocks()));
    updateLyricButtons();
}

void BoothCueAudioProcessorEditor::revertLyrics()
{
    shownBlocksText = str (lyricsToText (processor.getSong().blocks()));
    lyricsEditor.setText (shownBlocksText, false);
    appliedText = shownBlocksText;
    updateLyricButtons();
}

void BoothCueAudioProcessorEditor::updateLyricButtons()
{
    const bool dirty = lyricsEditor.getText() != appliedText;
    applyButton.setEnabled (dirty);
    revertButton.setEnabled (dirty);
    repaint (noteRow);
}

// ---- layout & paint ---------------------------------------------------------------------------

void BoothCueAudioProcessorEditor::resized()
{
    processor.setEditorSize ({ getWidth(), getHeight() });
    auto area = getLocalBounds().reduced (20);

    // Left column.
    leftColumn = area.removeFromLeft (kLeftWidth);
    area.removeFromLeft (24);
    auto left = leftColumn;
    left.removeFromTop (66);   // title

    left.removeFromTop (22);   // "Live set"
    auto links = left.removeFromTop (28);
    linkButton.setBounds (links.removeFromLeft (110));
    links.removeFromLeft (8);
    reloadButton.setBounds (links.removeFromLeft (80));
    links.removeFromLeft (8);
    unlinkButton.setBounds (links.removeFromLeft (80));
    linkArea = left.removeFromTop (72).withTrimmedTop (6);
    auto songs = left.removeFromTop (28);
    songButton.setBounds (songs.removeFromLeft (110));
    songs.removeFromLeft (8);
    songUnlinkButton.setBounds (songs.removeFromLeft (100));
    songArea = left.removeFromTop (42).withTrimmedTop (6);

    left.removeFromTop (22);   // "Singer display"
    {
        auto row = left.removeFromTop (30);
        displayButton.setBounds (row.removeFromLeft (220));
        row.removeFromLeft (8);
        phonesButton.setBounds (row);
    }
    left.removeFromTop (8);
    auto toggles = left.removeFromTop (48);
    auto toggleLeft = toggles.removeFromLeft (toggles.getWidth() / 2 - 20);
    showPart.setBounds (toggleLeft.removeFromTop (24));
    showLyrics.setBounds (toggleLeft);
    showChords.setBounds (toggles.removeFromTop (24));
    showBass.setBounds (toggles);
    left.removeFromTop (6);

    auto size = left.removeFromTop (26);
    sizeLabel.setBounds (size.removeFromLeft (80));
    sizeSlider.setBounds (size);
    left.removeFromTop (6);
    auto count = left.removeFromTop (26);
    countLabel.setBounds (count.removeFromLeft (80));
    countBox.setBounds (count);
    left.removeFromTop (6);
    auto entry = left.removeFromTop (26);
    entryLabel.setBounds (entry.removeFromLeft (180));
    entryBox.setBounds (entry);
    left.removeFromTop (6);
    auto lead = left.removeFromTop (26);
    leadLabel.setBounds (lead.removeFromLeft (150));
    leadBox.setBounds (lead);
    left.removeFromTop (6);

    tapTitle = left.removeFromTop (22);
    auto snap = left.removeFromTop (26);
    snapLabel.setBounds (snap.removeFromLeft (100));
    snapBox.setBounds (snap);
    left.removeFromTop (6);
    auto latency = left.removeFromTop (26);
    latencyLabel.setBounds (latency.removeFromLeft (100));
    latencySlider.setBounds (latency);
    left.removeFromTop (14);

    readoutArea = left;

    // Right column: the tabs, then the lyrics (text over the line list) or the chord chart.
    tabRow = area.removeFromTop (30);
    {
        auto row = tabRow;
        lyricsTab.setBounds (row.removeFromLeft (84).reduced (0, 1));
        lyricsTab.setConnectedEdges (juce::Button::ConnectedOnRight);
        chordsTab.setBounds (row.removeFromLeft (84).reduced (0, 1));
        chordsTab.setConnectedEdges (juce::Button::ConnectedOnLeft);
        auto place = [&row] (juce::Button& b, int w) { b.setBounds (row.removeFromRight (w).reduced (0, 1)); row.removeFromRight (6); };
        if (showingChords)
        {
            place (chordSave, 70);
            place (chordLoad, 70);
            chordMidi.setBounds (row.removeFromRight (juce::jmin (110, juce::jmax (0, row.getWidth()))).reduced (0, 1));
        }
        else
        {
            place (revertButton, 70);
            place (applyButton, 64);
            place (saveLyricsButton, 70);
            place (loadButton, 70);
        }
    }
    if (showingChords)
    {
        // A second row: what to do with the chart.
        area.removeFromTop (6);
        auto row = area.removeFromTop (28);
        for (auto* b : { &chordApply, &chordRevert, &chordUndo, &chordDetected })
        {
            b->setBounds (row.removeFromLeft (b == &chordDetected ? 110 : 70).reduced (0, 1));
            row.removeFromLeft (6);
        }
        noteRow = area.removeFromTop (40).withTrimmedTop (4);
        chordsEditor.setBounds (area);
        return;
    }
    noteRow = area.removeFromTop (24).withTrimmedTop (4);

    lyricsEditor.setBounds (area.removeFromTop (juce::jmax (160, area.getHeight() * 2 / 5)));
    area.removeFromTop (10);
    linesTitle = area.removeFromTop (24);
    area.removeFromTop (4);
    auto taps = area.removeFromTop (32);
    tapButton.setBounds (taps.removeFromLeft (96));
    for (auto* b : { &undoButton, &earlierButton, &laterButton, &clearButton, &clearAllButton })
    {
        taps.removeFromLeft (8);
        b->setBounds (taps.removeFromLeft (b == &clearButton ? 80 : b == &clearAllButton ? 70 : b == &undoButton ? 60 : 58));
    }
    area.removeFromTop (6);
    lineList.setBounds (area);
}

void BoothCueAudioProcessorEditor::timerCallback()
{
    displayButton.setButtonText (processor.isDisplayVisible() ? "Hide singer display" : "Open singer display");
    const bool linked = processor.getLinkStatus().file != juce::File();
    reloadButton.setEnabled (linked);
    unlinkButton.setEnabled (linked || ! processor.getSong().sections().empty());
    songUnlinkButton.setEnabled (processor.getSongFolder() != juce::File());

    const auto& song = processor.getSong();
    if (processor.getContentVersion() != shownVersion)
    {
        shownVersion = processor.getContentVersion();
        pullSettings();   // settings restored from the host follow here

        // Lyrics changed elsewhere (state restored, a tap): follow, unless the
        // text box holds edits not applied yet.
        const auto blocksText = str (lyricsToText (song.blocks()));
        if (blocksText != shownBlocksText && lyricsEditor.getText() == appliedText)
            revertLyrics();

        // Chords changed (detected after a save, restored, undone): the chart box
        // follows unless it holds edits not applied yet.
        if (chordsEditor.getText() == chordsApplied && processor.getChordChart() != chordsApplied)
            revertChords();
        updateChordButtons();

        lineModel.rebuild();
        lineList.updateContent();
        syncSelectionToCursor();
        lineList.repaint();
        repaint (linkArea);
        repaint (songArea);
        repaint (linesTitle);
    }

    int b = -1, l = -1;
    const bool lineSelected = selectedLine (b, l);
    const bool timed = lineSelected && processor.getSong().blocks()[static_cast<std::size_t> (b)]
                                           .lines[static_cast<std::size_t> (l)].timed();
    earlierButton.setEnabled (lineSelected && processor.getSong().firstSectionForBlock (b) >= 0);
    laterButton.setEnabled (earlierButton.isEnabled());
    clearButton.setEnabled (timed);
    undoButton.setEnabled (processor.canUndo());
    undoButton.setTooltip (processor.canUndo() ? "Undo the last " + processor.getUndoName() : juce::String());
    tapButton.setEnabled (! processor.getSong().cues().empty());

    shown = processor.getTransport();

    // Highlight the line being sung.
    int block = -1, line = -1;
    if (shown.valid)
        if (const int cue = song.cueAt (shown.ppq); cue >= 0)
        {
            block = song.cues()[static_cast<std::size_t> (cue)].block;
            line  = song.cues()[static_cast<std::size_t> (cue)].line;
        }
    if (block != lineModel.sungBlock || line != lineModel.sungLine)
    {
        lineModel.sungBlock = block;
        lineModel.sungLine  = line;
        lineList.repaint();
        if (shown.playing && block >= 0)
            lineList.scrollToEnsureRowIsOnscreen (lineModel.rowFor (block, line));
    }

    repaint (readoutArea);
}

void BoothCueAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto title = leftColumn.withHeight (66);
    g.setColour (theme::text);
    g.setFont (font (24.0f, true));
    g.drawText ("BoothCue", title.removeFromTop (30), juce::Justification::centredLeft);
    g.setColour (theme::dim);
    g.setFont (font (13.0f));
    g.drawText ("v" JucePlugin_VersionString "  -  display: F = full screen, Esc = leave it",
                title.removeFromTop (20), juce::Justification::centredLeft);

    sectionTitle (g, { leftColumn.getX(), leftColumn.getY() + 66, leftColumn.getWidth(), 18 }, "Live set");
    sectionTitle (g, { leftColumn.getX(), songArea.getBottom(), leftColumn.getWidth(), 18 }, "Singer display");

    // Under the tabs: what the text box holds / where the chords come from.
    {
        auto t = noteRow;
        g.setFont (font (12.5f));
        if (showingChords)
        {
            juce::String source;
            auto colour = theme::dim;
            if (processor.areChordsEdited())
                source = "Your chart is in use (saved with the set). Detection after a save is kept aside"
                         + juce::String (processor.hasDetectedChords() ? ": \"Use detected\" switches back." : ".");
            else if (! processor.getSong().chords().empty())
                source = "Detected from the set's MIDI, again after every save. Edit and Apply to keep your own chart.";
            else
                source = "No chords yet: link a set with MIDI, or paste / load a chart.";
            if (chordsEditor.getText() != chordsApplied)
            {
                source = "Not applied yet.";
                colour = theme::loop;
            }
            else if (chordNote.isNotEmpty())
            {
                source = chordNote + "\n" + source;
                colour = chordWarning ? theme::loop : theme::dim;
            }
            g.setColour (colour);
            g.drawFittedText (source, t, juce::Justification::topLeft, 2, 0.9f);
        }
        else if (lyricsEditor.getText() != appliedText)
        {
            g.setColour (theme::loop);
            g.drawText ("Not applied yet.", t, juce::Justification::centredLeft);
        }
        else if (importNote.isNotEmpty())
        {
            g.setColour (importWarning ? theme::loop : theme::dim);
            g.drawFittedText (importNote, t, juce::Justification::centredLeft, 1, 0.85f);
        }
    }
    sectionTitle (g, tapTitle, "Tap-along");
    if (! showingChords)
    {
        auto t = linesTitle;
        sectionTitle (g, t.removeFromLeft (60), "Lines");
        const auto message = processor.getLastTapMessage();
        if (message.isNotEmpty())
        {
            g.setColour (processor.wasLastTapOk() ? theme::text : theme::loop);
            g.setFont (font (13.0f));
            g.drawText ((processor.wasLastTapOk() ? "" : "Not tapped: ") + message, t,
                        juce::Justification::bottomRight, true);
        }
    }

    // The linked set.
    juce::Colour linkColour;
    g.setFont (font (13.0f));
    const auto status = linkText (linkColour);
    g.setColour (linkColour);
    g.drawFittedText (status, linkArea, juce::Justification::topLeft, 5, 1.0f);
    g.setColour (theme::dim);
    g.drawFittedText (processor.getSongFolder() != juce::File() ? processor.getSongFolderStatus()
                                                                : juce::String ("No song folder linked."),
                      songArea, juce::Justification::topLeft, 3, 1.0f);

    // Transport readout, to check the host connection.
    auto area = readoutArea;
    g.setColour (theme::faint);
    g.drawHorizontalLine (area.getY(), (float) area.getX(), (float) area.getRight());
    area.removeFromTop (10);

    if (! shown.valid)
    {
        g.setColour (theme::dim);
        g.setFont (font (13.0f));
        g.drawFittedText ("No host position (start the transport, or this is the Standalone)",
                          area.removeFromTop (40), juce::Justification::topLeft, 2);
        return;
    }

    const TimeSig sig { shown.sigNum, shown.sigDen };
    const auto bb = barBeatAt (shown.ppq, sig, shown.hasLastBarStart, shown.lastBarStart);

    auto line = area.removeFromTop (30);
    g.setColour (theme::text);
    g.setFont (font (24.0f, true));
    g.drawText (juce::String (bb.bar) + "." + juce::String (bb.beat), line.removeFromLeft (90),
                juce::Justification::centredLeft);

    auto tag = [&g, &line] (const juce::String& text, juce::Colour colour, bool on)
    {
        g.setColour (on ? colour : theme::dim.withAlpha (0.4f));
        g.setFont (font (12.0f, true));
        g.drawText (text, line.removeFromLeft (76), juce::Justification::centredLeft);
    };
    tag (shown.playing ? "PLAYING" : "STOPPED", theme::play, shown.playing);
    tag ("REC", theme::record, shown.recording);
    tag (shown.looping ? "LOOP" : "LOOP OFF", theme::loop, shown.looping);

    g.setFont (font (12.5f));
    g.setColour (theme::dim);
    auto text = juce::String (shown.bpm, 1) + " BPM  " + juce::String (shown.sigNum) + "/" + juce::String (shown.sigDen)
                + "  beat " + beats (shown.ppq);
    if (shown.loopEnd > shown.loopStart)
    {
        text << "\nloop " << beats (shown.loopStart) << " - " << beats (shown.loopEnd);
        if (shown.loopPass > 0)
            text << "  pass " << shown.loopPass;
    }
    g.drawFittedText (text, area.removeFromTop (36), juce::Justification::topLeft, 2);
}

}  // namespace boothcue
