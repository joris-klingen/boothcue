#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

namespace boothcue
{

// The settings window inside Live (the singer looks at the separate display).
//
//   left   the linked Live set, the singer display (open, toggles, text size,
//          count-in), Phones... (a QR code for the band's phones to open the
//          songbook page), and a transport readout to check the host connection
//   right  two tabs, BoothCue's own database for the song (it never writes
//          to the Live set):
//          Lyrics - the text (paste, load, save with timings) and the line list:
//                   each block with the parts it is sung in, each line with
//                   where it starts; the line being sung is highlighted
//          Chords - the chords as a chart (edit, load, save); detected from the
//                   set until edited, then kept
//
// Tap-along: the selected line is the one the next tap stamps; Tap (or T /
// space here, when the host passes the key on; always in the display window)
// stamps it at the playhead and moves on to the next line.
class BoothCueAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           public juce::FileDragAndDropTarget,
                                           private juce::Timer
{
public:
    explicit BoothCueAudioProcessorEditor (BoothCueAudioProcessor&);
    ~BoothCueAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void paintOverChildren (juce::Graphics&) override;

    // Drop a file on the window: .mid -> chords, .txt -> the open tab's text
    // (chart or lyrics), .als -> link that set.
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override               { dropHover = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;

private:
    // The line list: one row per block header, one per line, and a note
    // naming the sections without lyrics.
    class LineList final : public juce::ListBoxModel
    {
    public:
        explicit LineList (BoothCueAudioProcessor& p) : processor (p) {}

        struct Row
        {
            enum class Kind { note, header, line };
            Kind kind = Kind::line;
            int block = -1, line = -1;
            juce::String text;   // note rows
        };

        void rebuild();
        int rowFor (int block, int line) const;
        const Row* rowAt (int index) const;
        int getNumRows() override { return static_cast<int> (rows.size()); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void selectedRowsChanged (int lastRowSelected) override { if (onSelect) onSelect (lastRowSelected); }

        int sungBlock = -1, sungLine = -1;
        std::function<void (int)> onSelect;

    private:
        BoothCueAudioProcessor& processor;
        std::vector<Row> rows;
    };

    void timerCallback() override;
    void pushSettings();
    void pullSettings();
    void chooseSet();
    juce::String linkText (juce::Colour&) const;

    void pushTapSettings();
    void syncSelectionToCursor();
    bool selectedLine (int& block, int& line) const;

    // "Drag MIDI": drag it into Live (a chord clip), or click it to save the .mid.
    class MidiDragHandle final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        std::function<juce::File()> makeFile;
        std::function<void()> onClick;
        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseEnter (const juce::MouseEvent&) override { repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { repaint(); }
    private:
        bool dragging = false;
    };

    juce::File writeChordsMidiForDrag();
    void saveChordsMidi();
    void importChordsMidi (const juce::File&);
    juce::String songName() const;
    void showTab (bool chords);
    void saveTextFile (const juce::String& title, const juce::String& suffix, const juce::String& text);
    void applyChords();
    void revertChords();
    void loadChordsFile();
    void updateChordButtons();

    void loadLyricsFile();
    void applyLyrics();
    void revertLyrics();
    void updateLyricButtons();

    BoothCueAudioProcessor& processor;
    TransportState shown;
    juce::uint32 shownVersion = 0;

    // ---- left: set ----
    juce::TextButton linkButton   { "Link set..." };
    juce::TextButton reloadButton { "Reload" };
    juce::TextButton unlinkButton { "Unlink" };
    juce::TextButton songButton       { "Link song..." };
    juce::TextButton songUnlinkButton { "Unlink song" };
    juce::Rectangle<int> songArea;
    void chooseSongFolder();
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> linkArea;

    // ---- left: display ----
    juce::TextButton displayButton;
    juce::TextButton phonesButton { "Phones..." };   // the songbook page as a QR code (PhonesPanel)
    juce::ToggleButton showPart   { "Song part" };
    juce::ToggleButton showLyrics { "Lyrics" };
    juce::ToggleButton showChords { "Chords" };
    juce::ToggleButton showBass   { "Bass note" };
    juce::Label  sizeLabel { {}, "Text size" };
    juce::Slider sizeSlider;
    juce::Label  countLabel { {}, "Count-in" };
    juce::ComboBox countBox;
    juce::Label  entryLabel { {}, "Count lines after a rest of" };
    juce::ComboBox entryBox;
    juce::Label  leadLabel { {}, "Show a line ahead by" };
    juce::ComboBox leadBox;

    // ---- left: tap-along ----
    juce::Label  snapLabel { {}, "Snap taps to" };
    juce::ComboBox snapBox;
    juce::Label  latencyLabel { {}, "Tap latency" };
    juce::Slider latencySlider;
    juce::Rectangle<int> tapTitle;

    juce::Rectangle<int> readoutArea;

    // ---- right: tap row ----
    juce::TextButton tapButton      { "Tap" };
    juce::TextButton undoButton     { "Undo" };
    juce::TextButton earlierButton  { "< 1/4" };
    juce::TextButton laterButton    { "1/4 >" };
    juce::TextButton clearButton    { "Clear line" };
    juce::TextButton clearAllButton { "Clear all" };
    bool syncingSelection = false;
    bool dropHover = false;

    // ---- right: tabs ----
    juce::TextButton lyricsTab { "Lyrics" }, chordsTab { "Chords" };
    bool showingChords = false;
    juce::Rectangle<int> tabRow, noteRow;

    // ---- right: chords ----
    juce::TextButton chordLoad { "Load..." }, chordSave { "Save..." }, chordApply { "Apply" },
                     chordRevert { "Revert" }, chordUndo { "Undo" }, chordDetected { "Use detected" };
    MidiDragHandle chordMidi;
    juce::TextEditor chordsEditor;
    juce::String chordsApplied;     // what the chart box held when last applied / shown
    juce::String chordNote;         // what the last chart import noticed
    bool chordWarning = false;

    // ---- right: lyrics ----
    juce::TextButton saveLyricsButton { "Save..." };
    juce::TextButton loadButton   { "Load..." };
    juce::TextButton applyButton  { "Apply" };
    juce::TextButton revertButton { "Revert" };
    juce::TextEditor lyricsEditor;
    juce::String appliedText;       // what the editor held when last applied / loaded
    juce::String shownBlocksText;   // the song's lyrics as text, last time we looked
    juce::String importNote;        // what the last Apply / Load noticed
    bool importWarning = false;     // e.g. no section headers: nothing links
    LineList lineModel { processor };
    juce::ListBox lineList { "Lines", &lineModel };

    juce::Rectangle<int> leftColumn, linesTitle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoothCueAudioProcessorEditor)
};

}  // namespace boothcue
