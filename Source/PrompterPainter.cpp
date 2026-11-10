#include "PrompterPainter.h"

#include "Theme.h"

namespace boothcue
{

namespace
{
    float unitFor (juce::Rectangle<int> bounds, const DisplaySettings& settings)
    {
        const float base = juce::jmin ((float) bounds.getHeight() / 100.0f, (float) bounds.getWidth() / 160.0f);
        return base * juce::jlimit (0.5f, 2.0f, settings.fontScale);
    }

    juce::Font font (float height, bool bold = false)
    {
        return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
    }

    juce::String str (const std::string& s)   { return juce::String::fromUTF8 (s.c_str()); }

    juce::String sectionName (const Song& song, int index)
    {
        if (index < 0)
            return {};
        const auto& name = song.sections()[static_cast<std::size_t> (index)].name;
        return name.empty() ? juce::String ("(unnamed)") : str (name);
    }

    juce::TextLayout layoutFor (const juce::String& text, float fontHeight, bool bold, juce::Colour colour, float width)
    {
        juce::AttributedString as;
        as.setJustification (juce::Justification::centred);
        as.setWordWrap (juce::AttributedString::byWord);

        // Words in parentheses (pickups, backing vocals, echoes) are drawn
        // dimmer than the line itself.
        const auto f = font (fontHeight, bold);
        const auto aside = colour.withMultipliedAlpha (0.55f);
        juce::String run;
        int depth = 0;
        for (auto p = text.getCharPointer(); ! p.isEmpty(); ++p)
        {
            const auto c = *p;
            if (c == '(' && depth++ == 0 && run.isNotEmpty())
            {
                as.append (run, f, colour);
                run.clear();
            }
            run += juce::String::charToString (c);
            if (c == ')' && depth > 0 && --depth == 0)
            {
                as.append (run, f, aside);
                run.clear();
            }
        }
        if (run.isNotEmpty())
            as.append (run, f, depth > 0 ? aside : colour);
        juce::TextLayout layout;
        layout.createLayout (as, width);
        return layout;
    }

    // Header and timeline heights, in units.
    constexpr float kHeaderUnits   = 11.0f;
    constexpr float kTimelineUnits = 6.0f;
    constexpr float kGapUnits      = 2.0f;
    constexpr float kMarginUnits   = 3.0f;

    juce::Rectangle<float> timelineArea (juce::Rectangle<int> bounds, float unit)
    {
        auto area = bounds.toFloat().reduced (kMarginUnits * unit);
        area.removeFromTop ((kHeaderUnits + kGapUnits) * unit);
        return area.removeFromTop (kTimelineUnits * unit);
    }

    float xFor (double ppq, juce::Rectangle<float> area, const PrompterScene& s)
    {
        const double span = juce::jmax (1.0e-9, s.timelineEnd - s.timelineStart);
        const double t = juce::jlimit (0.0, 1.0, (ppq - s.timelineStart) / span);
        return area.getX() + (float) t * area.getWidth();
    }
}

// ---- PrompterPainter ----------------------------------------------------------------

int PrompterPainter::playheadX (juce::Rectangle<int> bounds, const PrompterScene& s, const DisplaySettings& settings)
{
    if (! settings.showSongPart || ! s.hasSong || ! s.hasPosition)
        return -1;
    return juce::roundToInt (xFor (s.ppq, timelineArea (bounds, unitFor (bounds, settings)), s));
}

void PrompterPainter::paint (juce::Graphics& g, juce::Rectangle<int> bounds,
                             const PrompterScene& s, const Song& song, const DisplaySettings& settings)
{
    g.fillAll (theme::background);

    const float unit = unitFor (bounds, settings);
    auto area = bounds.toFloat().reduced (kMarginUnits * unit);

    if (settings.showSongPart)
    {
        paintHeader (g, area.removeFromTop (kHeaderUnits * unit), unit, s, song);
        area.removeFromTop (kGapUnits * unit);
        paintTimeline (g, area.removeFromTop (kTimelineUnits * unit), unit, s, song);
        area.removeFromTop (kGapUnits * 2.0f * unit);
    }

    if (! s.tapNote.empty())
    {
        auto note = area.removeFromBottom (3.6f * unit);
        g.setColour (s.tapNoteOk ? theme::dim : theme::loop);
        g.setFont (font (2.6f * unit));
        g.drawText ((s.tapNoteOk ? "tap: " : "not tapped: ") + str (s.tapNote), note,
                    juce::Justification::centredRight, true);
    }

    if (! s.hasPosition)
    {
        g.setColour (theme::dim);
        g.setFont (font (4.0f * unit));
        g.drawFittedText ("Waiting for the host transport", area.toNearestInt(), juce::Justification::centred, 2);
        return;
    }

    const bool chords = settings.showChords && s.hasChords;
    if (settings.showLyrics)
    {
        if (chords)
        {
            paintChords (g, area.removeFromTop (16.0f * unit), unit, s, false);
            area.removeFromTop (2.0f * unit);
        }
        paintLyrics (g, area, unit, s);
    }
    else if (chords)
    {
        paintChords (g, area, unit, s, true);   // for keys / guitar: chords fill the screen
    }
    else if (settings.showSongPart)
    {
        paintStructureOnly (g, area, unit, s, song);
    }
}

void PrompterPainter::paintChords (juce::Graphics& g, juce::Rectangle<float> area, float unit,
                                   const PrompterScene& s, bool big)
{
    const float scale = big ? 1.8f : 1.0f;
    if (big)   // one row in the middle, not spread over the whole height
        area = area.withSizeKeepingCentre (area.getWidth(), juce::jmin (area.getHeight(), 34.0f * unit));
    auto prev = area.removeFromLeft (area.getWidth() * 0.22f);
    auto next = area.removeFromRight (area.getWidth() * 0.30f);
    auto now = area;

    if (! s.prevChord.empty())
    {
        g.setColour (theme::faint.brighter (0.4f));
        g.setFont (font (5.0f * unit * scale));
        g.drawFittedText (str (s.prevChord), prev.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }

    // Current: as large as fits; the bass note under it.
    auto bassArea = s.bassNote.empty() ? juce::Rectangle<float>() : now.removeFromBottom (now.getHeight() * 0.28f);
    const auto name = s.chord.empty() ? juce::String ("-") : str (s.chord) + (s.chordUnsure ? "?" : "");
    g.setColour (s.chord.empty() ? theme::dim : theme::count);
    g.setFont (font (juce::jmin (now.getHeight() * 0.95f, 12.0f * unit * scale), true));
    g.drawFittedText (name, now.toNearestInt(), juce::Justification::centred, 1, 0.6f);
    if (! bassArea.isEmpty())
    {
        g.setColour (theme::sectionNow.brighter (0.3f));
        g.setFont (font (juce::jmin (bassArea.getHeight() * 0.9f, 4.5f * unit * scale), true));
        g.drawFittedText ("bass " + str (s.bassNote), bassArea.toNearestInt(), juce::Justification::centredTop, 1, 0.8f);
    }

    if (! s.nextChord.empty())
    {
        auto label = next.removeFromBottom (next.getHeight() * 0.32f);
        g.setColour (theme::text.withAlpha (0.8f));
        g.setFont (font (6.5f * unit * scale, true));
        g.drawFittedText (str (s.nextChord), next.toNearestInt(), juce::Justification::centredRight, 1, 0.7f);
        g.setColour (theme::dim);
        g.setFont (font (2.8f * unit * scale));
        g.drawFittedText ("in " + str (describeDistance (s.beatsToNextChord, s.sig)), label.toNearestInt(),
                          juce::Justification::centredRight, 1, 0.8f);
    }
}

void PrompterPainter::paintHeader (juce::Graphics& g, juce::Rectangle<float> area, float unit,
                                   const PrompterScene& s, const Song& song)
{
    // Right: transport state, then bar.beat.
    auto right = area.removeFromRight (34.0f * unit);
    if (s.hasPosition)
    {
        auto top = right.removeFromTop (7.0f * unit);
        g.setColour (theme::text);
        g.setFont (font (7.0f * unit, true));
        g.drawText (juce::String (s.barBeat.bar) + "." + juce::String (s.barBeat.beat), top,
                    juce::Justification::centredRight, false);

        // State pill under it: REC beats PLAY beats STOP; "Pass n" while looping.
        auto row = right.removeFromTop (4.0f * unit).withTrimmedTop (0.6f * unit);
        g.setFont (font (2.8f * unit, true));

        if (s.loopPass > 0)
        {
            const auto text = "PASS " + juce::String (s.loopPass);
            const auto w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 2.0f * unit;
            auto pill = row.removeFromRight (w);
            g.setColour (theme::loop);
            g.fillRoundedRectangle (pill, 0.6f * unit);
            g.setColour (theme::background);
            g.drawText (text, pill, juce::Justification::centred, false);
            row.removeFromRight (1.2f * unit);
        }

        const auto stateColour = s.recording ? theme::record : s.playing ? theme::play : theme::dim;
        const auto stateText   = s.recording ? "REC" : s.playing ? "PLAY" : "STOP";
        g.setColour (stateColour);
        const auto w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), stateText);
        auto label = row.removeFromRight (w);
        g.drawText (stateText, label, juce::Justification::centredRight, false);
        row.removeFromRight (0.8f * unit);
        const auto dot = row.removeFromRight (2.0f * unit).withSizeKeepingCentre (1.8f * unit, 1.8f * unit);
        if (s.recording)
            g.fillEllipse (dot);
        else if (s.playing)
        {
            juce::Path tri;
            tri.addTriangle (dot.getX(), dot.getY(), dot.getX(), dot.getBottom(), dot.getRight(), dot.getCentreY());
            g.fillPath (tri);
        }
        else
            g.fillRect (dot.reduced (0.15f * unit));
    }

    // Left: the section we're in, then what comes next.
    area.removeFromRight (2.0f * unit);
    auto nameRow = area.removeFromTop (7.0f * unit);

    juce::String name;
    auto colour = theme::text;
    if (! s.hasSong)              { name = "No set linked"; colour = theme::dim; }
    else if (s.section >= 0)      name = sectionName (song, s.section);
    else if (s.hasPosition && s.ppq < song.start())  { name = "Before the song"; colour = theme::dim; }
    else if (s.hasPosition && s.nextSection >= 0)    { name = "Gap"; colour = theme::dim; }
    else if (s.hasPosition)       { name = "End"; colour = theme::dim; }

    g.setColour (colour);
    g.setFont (font (6.5f * unit, true));
    g.drawFittedText (name, nameRow.toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);

    if (s.hasSong && s.hasPosition && s.nextSection >= 0)
    {
        g.setColour (theme::dim);
        g.setFont (font (3.4f * unit));
        g.drawFittedText ("next: " + sectionName (song, s.nextSection) + "  in "
                              + str (describeDistance (s.beatsToNextSection, s.sig)),
                          area.removeFromTop (4.0f * unit).toNearestInt(), juce::Justification::centredLeft, 1, 0.7f);
    }
}

void PrompterPainter::paintTimeline (juce::Graphics& g, juce::Rectangle<float> area, float unit,
                                     const PrompterScene& s, const Song& song)
{
    if (! s.hasSong)
    {
        g.setColour (theme::sectionPast);
        g.fillRoundedRectangle (area, 0.6f * unit);
        g.setColour (theme::dim);
        g.setFont (font (2.6f * unit));
        g.drawFittedText ("Link a Live set in the BoothCue plugin window", area.toNearestInt(),
                          juce::Justification::centred, 1);
        return;
    }

    const float gap = juce::jmax (1.0f, 0.25f * unit);
    g.setFont (font (2.5f * unit, true));

    const auto& sections = song.sections();
    for (std::size_t i = 0; i < sections.size(); ++i)
    {
        const auto& sec = sections[i];
        const float x0 = xFor (sec.start, area, s);
        const float x1 = xFor (sec.end, area, s);
        auto block = juce::Rectangle<float> (x0, area.getY(), juce::jmax (1.0f, x1 - x0 - gap), area.getHeight());

        const bool now  = static_cast<int> (i) == s.section;
        const bool past = s.hasPosition && sec.end <= s.ppq;
        g.setColour (now ? theme::sectionNow : past ? theme::sectionPast : theme::sectionFuture);
        g.fillRoundedRectangle (block, 0.4f * unit);

        if (block.getWidth() > 3.0f * unit)
        {
            g.setColour (now ? theme::text : past ? theme::dim.withAlpha (0.6f) : theme::text.withAlpha (0.85f));
            g.drawFittedText (sectionName (song, static_cast<int> (i)),
                              block.reduced (0.8f * unit, 0.0f).toNearestInt(),
                              juce::Justification::centredLeft, 1, 1.0f);
        }
    }

    // The host's loop / punch range: a bracket over the strip, orange when on.
    if (s.loopShown)
    {
        const float x0 = xFor (s.loopStart, area, s);
        const float x1 = xFor (s.loopEnd, area, s);
        const auto range = juce::Rectangle<float> (x0, area.getY() - 1.2f * unit, juce::jmax (2.0f, x1 - x0),
                                                   area.getHeight() + 2.4f * unit);
        const auto c = s.looping ? theme::loop : theme::faint;
        g.setColour (c.withAlpha (s.looping ? 0.16f : 0.25f));
        g.fillRect (range);
        g.setColour (c);
        const float t = juce::jmax (2.0f, 0.4f * unit);
        g.fillRect (range.withHeight (t));
        g.fillRect (range.withY (range.getBottom() - t).withHeight (t));
        g.fillRect (range.withWidth (t));
        g.fillRect (range.withX (range.getRight() - t).withWidth (t));
    }

    if (s.hasPosition)
    {
        const float x = xFor (s.ppq, area, s);
        const float w = juce::jmax (2.0f, 0.4f * unit);
        g.setColour (theme::text);
        g.fillRect (juce::Rectangle<float> (x - w * 0.5f, area.getY() - 1.0f * unit, w, area.getHeight() + 2.0f * unit));
        juce::Path tri;
        const float ty = area.getY() - 1.0f * unit;
        tri.addTriangle (x - 1.2f * unit, ty - 1.4f * unit, x + 1.2f * unit, ty - 1.4f * unit, x, ty);
        g.fillPath (tri);
    }
}

void PrompterPainter::paintLyrics (juce::Graphics& g, juce::Rectangle<float> area, float unit, const PrompterScene& s)
{
    if (! s.hasLyrics)
    {
        if (! s.hasSong)
            return;   // the timeline already asks for a set to be linked
        g.setColour (theme::dim);
        g.setFont (font (3.6f * unit));
        g.drawFittedText ("No lyrics yet: paste them in the BoothCue plugin window",
                          area.toNearestInt(), juce::Justification::centred, 2);
        return;
    }

    auto top  = area.removeFromTop (area.getHeight() * 0.64f);
    auto next = area.withTrimmedTop (1.5f * unit);

    const bool counting = s.countdown.kind != Countdown::Kind::none;
    const bool resting  = s.currentLine.empty();

    if (! resting)
    {
        drawFitted (g, currentFit, str (s.currentLine), top, 3, 13.0f * unit, 4.0f * unit, true, theme::text);
    }
    else if (s.countdown.kind == Countdown::Kind::beats)
    {
        // The last bar before an entry: one huge number per beat.
        g.setColour (theme::count);
        g.setFont (font (juce::jmin (top.getHeight() * 0.95f, 40.0f * unit), true));
        g.drawText (juce::String (s.countdown.value), top, juce::Justification::centred, false);
    }
    else if (s.countdown.kind == Countdown::Kind::bars)
    {
        g.setColour (theme::count);
        g.setFont (font (juce::jmin (top.getHeight() * 0.6f, 16.0f * unit), true));
        g.drawText (juce::String (s.countdown.value) + (s.countdown.value == 1 ? " bar" : " bars"),
                    top, juce::Justification::centred, false);
    }
    else if (s.nextCue >= 0)
    {
        g.setColour (theme::faint);
        g.setFont (font (4.0f * unit));
        g.drawText ("rest - next line in " + str (describeDistance (s.beatsToNextLine, s.sig)),
                    top, juce::Justification::centred, false);
    }

    if (s.nextLine.empty())
        return;

    // The next line: grey, smaller. While it is being counted in it turns white
    // (it's the one to sing), and if a current line is still showing, the count
    // sits in a badge in front of it.
    auto colour = counting ? theme::text : theme::dim;
    if (counting && ! resting)
    {
        auto badge = next.removeFromLeft (17.0f * unit).withSizeKeepingCentre (15.0f * unit, 7.0f * unit);
        g.setColour (theme::count);
        g.fillRoundedRectangle (badge, 1.0f * unit);
        g.setColour (theme::background);
        g.setFont (font (5.0f * unit, true));
        const auto text = s.countdown.kind == Countdown::Kind::beats
                              ? juce::String (s.countdown.value)
                              : juce::String (s.countdown.value) + (s.countdown.value == 1 ? " bar" : " bars");
        g.drawFittedText (text, badge.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        next.removeFromRight (17.0f * unit);
    }
    drawFitted (g, nextFit, str (s.nextLine), next, 2, 6.5f * unit, 3.0f * unit, counting, colour);
}

void PrompterPainter::paintStructureOnly (juce::Graphics& g, juce::Rectangle<float> area, float unit,
                                          const PrompterScene& s, const Song& song)
{
    // Lyrics off: the section, huge, for players.
    if (! s.hasSong || s.section < 0)
        return;
    auto top = area.removeFromTop (area.getHeight() * 0.64f);
    drawFitted (g, sectionFit, sectionName (song, s.section), top, 2, 16.0f * unit, 5.0f * unit, true, theme::text);
    if (s.nextSection >= 0)
    {
        g.setColour (theme::dim);
        g.setFont (font (5.0f * unit));
        g.drawFittedText ("next: " + sectionName (song, s.nextSection) + "  in "
                              + str (describeDistance (s.beatsToNextSection, s.sig)),
                          area.toNearestInt(), juce::Justification::centred, 2);
    }
}

void PrompterPainter::drawFitted (juce::Graphics& g, Fit& cache, const juce::String& text, juce::Rectangle<float> area,
                                  int maxLines, float maxFont, float minFont, bool bold, juce::Colour colour)
{
    if (text.isEmpty() || area.isEmpty())
        return;

    const int w = juce::roundToInt (area.getWidth());
    const int h = juce::roundToInt (area.getHeight());

    if (cache.text != text || cache.width != w || cache.height != h || cache.maxLines != maxLines
        || ! juce::exactlyEqual (cache.maxFont, maxFont) || ! juce::exactlyEqual (cache.minFont, minFont)
        || cache.bold != bold)
    {
        auto fits = [&] (float size)
        {
            const auto layout = layoutFor (text, size, bold, colour, area.getWidth());
            return layout.getNumLines() <= maxLines && layout.getHeight() <= area.getHeight()
                && layout.getWidth() <= area.getWidth() + 0.5f;
        };

        float lo = minFont, hi = juce::jmin (maxFont, area.getHeight());
        float best = lo;
        if (hi > lo && fits (hi))
            best = hi;
        else
            for (int i = 0; i < 14 && hi - lo > 0.5f; ++i)
            {
                const float mid = 0.5f * (lo + hi);
                if (fits (mid)) { best = mid; lo = mid; }
                else            hi = mid;
            }

        cache = { text, w, h, maxLines, maxFont, minFont, bold, best };
    }

    const auto layout = layoutFor (text, cache.font, bold, colour, area.getWidth());
    const float y = area.getY() + juce::jmax (0.0f, (area.getHeight() - layout.getHeight()) * 0.5f);
    juce::Graphics::ScopedSaveState clip (g);
    g.reduceClipRegion (area.toNearestInt());
    layout.draw (g, { area.getX(), y, area.getWidth(), layout.getHeight() });
}

}  // namespace boothcue
