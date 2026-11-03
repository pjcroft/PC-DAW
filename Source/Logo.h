#pragma once

#include <JuceHeader.h>
#include "Theme.h"

//==============================================================================
/**
    Croft DAW artwork, drawn with vectors so it's crisp at any size.
    - drawIcon:     the launcher icon (chrome rounded square, interlocking C + D, waveform)
    - drawBadge:    the round emblem used inside the app
    - drawWordmark: "CROFT DAW" lettering in brushed chrome
    The macOS/Linux icon PNG in assets/ is rendered from drawIcon (run with --render-icon).
*/
namespace Logo
{
    inline const juce::Colour glowCyan   { 0xff3fe0ff };
    inline const juce::Colour glowViolet { 0xff9b7bff };

    /** The interlocking "C" and "D" with a waveform through the middle. */
    inline void drawMonogram (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto s = juce::jmin (area.getWidth(), area.getHeight());
        auto c = area.getCentre();
        const float pi = juce::MathConstants<float>::pi;

        auto strokeArc = [&] (float radius, float from, float to, float thickness, juce::Colour colour, bool glow)
        {
            juce::Path p;
            p.addCentredArc (c.x, c.y, radius, radius, 0.0f, from, to, true);

            if (glow)
            {
                for (int i = 3; i >= 1; --i)
                {
                    g.setColour (colour.withAlpha (0.10f));
                    g.strokePath (p, juce::PathStrokeType (thickness + (float) i * s * 0.025f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
            }

            g.setColour (colour);
            g.strokePath (p, juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
        };

        auto chromeArc = [&] (float radius, float from, float to, float thickness)
        {
            juce::Path p;
            p.addCentredArc (c.x, c.y, radius, radius, 0.0f, from, to, true);
            juce::Path outline;
            juce::PathStrokeType (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt).createStrokedPath (outline, p);

            auto b = outline.getBounds();
            juce::ColourGradient grad (Theme::chromeHi, b.getX(), b.getY(), Theme::chromeLo, b.getRight(), b.getBottom(), false);
            grad.addColour (0.45, juce::Colour (0xffb8c3dc));
            grad.addColour (0.55, juce::Colour (0xff5d6a8c));
            g.setGradientFill (grad);
            g.fillPath (outline);
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.strokePath (outline, juce::PathStrokeType (juce::jmax (1.0f, s * 0.006f)));
        };

        const float rOuter = s * 0.40f, rInner = s * 0.30f;
        const float thick = s * 0.085f;
        const float gap = 0.22f;

        // "C": left half, open on the right
        chromeArc (rOuter, pi + gap,         2.0f * pi - gap, thick);   // upper-left -> left -> lower-left (angles: 0 = up)
        // "D": right half, a smaller bowl offset slightly right
        chromeArc (rInner, 0.06f,           pi - 0.06f,      thick);

        // D's vertical stem
        auto stem = juce::Rectangle<float> (thick * 0.9f, rInner * 2.0f + thick * 0.2f).withCentre ({ c.x + s * 0.02f, c.y });
        juce::ColourGradient sg (Theme::chromeHi, stem.getX(), stem.getY(), Theme::chromeLo, stem.getRight(), stem.getBottom(), false);
        g.setGradientFill (sg);
        g.fillRect (stem);
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawRect (stem, juce::jmax (1.0f, s * 0.006f));

        // Neon inner rims
        strokeArc (rOuter - thick * 0.75f, pi + gap + 0.05f, 2.0f * pi - gap - 0.05f, s * 0.012f, glowCyan, true);
        strokeArc (rInner - thick * 0.75f, 0.12f, pi - 0.12f, s * 0.012f, glowViolet, true);

        // Waveform bars across the centre
        const int bars = 15;
        auto waveW = s * 0.50f;
        auto barW = waveW / (float) bars * 0.45f;

        for (int i = 0; i < bars; ++i)
        {
            auto t = (float) i / (float) (bars - 1);
            auto env = std::sin (t * pi);                                 // taller in the middle
            auto wobble = 0.55f + 0.45f * std::abs (std::sin ((float) i * 1.7f + 0.3f));
            auto h = s * 0.26f * env * wobble + s * 0.03f;
            auto x = c.x - waveW / 2.0f + t * waveW;
            auto bar = juce::Rectangle<float> (barW, h).withCentre ({ x, c.y });

            auto col = glowCyan.interpolatedWith (glowViolet, t);
            g.setColour (col.withAlpha (0.25f));
            g.fillRoundedRectangle (bar.expanded (barW * 0.6f, barW * 0.6f), barW);
            g.setColour (col.brighter (0.3f));
            g.fillRoundedRectangle (bar, barW * 0.5f);
        }
    }

    /** Launcher icon: chrome-bezel rounded square. */
    inline void drawIcon (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto s = juce::jmin (area.getWidth(), area.getHeight());
        auto r = area.withSizeKeepingCentre (s, s).reduced (s * 0.06f);   // macOS-style margin
        auto corner = r.getWidth() * 0.225f;

        // Shadow
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRoundedRectangle (r.translated (0, s * 0.015f), corner);

        // Chrome bezel
        juce::ColourGradient bezel (juce::Colour (0xffdfe6f5), r.getX(), r.getY(), juce::Colour (0xff2a3350), r.getRight(), r.getBottom(), false);
        bezel.addColour (0.35, juce::Colour (0xff7d89ad));
        bezel.addColour (0.65, juce::Colour (0xff3a4566));
        g.setGradientFill (bezel);
        g.fillRoundedRectangle (r, corner);

        // Face
        auto face = r.reduced (s * 0.045f);
        juce::ColourGradient faceGrad (juce::Colour (0xff141a2c), face.getX(), face.getY(), juce::Colour (0xff05070d), face.getRight(), face.getBottom(), false);
        g.setGradientFill (faceGrad);
        g.fillRoundedRectangle (face, corner * 0.8f);

        // Subtle concentric grooves
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        for (int i = 1; i <= 3; ++i)
            g.drawEllipse (face.withSizeKeepingCentre (face.getWidth() * (0.62f + 0.11f * (float) i), face.getHeight() * (0.62f + 0.11f * (float) i)), juce::jmax (1.0f, s * 0.004f));

        // Edge highlight
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawRoundedRectangle (r.reduced (s * 0.006f), corner, juce::jmax (1.0f, s * 0.006f));

        drawMonogram (g, face.reduced (face.getWidth() * 0.06f));

        // Specular glint, top right
        auto glint = juce::Point<float> (r.getRight() - corner * 0.55f, r.getY() + corner * 0.45f);
        juce::ColourGradient glow (juce::Colours::white.withAlpha (0.55f), glint.x, glint.y,
                                   juce::Colours::white.withAlpha (0.0f), glint.x + s * 0.05f, glint.y, true);
        g.setGradientFill (glow);
        g.fillEllipse (juce::Rectangle<float> (s * 0.10f, s * 0.10f).withCentre (glint));
    }

    /** Round emblem for the transport bar. */
    inline void drawBadge (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto s = juce::jmin (area.getWidth(), area.getHeight());
        auto r = area.withSizeKeepingCentre (s, s);

        juce::ColourGradient ring (Theme::chromeHi, r.getX(), r.getY(), Theme::chromeEdge, r.getRight(), r.getBottom(), false);
        g.setGradientFill (ring);
        g.fillEllipse (r);
        g.setColour (juce::Colour (0xff070a12));
        g.fillEllipse (r.reduced (s * 0.08f));
        g.setColour (glowCyan.withAlpha (0.7f));
        g.drawEllipse (r.reduced (s * 0.12f), juce::jmax (1.0f, s * 0.02f));

        drawMonogram (g, r.reduced (s * 0.1f));
    }

    /** "CROFT" in large brushed chrome with "DAW" beneath. */
    inline void drawWordmark (juce::Graphics& g, juce::Rectangle<float> area)
    {
        auto top = area.removeFromTop (area.getHeight() * 0.68f);

        juce::GlyphArrangement glyphs;
        auto font = juce::Font (juce::FontOptions (top.getHeight() * 0.95f, juce::Font::bold | juce::Font::italic));
        font.setExtraKerningFactor (0.06f);
        glyphs.addFittedText (font, "CROFT", top.getX(), top.getY(), top.getWidth(), top.getHeight(), juce::Justification::centredLeft, 1);

        juce::Path text;
        glyphs.createPath (text);
        auto b = text.getBounds();

        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillPath (text, juce::AffineTransform::translation (0, 1.5f));

        juce::ColourGradient chrome (juce::Colour (0xfff2f5fb), 0, b.getY(), juce::Colour (0xff59648a), 0, b.getBottom(), false);
        chrome.addColour (0.48, juce::Colour (0xffb7c1db));
        chrome.addColour (0.52, juce::Colour (0xff6b7798));
        g.setGradientFill (chrome);
        g.fillPath (text);

        g.setColour (glowCyan.withAlpha (0.35f));
        g.strokePath (text, juce::PathStrokeType (0.8f));

        g.setColour (Theme::chromeMid);
        g.setFont (juce::FontOptions (area.getHeight() * 0.95f, juce::Font::bold));
        auto daw = area.withWidth (b.getWidth());
        g.drawText ("D  A  W", daw, juce::Justification::centred, false);
    }

    /** Renders the launcher icon to an image (used for the window icon and assets/icon.png). */
    inline juce::Image createIconImage (int size)
    {
        juce::Image img (juce::Image::ARGB, size, size, true);
        juce::Graphics g (img);
        drawIcon (g, img.getBounds().toFloat());
        return img;
    }
}
