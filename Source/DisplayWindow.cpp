#include "DisplayWindow.h"

#include "PluginProcessor.h"
#include "PrompterView.h"
#include "Theme.h"

namespace boothcue
{

DisplayWindow::DisplayWindow (BoothCueAudioProcessor& processor)
    : DocumentWindow ("BoothCue", theme::background, DocumentWindow::allButtons, false)
{
    setUsingNativeTitleBar (true);
    auto* view = new PrompterView (processor);
    view->onTap  = [&processor] { processor.tap(); };
    view->onUndo = [&processor] { processor.undo(); };
    setContentOwned (view, false);
    setResizable (true, false);
    setResizeLimits (480, 270, 16384, 16384);
    // No monitor reported (headless, or a display going away): don't centre on it.
    if (juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr)
        centreWithSize (1280, 720);
    else
        setBounds (0, 0, 1280, 720);
}

void DisplayWindow::moved()
{
    DocumentWindow::moved();
    if (onPlacementChanged)
        onPlacementChanged();
}

void DisplayWindow::resized()
{
    DocumentWindow::resized();
    if (onPlacementChanged)
        onPlacementChanged();
}

void DisplayWindow::closeButtonPressed()
{
    if (isFullScreen())
        setFullScreen (false);
    setVisible (false);
    if (onPlacementChanged)
        onPlacementChanged();
}

void DisplayWindow::setTrackName (const juce::String& track)
{
    setName (track.isEmpty() ? juce::String ("BoothCue") : "BoothCue - " + track);
}

}  // namespace boothcue
