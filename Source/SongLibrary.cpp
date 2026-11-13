#include "SongLibrary.h"

#include <algorithm>
#include <cctype>
#include <tuple>

namespace boothcue
{

namespace
{
    constexpr int kMaxRecents = 10;

    juce::File dirFrom (const juce::String& path)
    {
        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
            return {};
        const juce::File f (path);
        return f.isDirectory() ? f : juce::File();
    }

    juce::File fileFrom (const juce::var& v)
    {
        const auto path = v.toString();
        return path.isNotEmpty() && juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    }

    juce::String forward (const juce::File& f)   { return f.getFullPathName().replaceCharacter ('\\', '/'); }

    // "papa is even weg Project" -> "papa is even weg"
    juce::String projectName (const juce::File& folder)
    {
        const auto name = folder.getFileName();
        return name.endsWithIgnoreCase (" Project") ? name.dropLastCharacters (8) : name;
    }
}

juce::File SongLibrary::defaultSettingsFile()
{
    const auto named = juce::SystemStats::getEnvironmentVariable ("BOOTHCUE_LIBRARY", {});
    if (named.isNotEmpty() && juce::File::isAbsolutePath (named))
        return juce::File (named);
    auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile ("Application Support");
   #endif
    return dir.getChildFile ("BoothCue").getChildFile ("library.json");
}

SongLibrary::SongLibrary (juce::File settingsFile) : file (std::move (settingsFile)) {}

juce::var SongLibrary::load() const
{
    auto v = file.existsAsFile() ? juce::JSON::parse (file.loadFileAsString()) : juce::var();
    if (v.getDynamicObject() == nullptr)
        v = juce::var (new juce::DynamicObject());
    return v;
}

void SongLibrary::save (const juce::var& v) const
{
    const auto text = juce::JSON::toString (v);
    if (file.existsAsFile() && file.loadFileAsString() == text)
        return;
    file.getParentDirectory().createDirectory();
    file.replaceWithText (text, false, false, "\n");
}

juce::File SongLibrary::songbookRoot() const
{
    if (const auto saved = dirFrom (load()["songbook"].toString()); saved != juce::File())
        return saved;
    return dirFrom (juce::SystemStats::getEnvironmentVariable ("BBB_SONGBOOK", {}));
}

void SongLibrary::setSongbookRoot (const juce::File& root)
{
    auto v = load();
    v.getDynamicObject()->setProperty ("songbook", root.getFullPathName());
    save (v);
}

juce::File SongLibrary::projectsRoot() const
{
    if (const auto saved = dirFrom (load()["projects"].toString()); saved != juce::File())
        return saved;
    return dirFrom (juce::SystemStats::getEnvironmentVariable ("BBB_ROOT", {}));
}

LibrarySong SongLibrary::songIn (const juce::File& folder)
{
    LibrarySong s;
    s.folder = folder;
    s.id = folder.getFileName();
    s.title = s.id;
    const auto json = folder.getChildFile ("song.json");
    if (json.existsAsFile())
    {
        const auto v = juce::JSON::parse (json.loadFileAsString());
        if (const auto title = v["title"].toString().trim(); title.isNotEmpty())
            s.title = title;
        s.project = v["project"].toString().trim().replaceCharacter ('\\', '/');
        while (s.project.endsWithChar ('/'))
            s.project = s.project.dropLastCharacters (1);
    }
    return s;
}

std::vector<LibrarySong> SongLibrary::songs() const
{
    std::vector<LibrarySong> out;
    const auto root = songbookRoot();
    if (root == juce::File())
        return out;
    for (const auto& entry : juce::RangedDirectoryIterator (root.getChildFile ("songs"), false, "*", juce::File::findDirectories))
        if (entry.getFile().getChildFile ("song.json").existsAsFile())
            out.push_back (songIn (entry.getFile()));
    std::sort (out.begin(), out.end(), [] (const LibrarySong& a, const LibrarySong& b)
               { return a.title.compareNatural (b.title) < 0; });
    return out;
}

juce::String SongLibrary::slug (const juce::String& title)
{
    juce::String out;
    bool dash = false;
    for (auto p = title.getCharPointer(); ! p.isEmpty(); ++p)
    {
        const auto c = juce::CharacterFunctions::toLowerCase (*p);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            if (dash && out.isNotEmpty())
                out << '-';
            dash = false;
            out << juce::String::charToString (c);
        }
        else
            dash = true;
    }
    return out;
}

int SongLibrary::songForSet (const std::vector<LibrarySong>& songs, const juce::File& set)
{
    if (set == juce::File())
        return -1;
    const auto folder = set.getParentDirectory();
    const auto path = forward (folder);
    for (std::size_t i = 0; i < songs.size(); ++i)
    {
        const auto& project = songs[i].project;
        if (project.isEmpty())
            continue;
        if (juce::File::isAbsolutePath (project))
        {
            // A full path: the one set the song names, or its project folder.
            const juce::File named (project);
            if (project.endsWithIgnoreCase (".als") ? named == set : named == folder)
                return static_cast<int> (i);
        }
        else if ((path + "/").endsWithIgnoreCase ("/" + project + "/"))
            return static_cast<int> (i);
    }
    const auto name = slug (projectName (folder));
    if (name.isNotEmpty())
        for (std::size_t i = 0; i < songs.size(); ++i)
            if (songs[i].id == name || slug (songs[i].title) == name)
                return static_cast<int> (i);
    return -1;
}

juce::File SongLibrary::setForSong (const LibrarySong& song) const
{
    // song.json names one set by its full path: that one, whatever is newer.
    if (juce::File::isAbsolutePath (song.project) && song.project.endsWithIgnoreCase (".als"))
        return juce::File (song.project).existsAsFile() ? juce::File (song.project) : juce::File();

    if (juce::File::isAbsolutePath (song.project))   // a project folder by its full path: its latest version
        return latestSet (juce::File (song.project));

    const auto root = projectsRoot();
    if (root == juce::File())
        return {};

    juce::File project;
    if (song.project.isNotEmpty())
        project = root.getChildFile (song.project);
    else   // not set in song.json: a project two folders down, named like the song
        for (const auto& group : juce::RangedDirectoryIterator (root, false, "*", juce::File::findDirectories))
            for (const auto& p : juce::RangedDirectoryIterator (group.getFile(), false, "*", juce::File::findDirectories))
                if (const auto name = slug (projectName (p.getFile())); name == song.id || name == slug (song.title))
                    project = p.getFile();
    if (! project.isDirectory())
        return {};

    return latestSet (project);
}

// "papa is even weg 3.3 (test)" -> 3.3 as { 3, 3 }; no such number: { -1, -1 }.
static std::pair<int, int> versionOf (const juce::String& name)
{
    std::pair<int, int> found { -1, -1 };
    const auto s = name.toStdString();
    for (std::size_t i = 0; i < s.size();)
    {
        if (! std::isdigit (static_cast<unsigned char> (s[i]))) { ++i; continue; }
        std::size_t a = i;
        while (i < s.size() && std::isdigit (static_cast<unsigned char> (s[i]))) ++i;
        if (i + 1 < s.size() && s[i] == '.' && std::isdigit (static_cast<unsigned char> (s[i + 1])))
        {
            std::size_t b = i + 1, e = b;
            while (e < s.size() && std::isdigit (static_cast<unsigned char> (s[e]))) ++e;
            if (i - a <= 6 && e - b <= 6)
                found = { std::stoi (s.substr (a, i - a)), std::stoi (s.substr (b, e - b)) };
            i = b;   // "3.3.1": the last pair counts, as in the songbook
        }
    }
    return found;
}

juce::File SongLibrary::latestSet (const juce::File& project)
{
    // By the number in the name, not by the save time: opening an old version
    // and saving it does not make it the latest. The same number twice: the
    // plain name first ("3.3" before "3.3 (test)"); no number: last, newest save first.
    juce::File best;
    auto key = [] (const juce::File& f)
    {
        const auto v = versionOf (f.getFileNameWithoutExtension());
        return std::make_tuple (v.first >= 0, v, -f.getFileName().length(), f.getLastModificationTime().toMilliseconds());
    };
    for (const auto& entry : juce::RangedDirectoryIterator (project, false, "*.als", juce::File::findFiles))
        if (best == juce::File() || key (entry.getFile()) > key (best))
            best = entry.getFile();
    return best;
}

void SongLibrary::learn (const juce::File& set, const juce::File& songFolder)
{
    if (songFolder == juce::File() || ! songFolder.getChildFile ("song.json").existsAsFile())
        return;
    auto v = load();
    auto* o = v.getDynamicObject();

    // <songbook>/songs/<id>
    if (songFolder.getParentDirectory().getFileName() == "songs")
        o->setProperty ("songbook", songFolder.getParentDirectory().getParentDirectory().getFullPathName());

    // The set's folder ends with the song's project path: what is in front of
    // it is the projects folder.
    const auto song = songIn (songFolder);
    if (set != juce::File() && song.project.isNotEmpty() && ! juce::File::isAbsolutePath (song.project))
    {
        const auto path = forward (set.getParentDirectory());
        if ((path + "/").endsWithIgnoreCase ("/" + song.project + "/"))
            o->setProperty ("projects", juce::File (path.dropLastCharacters (song.project.length() + 1)).getFullPathName());
    }
    save (v);
}

std::vector<SongLibrary::Recent> SongLibrary::recents() const
{
    std::vector<Recent> out;
    const auto v = load();   // kept: the array lives in it
    if (const auto* list = v["recent"].getArray())
        for (const auto& r : *list)
        {
            Recent recent { fileFrom (r["set"]), fileFrom (r["song"]) };
            if (recent.set != juce::File() || recent.songFolder != juce::File())
                out.push_back (recent);
        }
    return out;
}

void SongLibrary::remember (const juce::File& set, const juce::File& songFolder)
{
    if (set == juce::File() && songFolder == juce::File())
        return;
    auto list = recents();
    // One entry per song: the same set, or the same song folder, is replaced.
    list.erase (std::remove_if (list.begin(), list.end(), [&] (const Recent& r)
                                {
                                    return (set != juce::File() && r.set == set)
                                        || (songFolder != juce::File() && r.songFolder == songFolder);
                                }),
                list.end());
    list.insert (list.begin(), Recent { set, songFolder });
    if (list.size() > static_cast<std::size_t> (kMaxRecents))
        list.resize (static_cast<std::size_t> (kMaxRecents));

    juce::Array<juce::var> arr;
    for (const auto& r : list)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("set", r.set.getFullPathName());
        o->setProperty ("song", r.songFolder.getFullPathName());
        arr.add (juce::var (o));
    }
    auto v = load();
    v.getDynamicObject()->setProperty ("recent", arr);
    save (v);
}

}  // namespace boothcue
