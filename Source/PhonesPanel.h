#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace boothcue
{

// "Phones...": the songbook page for the band's phones, as a QR code to scan
// while tracking or rehearsing.
//
// The address comes from the Bovenburen manager running on this Mac
// (../bovenburen_manager, started with --lan): GET /api/phones on
// 127.0.0.1:8770, which it answers only to this computer, returns the page's
// network address with its key. A phone that opens it once is remembered by
// the manager. The request runs on its own thread; nothing here touches the
// audio thread or the plugin state.
class PhonesPanel final : public juce::Component
{
public:
    static constexpr int managerPort = 8770;

    PhonesPanel();                                    // asks the manager
    explicit PhonesPanel (const juce::String& address);   // a given address (checks, previews)
    ~PhonesPanel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // The QR code for text, row by row (true = dark); empty when it doesn't fit.
    static std::vector<std::vector<bool>> encode (const juce::String& text);

    juce::String getAddress() const { return address; }

private:
    class Fetch;

    void show (const juce::String& newAddress, const juce::String& newMessage);
    void ask();

    juce::String address, message;
    std::vector<std::vector<bool>> modules;
    juce::Rectangle<int> codeArea, textArea;
    juce::TextButton copyButton  { "Copy address" };
    juce::TextButton retryButton { "Try again" };
    std::unique_ptr<Fetch> fetch;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PhonesPanel)
};

}  // namespace boothcue
