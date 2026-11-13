#pragma once

#include <juce_core/juce_core.h>

#include "Prompter.h"
#include "Song.h"
#include "Transport.h"

namespace boothcue
{

// The live view for the band's phones: what the singer display shows, sent as
// JSON to the Bovenburen manager on this Mac (POST 127.0.0.1:8770/api/live),
// which passes it on to the phones that opened its live page.
//
// The processor's message-thread timer builds the JSON and posts it here; a
// thread of its own sends only the newest one, so a slow or missing manager
// never holds anything up. Without a manager the connection is refused at
// once and the feed waits a few seconds before trying again. Nothing touches
// the audio thread.
class LiveFeed final : private juce::Thread
{
public:
    static constexpr int managerPort = 8770;

    LiveFeed();
    ~LiveFeed() override;   // waits for a request in flight (at most its timeout)

    // Message thread: the newest scene; an older one not sent yet is dropped.
    void post (const juce::String& json);

    // True after the last request reached the manager.
    bool isConnected() const noexcept { return connected.load(); }

    // The JSON for a scene: the song's parts with their colours, the part,
    // lines, count-in and chords at the position, and the song folder's name
    // (the songbook's id for the song) when one is linked.
    static juce::String sceneJson (const PrompterScene&, const Song&, const TransportState&,
                                   const juce::String& songId, const juce::String& track);

private:
    void run() override;

    juce::CriticalSection lock;
    juce::String pending;
    bool hasPending = false;
    juce::WaitableEvent wake;
    std::atomic<bool> connected { false };
};

}  // namespace boothcue
