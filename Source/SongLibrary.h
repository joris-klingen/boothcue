#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace boothcue
{

// A song in the band's songbook (bovenburen_manager): its folder holds
// song.json, lyrics.txt, chords.txt and the timings BoothCue writes.
struct LibrarySong
{
    juce::String id;        // the folder's name
    juce::String title;     // from song.json (the id when it has none)
    juce::String project;   // its Live project: a folder from the projects folder, or the full path
                            // of a folder (the newest set in it) or of one .als ("" = not set)
    juce::File folder;
};

// What BoothCue knows about this computer, kept outside any Live set so every
// set shares it: where the songbook and the Live projects are, and the songs
// linked last. With it, one choice links a song: picking the Live set finds
// the song's folder in the songbook, picking the song finds its newest set.
//
// Read and written on the message thread, as one small JSON file. Nothing here
// writes to the songbook or to a Live project.
class SongLibrary
{
public:
    // The settings file: BoothCue/library.json in the user's application data
    // (BOOTHCUE_LIBRARY names another file: the tests use their own).
    static juce::File defaultSettingsFile();
    explicit SongLibrary (juce::File settingsFile = defaultSettingsFile());

    // The songbook folder (the one that holds songs/): chosen, learned from a
    // linked song folder, or BBB_SONGBOOK. A folder that is not there: none.
    juce::File songbookRoot() const;
    void setSongbookRoot (const juce::File&);
    // The folder the songs' `project` paths start from: learned from a set
    // linked next to its song, or BBB_ROOT.
    juce::File projectsRoot() const;

    // The songbook's songs, by title.
    std::vector<LibrarySong> songs() const;
    // The song a folder holds (title = the folder's name without a song.json).
    static LibrarySong songIn (const juce::File& folder);

    // The song a Live set belongs to: the one whose project is the set's
    // folder, else the one named like that folder. -1: none.
    static int songForSet (const std::vector<LibrarySong>&, const juce::File& set);
    // The newest .als in the song's Live project; none when the project (or
    // the projects folder) isn't known.
    juce::File setForSong (const LibrarySong&) const;

    // A set and a song folder linked together: remembers where the songbook
    // and the projects are, from the two of them.
    void learn (const juce::File& set, const juce::File& songFolder);

    // The songs linked last, newest first (either file may be missing).
    struct Recent
    {
        juce::File set, songFolder;
    };
    std::vector<Recent> recents() const;
    void remember (const juce::File& set, const juce::File& songFolder);

    // "Papa is even weg" -> "papa-is-even-weg", as the songbook names folders.
    static juce::String slug (const juce::String&);

private:
    juce::var load() const;
    void save (const juce::var&) const;
    juce::File file;
};

}  // namespace boothcue
