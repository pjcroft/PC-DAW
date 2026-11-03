#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Midnight Chrome — every colour in the app lives here.
    Tweak these values (or paste your own hex codes) to re-skin everything.
*/
namespace Theme
{
    // Backgrounds, darkest -> lightest
    inline const juce::Colour bgDeep        { 0xff07090f };   // window / behind everything
    inline const juce::Colour bgPanel       { 0xff0e1220 };   // transport + headers
    inline const juce::Colour bgPanelHi     { 0xff161c2e };   // raised panel highlight
    inline const juce::Colour laneEven      { 0xff0b0f1a };   // timeline lane
    inline const juce::Colour laneOdd       { 0xff0d1220 };
    inline const juce::Colour laneSelected  { 0xff141b2f };

    // Chrome (brushed metal) for buttons, knobs, borders
    inline const juce::Colour chromeHi      { 0xffe6ebf2 };
    inline const juce::Colour chromeMid     { 0xff9aa3b2 };
    inline const juce::Colour chromeLo      { 0xff4a5160 };
    inline const juce::Colour chromeEdge    { 0xff252b38 };

    // Text
    inline const juce::Colour textBright    { 0xffdfe6f0 };
    inline const juce::Colour textDim       { 0xff7d8799 };
    inline const juce::Colour textOnChrome  { 0xff10131b };

    // Accents
    inline const juce::Colour accent        { 0xff5ac8fa };   // ice blue: selection, playhead, meters
    inline const juce::Colour accentGlow    { 0x405ac8fa };
    inline const juce::Colour record        { 0xffff3b4e };   // arm / record
    inline const juce::Colour mute          { 0xfff2b84b };
    inline const juce::Colour solo          { 0xff4be08a };
    inline const juce::Colour monitor       { 0xffb48cff };

    // Grid
    inline const juce::Colour gridBar       { 0xff222a3d };
    inline const juce::Colour gridBeat      { 0xff151b2a };

    // Clips
    inline const juce::Colour clipAudio     { 0xff2b4f7a };
    inline const juce::Colour clipMidi      { 0xff3d3a7a };
    inline const juce::Colour clipWave      { 0xffcfe6ff };
    inline const juce::Colour clipRecording { 0xff7a1f2b };

    /** Brushed-chrome vertical gradient for a rectangle. */
    inline juce::ColourGradient chromeGradient (juce::Rectangle<float> r, bool pressed = false)
    {
        juce::ColourGradient g (pressed ? chromeLo : chromeHi, r.getX(), r.getY(),
                                pressed ? chromeMid : chromeLo, r.getX(), r.getBottom(), false);
        g.addColour (0.48, pressed ? chromeLo.brighter (0.1f) : chromeMid.brighter (0.25f));
        g.addColour (0.52, pressed ? chromeLo : chromeMid);
        return g;
    }

    /** Dark glassy gradient used for panels. */
    inline void fillPanel (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setGradientFill (juce::ColourGradient (bgPanelHi, r.getX(), r.getY(),
                                                 bgPanel, r.getX(), r.getBottom(), false));
        g.fillRect (r);
        g.setColour (chromeEdge);
        g.drawLine (r.getX(), r.getBottom() - 0.5f, r.getRight(), r.getBottom() - 0.5f, 1.0f);
    }
}

//==============================================================================
class MidnightChromeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MidnightChromeLookAndFeel()
    {
        using namespace Theme;
        setColourScheme ({ bgPanel, bgDeep, bgPanelHi, chromeEdge, textBright,
                           accent, textOnChrome, accent.withAlpha (0.25f), textBright });

        setColour (juce::ResizableWindow::backgroundColourId, bgDeep);
        setColour (juce::DocumentWindow::textColourId, textBright);
        setColour (juce::Label::textColourId, textBright);
        setColour (juce::TextButton::textColourOffId, textOnChrome);
        setColour (juce::TextButton::textColourOnId, textOnChrome);
        setColour (juce::ComboBox::backgroundColourId, bgDeep);
        setColour (juce::ComboBox::outlineColourId, chromeEdge);
        setColour (juce::ComboBox::textColourId, textBright);
        setColour (juce::ComboBox::arrowColourId, chromeMid);
        setColour (juce::PopupMenu::backgroundColourId, bgPanel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.3f));
        setColour (juce::PopupMenu::textColourId, textBright);
        setColour (juce::PopupMenu::highlightedTextColourId, textBright);
        setColour (juce::Slider::thumbColourId, chromeHi);
        setColour (juce::Slider::trackColourId, accent);
        setColour (juce::Slider::backgroundColourId, bgDeep);
        setColour (juce::Slider::rotarySliderFillColourId, accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, chromeEdge);
        setColour (juce::Slider::textBoxTextColourId, textBright);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::backgroundColourId, bgDeep);
        setColour (juce::TextEditor::textColourId, textBright);
        setColour (juce::TextEditor::outlineColourId, chromeEdge);
        setColour (juce::TextEditor::focusedOutlineColourId, accent);
        setColour (juce::ScrollBar::thumbColourId, chromeLo);
        setColour (juce::ListBox::backgroundColourId, bgDeep);
    }

    // Chrome buttons. A button's "buttonOnColourId" is used as an LED tint when toggled on.
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                               bool isOver, bool isDown) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        const float corner = 4.0f;

        // Drop shadow
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.fillRoundedRectangle (r.translated (0, 1.0f), corner);

        if (on)
        {
            auto led = b.findColour (juce::TextButton::buttonOnColourId);
            g.setGradientFill (juce::ColourGradient (led.brighter (0.4f), r.getX(), r.getY(),
                                                     led.darker (0.3f), r.getX(), r.getBottom(), false));
        }
        else
        {
            g.setGradientFill (Theme::chromeGradient (r, isDown));
        }

        g.fillRoundedRectangle (r, corner);

        if (isOver)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (r, corner);
        }

        g.setColour (Theme::chromeEdge);
        g.drawRoundedRectangle (r, corner, 1.0f);

        // Top specular line
        g.setColour (juce::Colours::white.withAlpha (on ? 0.25f : 0.55f));
        g.drawLine (r.getX() + corner, r.getY() + 1.0f, r.getRight() - corner, r.getY() + 1.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) h * 0.55f), juce::Font::bold));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (2.0f);
        auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) / 2.0f;
        auto centre = bounds.getCentre();
        auto knob = juce::Rectangle<float> (radius * 2, radius * 2).withCentre (centre).reduced (3.0f);

        // Arc track
        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius - 1, radius - 1, 0, startAngle, endAngle, true);
        g.setColour (Theme::bgDeep);
        g.strokePath (track, juce::PathStrokeType (2.5f));

        // Value arc (bipolar for pan-style sliders centred on 0)
        const bool bipolar = s.getMinimum() < 0 && s.getMaximum() > 0;
        const float zeroAngle = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        const float valueAngle = startAngle + pos * (endAngle - startAngle);
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, radius - 1, radius - 1, 0,
                             juce::jmin (zeroAngle, valueAngle), juce::jmax (zeroAngle, valueAngle), true);
        g.setColour (Theme::accent);
        g.strokePath (value, juce::PathStrokeType (2.5f));

        // Chrome cap
        g.setGradientFill (Theme::chromeGradient (knob));
        g.fillEllipse (knob);
        g.setColour (Theme::chromeEdge);
        g.drawEllipse (knob, 1.0f);

        // Pointer
        juce::Path p;
        p.addRoundedRectangle (-1.0f, -knob.getHeight() * 0.5f + 2.0f, 2.0f, knob.getHeight() * 0.35f, 1.0f);
        g.setColour (Theme::textOnChrome);
        g.fillPath (p, juce::AffineTransform::rotation (valueAngle).translated (centre));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos,
                           float, float, juce::Slider::SliderStyle style, juce::Slider& s) override
    {
        if (style != juce::Slider::LinearHorizontal)
            return LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, sliderPos, 0, 0, style, s);

        auto r = juce::Rectangle<int> (x, y, w, h).toFloat();
        auto groove = r.withSizeKeepingCentre (r.getWidth(), 4.0f);
        g.setColour (Theme::bgDeep);
        g.fillRoundedRectangle (groove, 2.0f);
        g.setColour (Theme::accent.withAlpha (0.8f));
        g.fillRoundedRectangle (groove.withRight (sliderPos), 2.0f);

        auto cap = juce::Rectangle<float> (10.0f, juce::jmin (16.0f, r.getHeight())).withCentre ({ sliderPos, r.getCentreY() });
        g.setGradientFill (Theme::chromeGradient (cap));
        g.fillRoundedRectangle (cap, 2.0f);
        g.setColour (Theme::chromeEdge);
        g.drawRoundedRectangle (cap, 2.0f, 1.0f);
        g.setColour (Theme::textOnChrome);
        g.drawLine (cap.getCentreX(), cap.getY() + 3, cap.getCentreX(), cap.getBottom() - 3, 1.0f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<int> (w, h).toFloat().reduced (0.5f);
        g.setColour (Theme::bgDeep);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (box.hasKeyboardFocus (true) ? Theme::accent : Theme::chromeEdge);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);

        juce::Path arrow;
        auto a = juce::Rectangle<float> ((float) w - 14.0f, (float) h * 0.5f - 2.0f, 8.0f, 4.0f);
        arrow.addTriangle (a.getX(), a.getY(), a.getRight(), a.getY(), a.getCentreX(), a.getBottom());
        g.setColour (Theme::chromeMid);
        g.fillPath (arrow);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override        { return juce::Font (juce::FontOptions (12.0f)); }
    juce::Font getPopupMenuFont() override                        { return juce::Font (juce::FontOptions (14.0f)); }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (4, 0, box.getWidth() - 18, box.getHeight());
        label.setFont (getComboBoxFont (box));
    }
};
