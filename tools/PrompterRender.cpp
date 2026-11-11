// prompter-render — draws the singer's display for a set of scenes through the
// REAL PrompterPainter into a PNG contact sheet, so layout changes can be
// eyeballed without a host. Also the `prompter-render` CTest: it fails if a
// scene can't be rendered or the PNG can't be written.
//
//   prompter-render [out.png] [single-scene.png]
//   prompter-render --set <set.als> [lyrics.txt] <out.png> [beat ...]
//       (default: previews/prompter-sheet.png; the second file, if given, gets
//        the first iPad-sized scene at its own size)

#include <juce_gui_basics/juce_gui_basics.h>

#include "AlsParser.h"
#include "Chords.h"
#include "LyricImport.h"
#include "SetChords.h"
#include "PrompterPainter.h"
#include "Theme.h"

using namespace boothcue;

namespace
{
    // An invented song, with the parts track and lyrics a session would have.
    Song demoSong()
    {
        Song song;
        song.setSections ({
            { "Intro",       0,  16 }, { "Verse 1",  16,  48 }, { "Pre-Chorus", 48,  64 },
            { "Chorus",     64,  96 }, { "Verse 2",  96, 128 }, { "Chorus",    128, 160 },
            { "Bridge",    160, 176 }, { "Chorus",  176, 208 }, { "Outro",     208, 224 },
        });
        song.setBlocks (parseLyrics (
            "[Verse 1]\n"
            "Streetlights hum a song I used to know\n"
            "Every window glowing soft and low\n"
            "I keep your number written on my hand\n"
            "In case the night forgets the plans we planned\n"
            "[Pre-Chorus]\n"
            "And if you call\n"
            "I'll be there before the echo fades away\n"
            "[Chorus]\n"
            "Hold on, hold on\n"
            "We're running out of reasons to be wrong\n"
            "Hold on, hold on\n"
            "The morning's never further than a song\n"
            "[Verse 2]\n"
            "(And) paper boats are sailing down the drain (down the drain)\n"
            "Carrying the things we couldn't say\n"
            "[Bridge]\n"
            "Oh-oh, oh-oh\n"));

        // Chords, one or two per bar, as the detector would give them.
        std::vector<ChordSpan> chords;
        const char* perBar[] = { "C", "G/B", "Am", "F",  "C", "G/B", "Am", "F",  "Am", "F", "C", "G",
                                 "F", "G", "C/E", "Am", "F", "G", "C", "C",  "Dm7", "G" };
        double t = 0.0;
        for (int bar = 0; bar < 56; ++bar)
        {
            ChordSpan c;
            c.start = t;
            c.end = t + 4.0;
            c.confidence = 0.8;
            parseChordName (perBar[bar % 22], c.chord);
            chords.push_back (c);
            t += 4.0;
        }
        song.setChords (chords, Key { 0, false });
        return song;
    }

    TransportState at (double ppq, bool playing = true, bool recording = false)
    {
        TransportState t;
        t.valid = true;
        t.playing = playing;
        t.recording = recording;
        t.ppq = ppq;
        t.bpm = 96.0;
        return t;
    }

    struct Scene
    {
        juce::String title;
        Song song;
        TransportState transport;
        DisplaySettings settings;
        std::string tapNote = {};
        int width = 0, height = 0;   // render size (0 = the tile); drawn scaled into the tile
        int maxCurrentLines = 0;     // > 0: the check fails if the current line takes more lines
    };

    DisplaySettings bassOn0() { return DisplaySettings(); }

    std::vector<Scene> scenes()
    {
        const auto song = demoSong();
        std::vector<Scene> out;
        DisplaySettings normal;
        normal.showChords = false;

        out.push_back ({ "Intro, counting in: 2 bars", song, at (4.5), normal });
        out.push_back ({ "Last bar before the entry: beat 3", song, at (13.2, true, true), normal });
        out.push_back ({ "Verse 1, recording", song, at (25.0, true, true), normal });

        DisplaySettings withChords;
        withChords.showChords = true;
        withChords.showBassNote = false;
        DisplaySettings noChords;
        noChords.showChords = false;
        // On a 10" iPad: ordinary lines must stay on one line.
        out.push_back ({ "iPad 10.9in (1180x820): long verse line", song, at (41.0, true, true), bassOn0(), {}, 1180, 820, 1 });
        out.push_back ({ "iPad 10.2in (1024x768): chorus, chords", song, at (74.0, true, true), withChords, {}, 1024, 768, 1 });

        out.push_back ({ "Chords + lyrics (bass in the name)", song, at (26.0, true, true), withChords });
        DisplaySettings bassOn;
        out.push_back ({ "Chords + lyrics + bass note", song, at (26.0, true, true), bassOn });
        DisplaySettings playerMode;
        playerMode.showLyrics = false;
        out.push_back ({ "Keys / guitar: lyrics off, chords + bass", song, at (27.0), playerMode });

        auto looped = at (70.0, true, true);
        looped.looping = true;
        looped.loopStart = 64.0;
        looped.loopEnd = 96.0;
        looped.loopPass = 3;
        out.push_back ({ "Chorus, looping: pass 3", song, looped, normal });

        out.push_back ({ "Pre-chorus, long line wraps", song, at (58.0), normal });
        out.push_back ({ "Just tapped (note along the bottom, 1.5 s)", song, at (33.0), normal,
                         "\"Every window glowing soft and low\" at 9.2.1" });

        DisplaySettings noLyrics;
        noLyrics.showLyrics = false;
        noLyrics.showChords = false;
        out.push_back ({ "Pickups and backing in parentheses, dimmer", song, at (100.0), normal });
        out.push_back ({ "Lyrics off: structure only (players)", song, at (100.0), noLyrics });

        DisplaySettings big;
        big.fontScale = 1.6f;
        big.showChords = false;
        out.push_back ({ "Text size 160%, stopped", song, at (130.0, false), big });

        Song structureOnly;
        structureOnly.setSections (song.sections());
        out.push_back ({ "Set linked, no lyrics yet", structureOnly, at (20.0), normal });

        out.push_back ({ "Nothing linked yet", Song(), at (20.0), normal });
        out.push_back ({ "No host transport (Standalone)", song, TransportState(), normal });
        return out;
    }

    int failures = 0;

    juce::Image render (const Scene& scene, int w, int h)
    {
        juce::Image image (juce::Image::RGB, w, h, true);
        juce::Graphics g (image);
        PrompterPainter painter;
        auto built = buildScene (scene.song, scene.transport, scene.settings);
        built.tapNote = scene.tapNote;
        painter.paint (g, { 0, 0, w, h }, built, scene.song, scene.settings);
        if (scene.maxCurrentLines > 0 && painter.lastCurrentLineCount() > scene.maxCurrentLines)
        {
            std::printf ("FAIL: \"%s\": the current line took %d lines\n", scene.title.toRawUTF8(),
                         painter.lastCurrentLineCount());
            ++failures;
        }
        return image;
    }

    bool writePng (const juce::Image& image, const juce::File& file)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        return out.openedOk() && png.writeImageToStream (image, out);
    }
}

// --set <set.als> [lyrics.txt] <out.png> [beat ...]: a real Live set (its
// sections, the chords detected from its MIDI, the lyrics) at a few song
// positions, at iPad 10.9" size, as a 2-column sheet.
int renderSet (int argc, char* argv[])
{
    std::vector<std::string> args (argv + 2, argv + argc);
    if (args.size() < 2)
        return 2;
    const auto set = parseAlsFile (args[0], "parts", true);
    if (! set.ok)
    {
        std::printf ("prompter-render: %s\n", set.error.c_str());
        return 1;
    }
    std::size_t next = 1;
    std::string lyrics;
    if (args[1].size() > 4 && args[1].substr (args[1].size() - 4) == ".txt")
    {
        lyrics = juce::File (juce::String::fromUTF8 (args[1].c_str())).loadFileAsString().toStdString();
        ++next;
    }
    const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (args[next].c_str()));
    std::vector<double> beats;
    for (auto i = next + 1; i < args.size(); ++i)
        beats.push_back (std::atof (args[i].c_str()));

    Song song;
    std::vector<Section> sections;
    for (const auto& sec : set.sections)
        sections.push_back ({ sec.name, sec.start, sec.end, sec.color });
    song.setSections (sections);
    song.setBarLength (4.0);
    song.setBlocks (parseLyrics (lyrics));
    const auto chords = chordsFromSet (set, 4.0);
    song.setChords (chords.result.spans, chords.result.key);
    if (beats.empty())   // a bar into each of the first sections
        for (std::size_t i = 0; i < song.sections().size() && beats.size() < 6; ++i)
            beats.push_back (song.sections()[i].start + 6.0);

    constexpr int w = 1180, h = 820, tileW = 944, tileH = 656, pad = 16, label = 34;
    const int rows = (static_cast<int> (beats.size()) + 1) / 2;
    juce::Image sheet (juce::Image::RGB, pad + 2 * (tileW + pad), pad + rows * (label + tileH + pad), true);
    juce::Graphics g (sheet);
    g.fillAll (juce::Colour (0xff1e2228));
    for (std::size_t i = 0; i < beats.size(); ++i)
    {
        TransportState t;
        t.valid = t.playing = true;
        t.ppq = beats[i];
        t.bpm = 120.0;
        DisplaySettings settings;
        juce::Image image (juce::Image::RGB, w, h, true);
        {
            juce::Graphics ig (image);
            PrompterPainter painter;
            painter.paint (ig, { 0, 0, w, h }, buildScene (song, t, settings), song, settings);
        }
        const int x = pad + static_cast<int> (i % 2) * (tileW + pad), y = pad + static_cast<int> (i / 2) * (label + tileH + pad);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("beat " + juce::String (beats[i], 0), x, y, tileW, label - 6, juce::Justification::bottomLeft);
        g.drawImageWithin (image, x, y + label, tileW, tileH, juce::RectanglePlacement::centred);
    }
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (sheet, stream))
        return 1;
    std::printf ("prompter-render: wrote %s (%zu positions, key %s, %zu chords)\n", out.getFullPathName().toRawUTF8(),
                 beats.size(), keyName (chords.result.key).c_str(), chords.result.spans.size());
    return 0;
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

    if (argc > 1 && std::string (argv[1]) == "--set")
        return renderSet (argc, argv);

    const auto outFile = juce::File::getCurrentWorkingDirectory().getChildFile (
        argc > 1 ? argv[1] : "previews/prompter-sheet.png");

    const auto all = scenes();
    constexpr int tileW = 960, tileH = 540, label = 34, pad = 16, columns = 2;
    const int rows = (static_cast<int> (all.size()) + columns - 1) / columns;

    juce::Image sheet (juce::Image::RGB, pad + columns * (tileW + pad), pad + rows * (label + tileH + pad), true);
    {
        juce::Graphics g (sheet);
        g.fillAll (juce::Colour (0xff1e2228));

        for (std::size_t i = 0; i < all.size(); ++i)
        {
            const int col = static_cast<int> (i) % columns, row = static_cast<int> (i) / columns;
            const int x = pad + col * (tileW + pad), y = pad + row * (label + tileH + pad);

            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.drawText (all[i].title, x, y, tileW, label - 6, juce::Justification::bottomLeft);
            const auto& sc = all[static_cast<std::size_t> (i)];
            if (sc.width > 0 && sc.height > 0)
            {
                // A device size: rendered at that size, shown scaled in the tile.
                const auto image = render (sc, sc.width, sc.height);
                g.setColour (juce::Colour (0xff101214));
                g.fillRect (x, y + label, tileW, tileH);
                g.drawImageWithin (image, x, y + label, tileW, tileH, juce::RectanglePlacement::centred);
            }
            else
            {
                g.drawImageAt (render (sc, tileW, tileH), x, y + label);
            }
        }
    }

    if (! writePng (sheet, outFile))
    {
        std::printf ("prompter-render: could not write %s\n", outFile.getFullPathName().toRawUTF8());
        return 1;
    }
    std::printf ("prompter-render: wrote %s (%d scenes)\n", outFile.getFullPathName().toRawUTF8(),
                 static_cast<int> (all.size()));
    if (failures > 0)
        return 1;

    if (argc > 2)
    {
        const auto single = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
        // The first device-sized scene at its own size (else a 1920x1080 one).
        const Scene* pick = &all[2];
        for (const auto& sc : all)
            if (sc.width > 0) { pick = &sc; break; }
        if (! writePng (render (*pick, pick->width > 0 ? pick->width : 1920, pick->height > 0 ? pick->height : 1080), single))
            return 1;
        std::printf ("prompter-render: wrote %s\n", single.getFullPathName().toRawUTF8());
    }
    return 0;
}
