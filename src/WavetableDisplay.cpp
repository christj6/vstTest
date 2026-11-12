#include "WavetableDisplay.h"

namespace boringsynth
{

namespace
{

constexpr int refreshesPerSecond = 30;

// Sizes, in pixels.
constexpr float padding = 2.0f;
constexpr float markerHeight = 3.0f;
constexpr float markerWidth = 6.0f;
constexpr float curveThickness = 1.5f;

} // namespace

WavetableDisplay::WavetableDisplay (const WaveformTables& waveformTables, const WavetableImporter& importer, const std::atomic<float>& waveform,
                                    const std::atomic<float>& knob, const std::atomic<float>& playedPosition)
    : tables (waveformTables),
      wavetableImporter (importer),
      waveformValue (waveform),
      knobValue (knob),
      playedPositionValue (playedPosition)
{
    setComponentID ("wavetableDisplay");
    setTitle ("Waveform display");
    setInterceptsMouseClicks (false, false);
    refresh();
    startTimerHz (refreshesPerSecond);
}

void WavetableDisplay::refresh()
{
    const auto waveform = juce::jlimit (0, waveformNames.size() - 1, juce::roundToInt (waveformValue.load()));
    const auto played = playedPositionValue.load();
    const auto position = played >= 0.0f ? played : knobValue.load() / 100.0f;

    const auto generation = wavetableImporter.getGeneration();

    if (waveform == shownWaveform && position == shownPosition && generation == shownGeneration)
        return;

    if (waveform != shownWaveform || generation != shownGeneration)
        readTable (waveform, [this] (const MultiFrameWaveTable& table) { peak = getPeak (table); });

    shownWaveform = waveform;
    shownPosition = position;
    shownGeneration = generation;
    repaint();
}

void WavetableDisplay::readTable (int waveform, const std::function<void (const MultiFrameWaveTable&)>& reader) const
{
    if (waveform != importedWaveform)
    {
        reader (tables.get (waveform));
        return;
    }

    // Holding a reference while reading it keeps it from being freed.
    const auto imported = wavetableImporter.getLatest();
    reader (imported != nullptr && imported->getTable() != nullptr ? *imported->getTable() : tables.get (waveform));
}

float WavetableDisplay::getPeak (const MultiFrameWaveTable& table)
{
    auto largest = 0.0f;

    for (int frame = 0; frame < table.getNumFrames(); ++frame)
        for (int i = 0; i < WaveTable::size; ++i)
            largest = std::max (largest, std::abs (table.getFrame (frame).getLevel (0).getSample (juce::MathConstants<double>::twoPi * i / WaveTable::size)));

    // A silent table has nothing to scale.
    return largest > 0.0f ? largest : 1.0f;
}

void WavetableDisplay::paint (juce::Graphics& g)
{
    g.fillAll (findColour (juce::ComboBox::backgroundColourId));
    readTable (shownWaveform, [&] (const MultiFrameWaveTable& table) { drawTable (g, table); });
}

void WavetableDisplay::drawTable (juce::Graphics& g, const MultiFrameWaveTable& table) const
{
    const auto isWavetable = table.getNumFrames() > 1;
    const auto thumbColour = findColour (juce::Slider::thumbColourId);

    auto area = getLocalBounds().toFloat().reduced (padding);

    if (isWavetable)
    {
        const auto marker = area.removeFromBottom (markerHeight);
        g.setColour (findColour (juce::ComboBox::outlineColourId));
        g.fillRect (marker);
        g.setColour (thumbColour);
        g.fillRect (marker.withWidth (markerWidth).withX (marker.getX() + shownPosition * (marker.getWidth() - markerWidth)));
        area.removeFromBottom (padding);
    }

    g.setColour (findColour (juce::ComboBox::outlineColourId));
    g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());

    // One cycle, a point for each pixel across, from phase 0 back round to phase 0.
    const auto yFor = [&] (float value) { return area.getCentreY() - value / peak * area.getHeight() * 0.5f; };
    const auto numPoints = juce::jmax (1, juce::roundToInt (area.getWidth()));
    juce::Path curve;
    curve.startNewSubPath (area.getX(), yFor (table.getSample (shownPosition, 0, 0.0)));

    for (int i = 1; i <= numPoints; ++i)
    {
        const auto phase = i < numPoints ? juce::MathConstants<double>::twoPi * i / numPoints : 0.0;
        curve.lineTo (area.getX() + area.getWidth() * (float) i / (float) numPoints, yFor (table.getSample (shownPosition, 0, phase)));
    }

    g.setColour (thumbColour);
    g.strokePath (curve, juce::PathStrokeType (curveThickness));
}

} // namespace boringsynth
