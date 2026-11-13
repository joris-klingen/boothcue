#include "PhonesPanel.h"

#include "QrCode.h"
#include "Theme.h"

namespace boothcue
{

namespace
{
    const juce::String asking { "Asking the Bovenburen manager..." };
}

// The request to the manager, on its own thread. The panel waits for it when
// it closes (at most the connection timeout), so no plugin code runs after
// the panel, or the plugin, is gone.
class PhonesPanel::Fetch final : public juce::Thread
{
public:
    explicit Fetch (PhonesPanel& p) : juce::Thread ("BoothCue phones"), panel (&p) {}
    ~Fetch() override { stopThread (4000); }

    void run() override
    {
        juce::String found, note;
        const juce::URL url ("http://127.0.0.1:" + juce::String (managerPort) + "/api/phones");
        int status = 0;
        auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                 .withConnectionTimeoutMs (2000)
                                                 .withStatusCode (&status));
        if (stream == nullptr)
            note = "The Bovenburen manager isn't running on this Mac. Start it with phone access (--lan), then try again.";
        else
        {
            const auto reply = juce::JSON::parse (stream->readEntireStreamAsString());
            if (status != 200)
                note = "The manager didn't give the address (" + juce::String (status) + ").";
            else if (! static_cast<bool> (reply["lan"]))
                note = "The manager runs for this Mac only. Restart it with --lan so phones can reach it.";
            else
                found = reply["address"].toString();
        }
        if (threadShouldExit())
            return;
        juce::MessageManager::callAsync ([safe = panel, found, note]
        {
            if (safe != nullptr)
                safe->show (found, note);
        });
    }

private:
    juce::Component::SafePointer<PhonesPanel> panel;
};

PhonesPanel::PhonesPanel()
{
    copyButton.onClick  = [this] { juce::SystemClipboard::copyTextToClipboard (address); };
    retryButton.onClick = [this] { ask(); };
    addAndMakeVisible (copyButton);
    addChildComponent (retryButton);
    setSize (320, 420);
    ask();
}

PhonesPanel::PhonesPanel (const juce::String& givenAddress)
{
    copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (address); };
    addAndMakeVisible (copyButton);
    addChildComponent (retryButton);
    setSize (320, 420);
    show (givenAddress, {});
}

PhonesPanel::~PhonesPanel() = default;   // ~Fetch waits for the request

void PhonesPanel::ask()
{
    fetch.reset();
    show ({}, asking);
    fetch = std::make_unique<Fetch> (*this);
    fetch->startThread();
}

void PhonesPanel::show (const juce::String& newAddress, const juce::String& newMessage)
{
    address = newAddress;
    modules = encode (address);
    message = newMessage;
    if (address.isNotEmpty() && modules.empty())
        message = "The address is too long for a QR code.";
    copyButton.setVisible (address.isNotEmpty());
    retryButton.setVisible (address.isEmpty() && fetch != nullptr && message != asking);
    resized();
    repaint();
}

std::vector<std::vector<bool>> PhonesPanel::encode (const juce::String& text)
{
    std::vector<std::vector<bool>> out;
    const auto qr = encodeQr (text.toStdString());
    out.assign (static_cast<size_t> (qr.size), std::vector<bool> (static_cast<size_t> (qr.size)));
    for (int y = 0; y < qr.size; ++y)
        for (int x = 0; x < qr.size; ++x)
            out[static_cast<size_t> (y)][static_cast<size_t> (x)] = qr.dark (x, y);
    return out;
}

void PhonesPanel::resized()
{
    auto area = getLocalBounds().reduced (16);
    auto buttons = area.removeFromBottom (28);
    copyButton.setBounds (buttons.withSizeKeepingCentre (130, 28));
    retryButton.setBounds (buttons.withSizeKeepingCentre (110, 28));
    area.removeFromBottom (10);
    area.removeFromTop (28);   // title
    const int side = juce::jmin (area.getWidth(), area.getHeight() - 70);
    codeArea = area.removeFromTop (side).withSizeKeepingCentre (side, side);
    textArea = area.withTrimmedTop (8);
}

void PhonesPanel::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
    auto bounds = getLocalBounds().reduced (16);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
    g.drawText ("Songbook on your phone", bounds.removeFromTop (24), juce::Justification::centredLeft);

    if (! modules.empty())
    {
        // White square with a quiet zone of four modules, the code in whole pixels.
        const int n = static_cast<int> (modules.size());
        const int cell = juce::jmax (1, codeArea.getWidth() / (n + 8));
        const int full = cell * (n + 8);
        const auto square = codeArea.withSizeKeepingCentre (full, full);
        g.setColour (juce::Colours::white);
        g.fillRect (square);
        g.setColour (juce::Colours::black);
        const int x0 = square.getX() + 4 * cell, y0 = square.getY() + 4 * cell;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (modules[static_cast<size_t> (y)][static_cast<size_t> (x)])
                    g.fillRect (x0 + x * cell, y0 + y * cell, cell, cell);

        auto text = textArea;
        g.setColour (theme::dim);
        g.setFont (juce::FontOptions (13.0f));
        g.drawFittedText ("Same Wi-Fi as this Mac. Scan with the camera; "
                          "the phone is remembered after that.",
                          text.removeFromTop (36), juce::Justification::centred, 2);
        g.setFont (juce::FontOptions (11.0f));
        g.drawFittedText (address.upToFirstOccurrenceOf ("?", false, false), text.removeFromTop (18),
                          juce::Justification::centred, 1);
    }
    else
    {
        g.setColour (theme::dim);
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (message, codeArea.reduced (8), juce::Justification::centred, 6);
    }
}

}  // namespace boothcue
