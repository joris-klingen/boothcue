#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"

namespace boothcue
{

// The settings window inside Live (the singer looks at the separate display).
//
//   left   the linked Live set, the singer display (open, toggles, text size,
//          count-in), and a transport readout to check the host connection
//   right  the lyrics as text (paste, or load a .txt) and the line list: each
//          block with the sections it is linked to, each line with where it
//          starts; the line being sung is highlighted
//
// Tap-along controls are added in TODO step 5.
class BoothCueAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                           private juce::Timer
{
public:
    explicit BoothCueAudioProcessorEditor (BoothCueAudioProcessor&);
    ~BoothCueAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

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
        int getNumRows() override { return static_cast<int> (rows.size()); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;

        int sungBlock = -1, sungLine = -1;

    private:
        juce::String position (double ppq) const;

        BoothCueAudioProcessor& processor;
        std::vector<Row> rows;
    };

    void timerCallback() override;
    void pushSettings();
    void pullSettings();
    void chooseSet();
    juce::String linkText (juce::Colour&) const;

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
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> linkArea;

    // ---- left: display ----
    juce::TextButton displayButton;
    juce::ToggleButton showPart   { "Song part" };
    juce::ToggleButton showLyrics { "Lyrics" };
    juce::ToggleButton showChords { "Chords (coming)" };
    juce::ToggleButton showBass   { "Bass note (coming)" };
    juce::Label  sizeLabel { {}, "Text size" };
    juce::Slider sizeSlider;
    juce::Label  countLabel { {}, "Count-in" };
    juce::ComboBox countBox;
    juce::Label  entryLabel { {}, "Count lines after a rest of" };
    juce::ComboBox entryBox;

    juce::Rectangle<int> readoutArea;

    // ---- right: lyrics ----
    juce::TextButton loadButton   { "Load file..." };
    juce::TextButton applyButton  { "Apply" };
    juce::TextButton revertButton { "Revert" };
    juce::TextEditor lyricsEditor;
    juce::String appliedText;       // what the editor held when last applied / loaded
    juce::String shownBlocksText;   // the song's lyrics as text, last time we looked
    LineList lineModel { processor };
    juce::ListBox lineList { "Lines", &lineModel };

    juce::Rectangle<int> leftColumn, lyricsTitle, linesTitle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BoothCueAudioProcessorEditor)
};

}  // namespace boothcue
