#pragma once
#include <JuceHeader.h>

// Paleta inspirada en la captura: azul noche + turquesa
namespace Theme
{
    inline const juce::Colour bg      { 0xff0f1c28 };
    inline const juce::Colour panel   { 0xff17283a };
    inline const juce::Colour panel2  { 0xff1f3548 };
    inline const juce::Colour lane    { 0xff13222f };
    inline const juce::Colour laneAlt { 0xff162838 };
    inline const juce::Colour teal    { 0xff1de9c0 };
    inline const juce::Colour clipBlue{ 0xff1c5d9f };
    inline const juce::Colour clipWave{ 0xff9bd6ff };
    inline const juce::Colour red     { 0xffe0382f };
    inline const juce::Colour recClip { 0xff8a1b1b };
    inline const juce::Colour text    { 0xffd7e6f0 };
    inline const juce::Colour dim     { 0xff7f95a6 };
}

struct ECLookAndFeel : public juce::LookAndFeel_V4
{
    ECLookAndFeel()
    {
        using namespace juce;
        setColour (ResizableWindow::backgroundColourId, Theme::bg);
        setColour (DocumentWindow::backgroundColourId, Theme::bg);
        setColour (Slider::thumbColourId, Theme::teal);
        setColour (Slider::rotarySliderFillColourId, Theme::teal);
        setColour (Slider::rotarySliderOutlineColourId, Theme::panel2);
        setColour (Slider::trackColourId, Theme::teal);
        setColour (Slider::backgroundColourId, Theme::bg);
        setColour (Slider::textBoxTextColourId, Theme::text);
        setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        setColour (TextButton::buttonColourId, Theme::panel2);
        setColour (TextButton::buttonOnColourId, Theme::teal);
        setColour (TextButton::textColourOffId, Theme::text);
        setColour (TextButton::textColourOnId, Colours::black);
        setColour (ComboBox::backgroundColourId, Theme::panel2);
        setColour (ComboBox::outlineColourId, Colours::transparentBlack);
        setColour (ComboBox::textColourId, Theme::text);
        setColour (ComboBox::arrowColourId, Theme::teal);
        setColour (Label::textColourId, Theme::text);
        setColour (TextEditor::backgroundColourId, Theme::bg);
        setColour (TextEditor::textColourId, Theme::text);
        setColour (TextEditor::outlineColourId, Theme::panel2);
        setColour (PopupMenu::backgroundColourId, Theme::panel);
        setColour (PopupMenu::textColourId, Theme::text);
        setColour (PopupMenu::highlightedBackgroundColourId, Theme::teal);
        setColour (PopupMenu::highlightedTextColourId, Colours::black);
        setColour (AlertWindow::backgroundColourId, Theme::panel);
        setColour (AlertWindow::textColourId, Theme::text);
        setColour (AlertWindow::outlineColourId, Theme::panel2);
        setColour (ListBox::backgroundColourId, Theme::bg);
        setColour (ListBox::textColourId, Theme::text);
        setColour (ScrollBar::thumbColourId, Theme::panel2.brighter (0.3f));
        setColour (ToggleButton::textColourId, Theme::text);
        setColour (ToggleButton::tickColourId, Theme::teal);
    }
};
