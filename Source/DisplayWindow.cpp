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
    setContentOwned (new PrompterView (processor), false);
    setResizable (true, false);
    setResizeLimits (480, 270, 16384, 16384);
    centreWithSize (1280, 720);
}

void DisplayWindow::closeButtonPressed()
{
    if (isFullScreen())
        setFullScreen (false);
    setVisible (false);
}

void DisplayWindow::setTrackName (const juce::String& track)
{
    setName (track.isEmpty() ? juce::String ("BoothCue") : "BoothCue - " + track);
}

}  // namespace boothcue
