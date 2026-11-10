#include "PluginEditor.h"

#include "Song.h"

namespace boothcue
{

namespace
{
    const juce::Colour kBackground { 0xff101214 };
    const juce::Colour kText       { 0xffeef1f4 };
    const juce::Colour kDim        { 0xff8a939c };
    const juce::Colour kPlay       { 0xff3ecf6e };
    const juce::Colour kRecord     { 0xffff4d4d };
    const juce::Colour kLoop       { 0xfff2b640 };

    juce::String beats (double ppq) { return juce::String (ppq, 2); }
}

BoothCueAudioProcessorEditor::BoothCueAudioProcessorEditor (BoothCueAudioProcessor& p)
    : AudioProcessorEditor (p), processor (p)
{
    setSize (480, 260);
    startTimerHz (30);
}

BoothCueAudioProcessorEditor::~BoothCueAudioProcessorEditor()
{
    stopTimer();
}

void BoothCueAudioProcessorEditor::timerCallback()
{
    shown = processor.getTransport();
    repaint();
}

void BoothCueAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBackground);

    auto area = getLocalBounds().reduced (20);

    g.setColour (kText);
    g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
    g.drawText ("BoothCue", area.removeFromTop (32), juce::Justification::centredLeft);

    g.setColour (kDim);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("v" JucePlugin_VersionString " - transport check (the editor comes in a later step)",
                area.removeFromTop (20), juce::Justification::centredLeft);
    area.removeFromTop (16);

    if (! shown.valid)
    {
        g.setColour (kDim);
        g.setFont (juce::FontOptions (18.0f));
        g.drawText ("No host position (start the transport, or this is the Standalone)",
                    area, juce::Justification::topLeft);
        return;
    }

    const TimeSig sig { shown.sigNum, shown.sigDen };
    const auto bb = barBeatAt (shown.ppq, sig, shown.hasLastBarStart, shown.lastBarStart);

    g.setColour (kText);
    g.setFont (juce::FontOptions (44.0f, juce::Font::bold));
    g.drawText (juce::String (bb.bar) + "." + juce::String (bb.beat),
                area.removeFromTop (52), juce::Justification::centredLeft);

    g.setFont (juce::FontOptions (16.0f));
    g.setColour (kDim);
    g.drawText ("beat " + beats (shown.ppq) + "   " + juce::String (shown.bpm, 1) + " BPM   "
                    + juce::String (shown.sigNum) + "/" + juce::String (shown.sigDen),
                area.removeFromTop (24), juce::Justification::centredLeft);
    area.removeFromTop (8);

    auto row = area.removeFromTop (24);
    auto tag = [&g, &row] (const juce::String& text, juce::Colour colour, bool on)
    {
        g.setColour (on ? colour : kDim.withAlpha (0.4f));
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (text, row.removeFromLeft (110), juce::Justification::centredLeft);
    };
    tag (shown.playing ? "PLAYING" : "STOPPED", kPlay, shown.playing);
    tag ("REC", kRecord, shown.recording);
    tag (shown.looping ? "LOOP" : "LOOP OFF", kLoop, shown.looping);

    if (shown.loopEnd > shown.loopStart)
    {
        area.removeFromTop (6);
        g.setColour (shown.looping ? kLoop : kDim);
        g.setFont (juce::FontOptions (16.0f));
        auto text = "loop " + beats (shown.loopStart) + " - " + beats (shown.loopEnd);
        if (shown.loopPass > 0)
            text << "   pass " << shown.loopPass;
        g.drawText (text, area.removeFromTop (24), juce::Justification::centredLeft);
    }
}

}  // namespace boothcue
