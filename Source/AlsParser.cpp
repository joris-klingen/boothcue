#include "AlsParser.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace boothcue
{

namespace
{
    bool isSpace (char c)   { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }

    // Locale-independent: hosts may run with a locale whose decimal separator
    // is a comma, which breaks strtod on Live's "12.5".
    bool parseDouble (const std::string& s, double& out)
    {
        const char* p = s.c_str();
        while (isSpace (*p)) ++p;
        double sign = 1.0;
        if (*p == '-') { sign = -1.0; ++p; }
        else if (*p == '+') ++p;

        bool anyDigits = false;
        double v = 0.0;
        for (; *p >= '0' && *p <= '9'; ++p) { v = v * 10.0 + (*p - '0'); anyDigits = true; }
        if (*p == '.')
        {
            ++p;
            double scale = 0.1;
            for (; *p >= '0' && *p <= '9'; ++p) { v += (*p - '0') * scale; scale *= 0.1; anyDigits = true; }
        }
        if (! anyDigits)
            return false;
        if (*p == 'e' || *p == 'E')
        {
            ++p;
            int esign = 1, e = 0;
            if (*p == '-') { esign = -1; ++p; }
            else if (*p == '+') ++p;
            for (; *p >= '0' && *p <= '9'; ++p) e = e * 10 + (*p - '0');
            v *= std::pow (10.0, esign * e);
        }
        while (isSpace (*p)) ++p;
        if (*p != '\0')
            return false;
        out = sign * v;
        return true;
    }

    std::string lowerTrimmed (const std::string& s)
    {
        std::size_t a = 0, b = s.size();
        while (a < b && isSpace (s[a])) ++a;
        while (b > a && isSpace (s[b - 1])) --b;
        std::string out = s.substr (a, b - a);
        for (auto& c : out)
            c = static_cast<char> (std::tolower (static_cast<unsigned char> (c)));
        return out;
    }

    void appendUtf8 (std::string& out, unsigned long cp)
    {
        if (cp < 0x80)        out += static_cast<char> (cp);
        else if (cp < 0x800)  { out += static_cast<char> (0xC0 | (cp >> 6));
                                out += static_cast<char> (0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000){ out += static_cast<char> (0xE0 | (cp >> 12));
                                out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
                                out += static_cast<char> (0x80 | (cp & 0x3F)); }
        else                  { out += static_cast<char> (0xF0 | (cp >> 18));
                                out += static_cast<char> (0x80 | ((cp >> 12) & 0x3F));
                                out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
                                out += static_cast<char> (0x80 | (cp & 0x3F)); }
    }

    std::string decodeEntities (const char* begin, const char* end)
    {
        std::string out;
        out.reserve (static_cast<std::size_t> (end - begin));
        for (const char* p = begin; p < end; ++p)
        {
            if (*p != '&') { out += *p; continue; }

            const char* semi = static_cast<const char*> (std::memchr (p, ';', static_cast<std::size_t> (end - p)));
            if (semi == nullptr || semi - p > 10) { out += *p; continue; }

            const std::string ent (p + 1, semi);
            if      (ent == "amp")  out += '&';
            else if (ent == "lt")   out += '<';
            else if (ent == "gt")   out += '>';
            else if (ent == "quot") out += '"';
            else if (ent == "apos") out += '\'';
            else if (ent.size() > 1 && ent[0] == '#')
            {
                const bool hex = ent[1] == 'x' || ent[1] == 'X';
                appendUtf8 (out, std::strtoul (ent.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10));
            }
            else { out += *p; continue; }
            p = semi;
        }
        return out;
    }

    const std::string& attribute (const XmlScanner::Attributes& attrs, const char* name)
    {
        static const std::string empty;
        for (const auto& a : attrs)
            if (a.first == name)
                return a.second;
        return empty;
    }

    // Collects, per track, its name and its arrangement MIDI clips, and stops as
    // soon as the structure track is known.
    class StructureHandler : public XmlScanner::Handler
    {
    public:
        StructureHandler (const std::string& wantedName, bool notes)
            : wanted (lowerTrimmed (wantedName)), readNotes (notes) {}

        void startElement (const std::string& name, const XmlScanner::Attributes& attrs) override
        {
            ++depth;
            if (depth == 1)       { isAbleton = (name == "Ableton"); return; }
            if (depth == 2)       { inLiveSet = isAbleton && name == "LiveSet"; return; }
            if (depth == 3)       { inTracks = inLiveSet && name == "Tracks"; return; }
            if (! inTracks)       return;

            if (depth == 4)
            {
                track = Track();
                track.type = name;
                rel.clear();
                return;
            }

            rel += '/';
            rel += name;

            if (rel == "/Name/UserName")            track.userName = attribute (attrs, "Value");
            else if (rel == "/Name/EffectiveName")  track.effectiveName = attribute (attrs, "Value");
            else if (rel == kClip)
            {
                clip = AlsSection();
                clipHasStart = parseDouble (attribute (attrs, "Time"), clip.start);
                clipHasEnd = false;
                content = Content();
            }
            else if (rel == kClipEnd)   clipHasEnd = parseDouble (attribute (attrs, "Value"), clip.end);
            else if (rel == kClipName)  clip.name = attribute (attrs, "Value");
            else if (rel == kClipColor)
            {
                double v = -1.0;
                if (parseDouble (attribute (attrs, "Value"), v) && v >= 0.0 && v < 70.0)
                    clip.color = static_cast<int> (v);
            }
            else if (readNotes)
                noteElement (name, attrs);
        }

        // ---- notes (readNotes) ---------------------------------------------------
        void noteElement (const std::string& name, const XmlScanner::Attributes& attrs)
        {
            if (name == "DrumGroupDevice")
                track.drums = true;
            else if (rel == "/DeviceChain/Mixer/Speaker/Manual")
                track.muted = lowerTrimmed (attribute (attrs, "Value")) == "false";
            else if (rel.compare (0, kClip.size(), kClip) != 0)
                return;

            const auto sub = rel.substr (std::min (rel.size(), kClip.size()));
            double v = 0.0;
            if (sub == "/Loop/LoopStart" && parseDouble (attribute (attrs, "Value"), v))           content.loopStart = v;
            else if (sub == "/Loop/LoopEnd" && parseDouble (attribute (attrs, "Value"), v))        content.loopEnd = v;
            else if (sub == "/Loop/StartRelative" && parseDouble (attribute (attrs, "Value"), v))  content.startRelative = v;
            else if (sub == "/Loop/LoopOn")     content.loopOn = lowerTrimmed (attribute (attrs, "Value")) == "true";
            else if (sub == "/Disabled")        content.disabled = lowerTrimmed (attribute (attrs, "Value")) == "true";
            else if (sub == "/Notes/KeyTracks/KeyTrack")
            {
                keyNotes.clear();
                key = -1;
            }
            else if (sub == "/Notes/KeyTracks/KeyTrack/MidiKey")
            {
                if (parseDouble (attribute (attrs, "Value"), v))
                    key = static_cast<int> (v);
            }
            else if (sub == "/Notes/KeyTracks/KeyTrack/Notes/MidiNoteEvent")
            {
                if (lowerTrimmed (attribute (attrs, "IsEnabled")) == "false")
                    return;
                AlsNote n;
                double vel = 100.0;
                if (! parseDouble (attribute (attrs, "Time"), n.start) || ! parseDouble (attribute (attrs, "Duration"), n.length))
                    return;
                if (parseDouble (attribute (attrs, "Velocity"), vel))
                    n.velocity = static_cast<int> (vel + 0.5);
                keyNotes.push_back (n);
            }
        }

        void endKeyTrack()
        {
            if (key < 0)
                return;
            for (auto n : keyNotes)
            {
                n.pitch = key;
                content.notes.push_back (n);
            }
            keyNotes.clear();
        }

        // The clip's content notes, placed in the arrangement.
        void placeClip()
        {
            if (content.disabled || ! clipHasStart || ! clipHasEnd || clip.end <= clip.start)
                return;
            const double clipStart = clip.start, clipEnd = clip.end;
            const double markerStart = content.loopStart + content.startRelative;
            const double loopLength = content.loopEnd - content.loopStart;

            auto place = [&] (double contentFrom, double contentTo, double at)
            {
                for (const auto& n : content.notes)
                {
                    if (n.start < contentFrom - 1.0e-9 || n.start >= contentTo - 1.0e-9)
                        continue;
                    AlsNote out = n;
                    out.start = at + (n.start - contentFrom);
                    const double stop = std::min (clipEnd, at + (contentTo - contentFrom));
                    if (out.start >= clipEnd - 1.0e-9)
                        continue;
                    out.length = std::min (n.length, stop - out.start);
                    if (out.length > 1.0e-9)
                        track.notes.push_back (out);
                }
            };

            if (! content.loopOn || loopLength <= 1.0e-9)
            {
                place (markerStart, content.loopEnd > markerStart ? content.loopEnd : 1.0e12, clipStart);
                return;
            }
            // Looped: from the start marker to the loop end, then the loop again.
            double at = clipStart, from = markerStart;
            for (int pass = 0; at < clipEnd - 1.0e-9 && pass < 100000; ++pass)
            {
                if (from >= content.loopEnd - 1.0e-9)
                    from = content.loopStart;
                place (from, content.loopEnd, at);
                at += content.loopEnd - from;
                from = content.loopStart;
            }
        }

        void endElement (const std::string&) override
        {
            if (inTracks)
            {
                if (depth > 4)
                {
                    if (rel == kClip && clipHasStart && clipHasEnd && clip.end > clip.start)
                    {
                        track.clips.push_back (clip);
                        if (readNotes)
                            placeClip();
                    }
                    else if (readNotes && rel == kClip + "/Notes/KeyTracks/KeyTrack")
                        endKeyTrack();
                    rel.erase (rel.rfind ('/'));
                }
                else if (depth == 4)
                {
                    finishTrack();
                }
                else if (depth == 3)
                {
                    inTracks = false;
                    sawTracks = true;
                    finished = true;
                }
            }
            --depth;
        }

        bool done() const override { return finished; }

        AlsResult result() const
        {
            AlsResult r;
            r.trackCount = trackCount;

            const Track* chosen = nullptr;
            if (haveNamed)           chosen = &named;
            else if (haveSecond)     { chosen = &second; r.usedFallback = true; }

            if (chosen == nullptr)
            {
                if (! isAbleton)        r.error = "Not an Ableton Live set";
                else if (! sawTracks)   r.error = "The set is incomplete (no track list found)";
                else                    r.error = "No MIDI track named \"" + wanted + "\" and no 2nd track to fall back on";
                return r;
            }

            r.ok = true;
            r.midiTracks = midiTracks;
            r.trackName  = chosen->name();
            r.trackIndex = chosen->index;
            r.sections   = chosen->clips;
            std::stable_sort (r.sections.begin(), r.sections.end(),
                              [] (const AlsSection& a, const AlsSection& b) { return a.start < b.start; });
            return r;
        }

    private:
        struct Track
        {
            std::string type, userName, effectiveName;
            std::vector<AlsSection> clips;
            int index = -1;
            bool muted = false, drums = false;
            std::vector<AlsNote> notes;
            const std::string& name() const { return userName.empty() ? effectiveName : userName; }
        };

        struct Content
        {
            double loopStart = 0.0, loopEnd = 0.0, startRelative = 0.0;
            bool loopOn = false, disabled = false;
            std::vector<AlsNote> notes;   // in content time
        };

        void finishTrack()
        {
            track.index = trackCount++;
            if (readNotes && track.type == "MidiTrack")
            {
                AlsMidiTrack t;
                t.name = track.name();
                t.index = track.index;
                t.muted = track.muted;
                t.drums = track.drums;
                t.notes = std::move (track.notes);
                std::stable_sort (t.notes.begin(), t.notes.end(),
                                  [] (const AlsNote& a, const AlsNote& b) { return a.start < b.start; });
                midiTracks.push_back (std::move (t));
            }
            if (track.type == "MidiTrack" && lowerTrimmed (track.name()) == wanted && ! haveNamed)
            {
                named = track;
                haveNamed = true;
                finished = ! readNotes;   // without notes, nothing later in the file matters
            }
            else if (track.index == 1)
            {
                second = track;
                haveSecond = true;
            }
        }

        const std::string kClip     = "/DeviceChain/MainSequencer/ClipTimeable/ArrangerAutomation/Events/MidiClip";
        const std::string kClipEnd  = kClip + "/CurrentEnd";
        const std::string kClipName = kClip + "/Name";
        const std::string kClipColor = kClip + "/Color";

        std::string wanted;
        bool readNotes = false;
        int depth = 0;
        bool isAbleton = false, inLiveSet = false, inTracks = false, sawTracks = false, finished = false;

        std::string rel;   // element path below the current track
        Track track, named, second;
        bool haveNamed = false, haveSecond = false;
        int trackCount = 0;

        AlsSection clip;
        bool clipHasStart = false, clipHasEnd = false;

        Content content;
        std::vector<AlsNote> keyNotes;
        int key = -1;
        std::vector<AlsMidiTrack> midiTracks;
    };
}

// ---- XmlScanner ---------------------------------------------------------------

bool XmlScanner::feed (const char* data, std::size_t size)
{
    pending.append (data, size);

    const char* const base = pending.data();
    const char* const end  = base + pending.size();
    const char* p = base;

    auto findSeq = [end] (const char* from, const char* seq) -> const char*
    {
        const auto n = std::strlen (seq);
        const auto* hit = std::search (from, end, seq, seq + n);
        return hit == end ? nullptr : hit;
    };

    while (! handler.done())
    {
        const char* lt = static_cast<const char*> (std::memchr (p, '<', static_cast<std::size_t> (end - p)));
        if (lt == nullptr) { p = end; break; }
        p = lt;

        const auto avail = static_cast<std::size_t> (end - lt);
        const char* closing = nullptr;   // one past the construct
        if (avail >= 2 && lt[1] == '!')
        {
            if (avail < 9) break;        // can't tell comment / CDATA / DOCTYPE apart yet
            if (std::memcmp (lt, "<!--", 4) == 0)             { if (auto* e = findSeq (lt + 4, "-->")) closing = e + 3; }
            else if (std::memcmp (lt, "<![CDATA[", 9) == 0)   { if (auto* e = findSeq (lt + 9, "]]>")) closing = e + 3; }
            else                                              { if (auto* e = findSeq (lt + 2, ">"))   closing = e + 1; }
            if (closing == nullptr) break;
            p = closing;
            continue;
        }
        if (avail >= 2 && lt[1] == '?')
        {
            auto* e = findSeq (lt + 2, "?>");
            if (e == nullptr) break;
            p = e + 2;
            continue;
        }

        // An element tag: find its '>' outside attribute quotes.
        char quote = 0;
        const char* gt = nullptr;
        for (const char* q = lt + 1; q < end; ++q)
        {
            if (quote != 0)                     { if (*q == quote) quote = 0; }
            else if (*q == '"' || *q == '\'')   quote = *q;
            else if (*q == '>')                 { gt = q; break; }
        }
        if (gt == nullptr) break;

        if (! parseTag (lt + 1, gt))
            return false;
        p = gt + 1;
    }

    pending.erase (0, static_cast<std::size_t> (p - base));
    return true;
}

bool XmlScanner::parseTag (const char* p, const char* end)
{
    if (p == end)
        return false;

    if (*p == '/')
    {
        ++p;
        const char* e = p;
        while (e < end && ! isSpace (*e)) ++e;
        tagName.assign (p, e);
        handler.endElement (tagName);
        return true;
    }

    const char* e = p;
    while (e < end && ! isSpace (*e) && *e != '/') ++e;
    if (e == p)
        return false;
    tagName.assign (p, e);
    p = e;

    attrs.clear();
    bool selfClosing = false;
    for (;;)
    {
        while (p < end && isSpace (*p)) ++p;
        if (p == end) break;
        if (*p == '/') { selfClosing = true; break; }

        const char* nameEnd = p;
        while (nameEnd < end && *nameEnd != '=' && ! isSpace (*nameEnd)) ++nameEnd;
        const char* q = nameEnd;
        while (q < end && isSpace (*q)) ++q;
        if (q == end || *q != '=') return false;
        ++q;
        while (q < end && isSpace (*q)) ++q;
        if (q == end || (*q != '"' && *q != '\'')) return false;
        const char quote = *q++;
        const char* valueEnd = static_cast<const char*> (std::memchr (q, quote, static_cast<std::size_t> (end - q)));
        if (valueEnd == nullptr) return false;

        attrs.emplace_back (std::string (p, nameEnd), decodeEntities (q, valueEnd));
        p = valueEnd + 1;
    }

    handler.startElement (tagName, attrs);
    if (selfClosing)
        handler.endElement (tagName);
    return true;
}

// ---- entry points ---------------------------------------------------------------

AlsResult parseAlsXml (const std::string& xml, const std::string& trackName, bool readNotes)
{
    StructureHandler handler (trackName, readNotes);
    XmlScanner scanner (handler);
    if (! scanner.feed (xml.data(), xml.size()))
    {
        AlsResult r;
        r.error = "Malformed XML";
        return r;
    }
    return handler.result();
}

AlsResult parseAlsFile (const std::string& path, const std::string& trackName, bool readNotes)
{
    AlsResult failure;

    std::FILE* file = std::fopen (path.c_str(), "rb");
    if (file == nullptr)
    {
        failure.error = "Cannot open file";
        return failure;
    }

    StructureHandler handler (trackName, readNotes);
    XmlScanner scanner (handler);

    constexpr std::size_t kChunk = 1 << 16;
    std::vector<unsigned char> in (kChunk), out (kChunk * 4);

    std::size_t got = std::fread (in.data(), 1, kChunk, file);
    const bool gzipped = got >= 2 && in[0] == 0x1f && in[1] == 0x8b;
    bool bad = false;

    if (! gzipped)
    {
        while (got > 0 && ! scanner.finished())
        {
            if (! scanner.feed (reinterpret_cast<const char*> (in.data()), got)) { bad = true; break; }
            got = std::fread (in.data(), 1, kChunk, file);
        }
    }
    else
    {
        z_stream z;
        std::memset (&z, 0, sizeof (z));
        if (inflateInit2 (&z, 15 + 16) != Z_OK)
        {
            std::fclose (file);
            failure.error = "zlib failed to start";
            return failure;
        }

        bool streamEnd = false;
        while (got > 0 && ! streamEnd && ! bad && ! scanner.finished())
        {
            z.next_in  = in.data();
            z.avail_in = static_cast<uInt> (got);
            while (z.avail_in > 0 && ! scanner.finished())
            {
                z.next_out  = out.data();
                z.avail_out = static_cast<uInt> (out.size());
                const int rc = inflate (&z, Z_NO_FLUSH);
                if (rc != Z_OK && rc != Z_STREAM_END) { bad = true; break; }

                const auto produced = out.size() - z.avail_out;
                if (produced > 0 && ! scanner.feed (reinterpret_cast<const char*> (out.data()), produced))
                    { bad = true; break; }
                if (rc == Z_STREAM_END) { streamEnd = true; break; }
            }
            if (! streamEnd && ! bad)
                got = std::fread (in.data(), 1, kChunk, file);
        }
        inflateEnd (&z);
    }

    std::fclose (file);

    // A file caught halfway through a save ends early or fails to inflate. That
    // only matters if the structure track wasn't complete yet.
    if (bad && ! scanner.finished())
    {
        failure.error = gzipped ? "The file is damaged or still being written" : "Malformed XML";
        return failure;
    }
    return handler.result();
}

}  // namespace boothcue
