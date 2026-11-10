#include "PluginEditor.h"

#include "Song.h"
#include "Theme.h"

namespace boothcue
{

namespace
{
    juce::String beats (double ppq) { return juce::String (ppq, 2); }

    juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }
}

BoothCueAudioProcessorEditor::BoothCueAudioProcessorEditor (BoothCueAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    linkButton.onClick = [this] { chooseSet(); };
    reloadButton.onClick = [this] { processor.reloadSet(); };
    unlinkButton.onClick = [this] { processor.unlinkSet(); };
    linkButton.setTooltip ("Pick this song's Live set (.als). BoothCue reads the sections from its \"parts\" "
                           "track and reads them again every time the set is saved.");
    addAndMakeVisible (linkButton);
    addAndMakeVisible (reloadButton);
    addAndMakeVisible (unlinkButton);

    displayButton.onClick = [this]
    {
        if (processor.isDisplayVisible())
            processor.hideDisplay();
        else
            processor.showDisplay();
        timerCallback();
    };
    addAndMakeVisible (displayButton);

    for (auto* t : { &showPart, &showLyrics, &showChords, &showBass })
    {
        t->onClick = [this] { pushSettings(); };
        addAndMakeVisible (*t);
    }
    // Stored and saved already; nothing draws them until chord detection lands.
    showChords.setTooltip ("Shown once chord detection is built (see TODO.md)");
    showBass.setTooltip ("Shown once chord detection is built (see TODO.md)");

    sizeSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    sizeSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    sizeSlider.setRange (0.5, 2.0, 0.05);
    sizeSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
    sizeSlider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue() / 100.0; };
    sizeSlider.onValueChange = [this] { pushSettings(); };
    sizeLabel.setColour (juce::Label::textColourId, theme::dim);
    addAndMakeVisible (sizeLabel);
    addAndMakeVisible (sizeSlider);

    pullSettings();
    setSize (560, 480);
    startTimerHz (30);
    timerCallback();
}

BoothCueAudioProcessorEditor::~BoothCueAudioProcessorEditor()
{
    stopTimer();
}

void BoothCueAudioProcessorEditor::pullSettings()
{
    const auto& s = processor.getDisplaySettings();
    showPart.setToggleState (s.showSongPart, juce::dontSendNotification);
    showLyrics.setToggleState (s.showLyrics, juce::dontSendNotification);
    showChords.setToggleState (s.showChords, juce::dontSendNotification);
    showBass.setToggleState (s.showBassNote, juce::dontSendNotification);
    sizeSlider.setValue (s.fontScale, juce::dontSendNotification);
}

void BoothCueAudioProcessorEditor::pushSettings()
{
    auto s = processor.getDisplaySettings();
    s.showSongPart = showPart.getToggleState();
    s.showLyrics   = showLyrics.getToggleState();
    s.showChords   = showChords.getToggleState();
    s.showBassNote = showBass.getToggleState();
    s.fontScale    = static_cast<float> (sizeSlider.getValue());
    processor.setDisplaySettings (s);
}

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
            return text;
        }
    }
    return {};
}

void BoothCueAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);
    area.removeFromTop (66);   // title

    auto links = area.removeFromTop (30);
    linkButton.setBounds (links.removeFromLeft (120));
    links.removeFromLeft (8);
    reloadButton.setBounds (links.removeFromLeft (80));
    links.removeFromLeft (8);
    unlinkButton.setBounds (links.removeFromLeft (80));
    linkArea = area.removeFromTop (48).withTrimmedTop (6);
    area.removeFromTop (10);

    auto row = area.removeFromTop (32);
    displayButton.setBounds (row.removeFromLeft (220));
    area.removeFromTop (14);

    auto toggles = area.removeFromTop (52);
    auto left = toggles.removeFromLeft (toggles.getWidth() / 2);
    showPart.setBounds (left.removeFromTop (26));
    showLyrics.setBounds (left);
    showChords.setBounds (toggles.removeFromTop (26));
    showBass.setBounds (toggles);
    area.removeFromTop (8);

    auto size = area.removeFromTop (26);
    sizeLabel.setBounds (size.removeFromLeft (80));
    sizeSlider.setBounds (size);
    area.removeFromTop (16);

    readoutArea = area;
}

void BoothCueAudioProcessorEditor::timerCallback()
{
    displayButton.setButtonText (processor.isDisplayVisible() ? "Hide singer display" : "Open singer display");
    const bool linked = processor.getLinkStatus().file != juce::File();
    reloadButton.setEnabled (linked);
    unlinkButton.setEnabled (linked || ! processor.getSong().sections().empty());

    if (processor.getContentVersion() != shownVersion)
    {
        shownVersion = processor.getContentVersion();
        repaint (linkArea);
    }
    shown = processor.getTransport();
    repaint (readoutArea);
}

void BoothCueAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    auto title = getLocalBounds().reduced (20).removeFromTop (56);
    g.setColour (theme::text);
    g.setFont (font (24.0f, true));
    g.drawText ("BoothCue", title.removeFromTop (30), juce::Justification::centredLeft);
    g.setColour (theme::dim);
    g.setFont (font (13.0f));
    g.drawText ("v" JucePlugin_VersionString "  -  in the display: F = full screen, Esc = leave it",
                title, juce::Justification::centredLeft);

    // The linked set.
    juce::Colour linkColour;
    g.setFont (font (13.5f));
    const auto status = linkText (linkColour);
    g.setColour (linkColour);
    g.drawFittedText (status, linkArea, juce::Justification::topLeft, 3, 1.0f);

    // Transport readout, to check the host connection.
    auto area = readoutArea;
    g.setColour (theme::faint);
    g.drawHorizontalLine (area.getY(), (float) area.getX(), (float) area.getRight());
    area.removeFromTop (10);

    if (! shown.valid)
    {
        g.setColour (theme::dim);
        g.setFont (font (15.0f));
        g.drawText ("No host position (start the transport, or this is the Standalone)",
                    area.removeFromTop (24), juce::Justification::centredLeft);
        return;
    }

    const TimeSig sig { shown.sigNum, shown.sigDen };
    const auto bb = barBeatAt (shown.ppq, sig, shown.hasLastBarStart, shown.lastBarStart);

    auto line = area.removeFromTop (34);
    g.setColour (theme::text);
    g.setFont (font (28.0f, true));
    g.drawText (juce::String (bb.bar) + "." + juce::String (bb.beat), line.removeFromLeft (110),
                juce::Justification::centredLeft);

    auto tag = [&g, &line] (const juce::String& text, juce::Colour colour, bool on)
    {
        g.setColour (on ? colour : theme::dim.withAlpha (0.4f));
        g.setFont (font (14.0f, true));
        g.drawText (text, line.removeFromLeft (90), juce::Justification::centredLeft);
    };
    tag (shown.playing ? "PLAYING" : "STOPPED", theme::play, shown.playing);
    tag ("REC", theme::record, shown.recording);
    tag (shown.looping ? "LOOP" : "LOOP OFF", theme::loop, shown.looping);

    g.setFont (font (14.0f));
    g.setColour (theme::dim);
    auto text = "beat " + beats (shown.ppq) + "   " + juce::String (shown.bpm, 1) + " BPM   "
                + juce::String (shown.sigNum) + "/" + juce::String (shown.sigDen);
    if (shown.loopEnd > shown.loopStart)
    {
        text << "   loop " << beats (shown.loopStart) << " - " << beats (shown.loopEnd);
        if (shown.loopPass > 0)
            text << "  pass " << shown.loopPass;
    }
    g.drawText (text, area.removeFromTop (22), juce::Justification::centredLeft);
}

}  // namespace boothcue
