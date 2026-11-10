#include "PrompterView.h"

#include "DisplayWindow.h"
#include "PluginProcessor.h"

#include <cmath>

namespace boothcue
{

// ---- PrompterView ---------------------------------------------------------------------

bool PrompterView::Shown::operator== (const Shown& o) const
{
    return hasPosition == o.hasPosition && playing == o.playing && recording == o.recording
        && looping == o.looping && loopShown == o.loopShown && bar == o.bar && beat == o.beat
        && section == o.section && nextSection == o.nextSection && currentCue == o.currentCue
        && nextCue == o.nextCue && countKind == o.countKind && countValue == o.countValue
        && loopPass == o.loopPass && playheadX == o.playheadX && nextSectionIn == o.nextSectionIn
        && contentVersion == o.contentVersion && width == o.width && height == o.height
        && juce::exactlyEqual (tapNoteAt, o.tapNoteAt);
}

PrompterView::PrompterView (BoothCueAudioProcessor& p) : processor (p)
{
    setOpaque (true);
    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

PrompterView::~PrompterView()
{
    stopTimer();
}

void PrompterView::timerCallback()
{
    if (const auto track = processor.getTrackName(); track != titleTrack)
    {
        titleTrack = track;
        if (auto* window = findParentComponentOfClass<DisplayWindow>())
            window->setTrackName (track);
    }

    const auto& settings = processor.getDisplaySettings();
    scene = buildScene (processor.getSong(), processor.getTransport(), settings);

    // A tap in the last 1.5 s: say what it did.
    const double tapAt = processor.getLastTapTimeMs();
    const bool showTap = tapAt > 0.0 && juce::Time::getMillisecondCounterHiRes() - tapAt < 1500.0;
    if (showTap)
    {
        scene.tapNote = processor.getLastTapMessage().toStdString();
        scene.tapNoteOk = processor.wasLastTapOk();
    }

    Shown now;
    now.hasPosition = scene.hasPosition;
    now.playing     = scene.playing;
    now.recording   = scene.recording;
    now.looping     = scene.looping;
    now.loopShown   = scene.loopShown;
    now.bar         = scene.barBeat.bar;
    now.beat        = scene.barBeat.beat;
    now.section     = scene.section;
    now.nextSection = scene.nextSection;
    now.currentCue  = scene.currentCue;
    now.nextCue     = scene.nextCue;
    now.countKind   = static_cast<int> (scene.countdown.kind);
    now.countValue  = scene.countdown.value;
    now.loopPass    = scene.loopPass;
    now.playheadX   = PrompterPainter::playheadX (getLocalBounds(), scene, settings);
    now.nextSectionIn = scene.nextSection >= 0
                            ? static_cast<int> (std::ceil (scene.beatsToNextSection / scene.sig.beatLength())) : -1;
    now.contentVersion = processor.getContentVersion();
    now.tapNoteAt   = showTap ? tapAt : 0.0;
    now.width       = getWidth();
    now.height      = getHeight();

    if (first || ! (now == shown))
    {
        first = false;
        shown = now;
        repaint();
    }
}

void PrompterView::paint (juce::Graphics& g)
{
    painter.paint (g, getLocalBounds(), scene, processor.getSong(), processor.getDisplaySettings());
}

bool PrompterView::keyPressed (const juce::KeyPress& key)
{
    if (key.getTextCharacter() == 'f' || key.getTextCharacter() == 'F')
    {
        toggleFullScreen();
        return true;
    }
    if (key == juce::KeyPress::escapeKey)
    {
        if (auto* w = findParentComponentOfClass<juce::ResizableWindow>(); w != nullptr && w->isFullScreen())
            w->setFullScreen (false);
        return true;
    }
    if (key.getTextCharacter() == 't' || key.getTextCharacter() == 'T' || key == juce::KeyPress::spaceKey)
    {
        if (onTap)
            onTap();
        return true;
    }
    if (key == juce::KeyPress::backspaceKey)
    {
        if (onUndo)
            onUndo();
        return true;
    }
    return false;
}

void PrompterView::mouseDoubleClick (const juce::MouseEvent&)
{
    toggleFullScreen();
}

void PrompterView::toggleFullScreen()
{
    if (auto* w = findParentComponentOfClass<juce::ResizableWindow>())
        w->setFullScreen (! w->isFullScreen());
}

}  // namespace boothcue
