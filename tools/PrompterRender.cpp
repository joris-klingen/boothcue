// prompter-render — draws the singer's display for a set of scenes through the
// REAL PrompterPainter into a PNG contact sheet, so layout changes can be
// eyeballed without a host. Also the `prompter-render` CTest: it fails if a
// scene can't be rendered or the PNG can't be written.
//
//   prompter-render [out.png] [single-scene.png]
//       (default: previews/prompter-sheet.png; the second file, if given, gets
//        one scene at 1920x1080)

#include <juce_gui_basics/juce_gui_basics.h>

#include "Chords.h"
#include "LyricImport.h"
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
    };

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

    juce::Image render (const Scene& scene, int w, int h)
    {
        juce::Image image (juce::Image::RGB, w, h, true);
        juce::Graphics g (image);
        PrompterPainter painter;
        auto built = buildScene (scene.song, scene.transport, scene.settings);
        built.tapNote = scene.tapNote;
        painter.paint (g, { 0, 0, w, h }, built, scene.song, scene.settings);
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

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI gui;

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
            g.drawImageAt (render (all[i], tileW, tileH), x, y + label);
        }
    }

    if (! writePng (sheet, outFile))
    {
        std::printf ("prompter-render: could not write %s\n", outFile.getFullPathName().toRawUTF8());
        return 1;
    }
    std::printf ("prompter-render: wrote %s (%d scenes)\n", outFile.getFullPathName().toRawUTF8(),
                 static_cast<int> (all.size()));

    if (argc > 2)
    {
        const auto single = juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]);
        if (! writePng (render (all[2], 1920, 1080), single))
            return 1;
        std::printf ("prompter-render: wrote %s\n", single.getFullPathName().toRawUTF8());
    }
    return 0;
}
