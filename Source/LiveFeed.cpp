#include "LiveFeed.h"

#include "Theme.h"

namespace boothcue
{

namespace
{
    juce::String str (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

    juce::var sectionName (const Song& song, int index)
    {
        if (index < 0 || index >= static_cast<int> (song.sections().size()))
            return {};
        const auto& name = song.sections()[static_cast<size_t> (index)].name;
        return name.empty() ? juce::String ("(unnamed)") : str (name);
    }
}

LiveFeed::LiveFeed() : juce::Thread ("BoothCue live feed")
{
    startThread();
}

LiveFeed::~LiveFeed()
{
    signalThreadShouldExit();
    wake.signal();
    stopThread (3000);
}

void LiveFeed::post (const juce::String& json)
{
    {
        const juce::ScopedLock sl (lock);
        pending = json;
        hasPending = true;
    }
    wake.signal();
}

void LiveFeed::run()
{
    const juce::URL endpoint ("http://127.0.0.1:" + juce::String (managerPort) + "/api/live");
    while (! threadShouldExit())
    {
        wake.wait (1000);
        juce::String json;
        {
            const juce::ScopedLock sl (lock);
            if (! hasPending)
                continue;
            json = pending;
            hasPending = false;
        }

        int status = 0;
        auto stream = endpoint.withPOSTData (json).createInputStream (
            juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                .withExtraHeaders ("Content-Type: application/json")
                .withConnectionTimeoutMs (500)
                .withStatusCode (&status));
        const bool ok = stream != nullptr && status == 200;
        connected = ok;
        if (! ok)
            wait (3000);   // no manager: try again in a while, with the newest scene by then
    }
}

juce::String LiveFeed::sceneJson (const PrompterScene& s, const Song& song, const TransportState& t,
                                  const juce::String& songId, const juce::String& track)
{
    auto* o = new juce::DynamicObject();
    juce::var root (o);
    o->setProperty ("v", 1);
    o->setProperty ("song", songId);
    o->setProperty ("track", track);
    o->setProperty ("hasPosition", s.hasPosition);
    o->setProperty ("playing", s.playing);
    o->setProperty ("recording", s.recording);
    o->setProperty ("looping", s.looping);
    o->setProperty ("ppq", s.ppq);
    o->setProperty ("bpm", t.bpm);
    o->setProperty ("sig", juce::String (s.sig.num) + "/" + juce::String (s.sig.den));
    o->setProperty ("bar", s.barBeat.bar);
    o->setProperty ("beat", s.barBeat.beat);

    juce::Array<juce::var> parts;
    for (const auto& sec : song.sections())
    {
        auto* p = new juce::DynamicObject();
        p->setProperty ("name", sec.name.empty() ? juce::String ("(unnamed)") : str (sec.name));
        p->setProperty ("start", sec.start);
        p->setProperty ("end", sec.end);
        p->setProperty ("color", "#" + theme::partColour (sec).toDisplayString (false));
        parts.add (juce::var (p));
    }
    o->setProperty ("parts", parts);

    o->setProperty ("part", s.section);
    o->setProperty ("partName", sectionName (song, s.section));
    o->setProperty ("next", s.nextSection);
    o->setProperty ("nextName", sectionName (song, s.nextSection));
    if (s.nextSection >= 0)
        o->setProperty ("nextIn", str (describeDistance (s.beatsToNextSection, s.sig)));

    if (s.hasLyrics)
    {
        o->setProperty ("line", str (s.currentLine));
        o->setProperty ("lineAhead", s.lineAhead);   // shown ahead of its first word
        o->setProperty ("nextLine", str (s.nextLine));
        o->setProperty ("laterLine", str (s.laterLine));
        // The paragraph of the line (see DisplaySettings::paragraph), for phones that show that.
        juce::Array<juce::var> paragraph;
        for (const auto& line : s.paragraph)
            paragraph.add (str (line));
        o->setProperty ("paragraph", paragraph);
        if (s.nextCue >= 0)
            o->setProperty ("nextLineIn", str (describeDistance (s.beatsToNextLine, s.sig)));
        if (s.countdown.kind != Countdown::Kind::none)
            o->setProperty ("count", (s.countdown.kind == Countdown::Kind::beats ? juce::String (s.countdown.value)
                                      : juce::String (s.countdown.value) + (s.countdown.value == 1 ? " bar" : " bars")));
    }
    if (s.hasChords)
    {
        o->setProperty ("prevChord", str (s.prevChord));
        o->setProperty ("chord", str (s.chord));
        o->setProperty ("nextChord", str (s.nextChord));
        o->setProperty ("chordUnsure", s.chordUnsure);
        // The whole song's chords for the phone's ticker: [start, end, name].
        juce::Array<juce::var> chords;
        for (const auto& r : chordRuns (song, false))
            chords.add (juce::Array<juce::var> { r.start, r.end, str (r.name) });
        o->setProperty ("chords", chords);
    }
    return juce::JSON::toString (root, true);
}

}  // namespace boothcue
