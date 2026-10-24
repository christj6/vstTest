#include "TestHelpers.h"

#include "Envelope.h"

// Envelope tests: the ADSR envelope on its own, one sample at a time, at 48 kHz.

namespace testing
{

namespace
{

constexpr double envelopeSampleRate = 48000.0;

boringsynth::Envelope makeEnvelope (float attack, float decay, float sustain, float release)
{
    boringsynth::Envelope envelope;
    envelope.setSampleRate (envelopeSampleRate);
    envelope.setSettings ({ attack, decay, sustain, release });
    return envelope;
}

std::vector<float> next (boringsynth::Envelope& envelope, int numSamples)
{
    std::vector<float> levels ((size_t) numSamples);

    for (auto& level : levels)
        level = envelope.getNextSample();

    return levels;
}

float largestStepIn (const std::vector<float>& levels)
{
    float largest = 0.0f;

    for (size_t i = 1; i < levels.size(); ++i)
        largest = std::max (largest, std::abs (levels[i] - levels[i - 1]));

    return largest;
}

// Samples until the envelope stops, or -1 if it's still going after `limit` samples.
int samplesUntilFinished (boringsynth::Envelope& envelope, int limit)
{
    for (int i = 1; i <= limit; ++i)
    {
        envelope.getNextSample();

        if (! envelope.isActive())
            return i;
    }

    return -1;
}

//==============================================================================
void testInstantAttack()
{
    auto envelope = makeEnvelope (0.0f, 0.2f, 1.0f, 0.005f);
    envelope.noteOn();
    check (envelope.getNextSample() == 1.0f, "an attack of 0 starts the note at full level");
}

void testAttackTime()
{
    // 10 ms is 480 samples: halfway up after 240, at full level from 480 on.
    auto envelope = makeEnvelope (0.01f, 0.2f, 1.0f, 0.005f);
    envelope.noteOn();
    const auto levels = next (envelope, 1000);
    std::printf ("      10 ms attack: %.4f at the start, %.4f after 240 samples, %.4f after 480\n",
                 levels[0], levels[240], levels[480]);
    check (levels[0] == 0.0f && std::abs (levels[240] - 0.5f) < 0.001f && levels[480] == 1.0f && levels[470] < 1.0f,
           "the attack rises from 0 to full level over the attack time");
}

void testDecayToSustain()
{
    // From full level to a sustain of 0.25 over 100 ms (4800 samples), then holding there.
    auto envelope = makeEnvelope (0.0f, 0.1f, 0.25f, 0.005f);
    envelope.noteOn();
    const auto levels = next (envelope, 9600);
    check (std::abs (levels[2400] - 0.625f) < 0.001f, "halfway through the decay, the level is halfway to the sustain level");
    check (levels[4801] == 0.25f && levels[9599] == 0.25f, "after the decay, the level holds at the sustain level");
}

void testReleaseTime()
{
    // Released from a sustain of 0.5 with a 100 ms release: silent 4800 samples later.
    auto envelope = makeEnvelope (0.0f, 0.01f, 0.5f, 0.1f);
    envelope.noteOn();
    next (envelope, 2400);
    envelope.noteOff();
    const auto length = samplesUntilFinished (envelope, 10000);
    std::printf ("      100 ms release from the sustain level took %d samples (expected 4800)\n", length);
    check (std::abs (length - 4800) <= 1, "the release falls to silence over the release time");
}

void testReleaseDuringAttack()
{
    // Let go 100 ms into a 1 s attack, at about a tenth of full level. The release should still
    // take its full 100 ms from there. (juce::ADSR would recalculate it from the sustain level.)
    auto envelope = makeEnvelope (1.0f, 0.2f, 1.0f, 0.1f);
    envelope.noteOn();
    next (envelope, 4800);
    envelope.noteOff();

    // Update the settings on every "block", as the voice does, to show it doesn't upset the release.
    int length = -1;

    for (int i = 1; i <= 10000 && length < 0; ++i)
    {
        if (i % 512 == 0)
            envelope.setSettings ({ 1.0f, 0.2f, 1.0f, 0.1f });

        envelope.getNextSample();

        if (! envelope.isActive())
            length = i;
    }

    std::printf ("      released during the attack: the 100 ms release took %d samples (expected 4800)\n", length);
    check (std::abs (length - 4800) <= 1, "a note released during its attack still takes the full release time");
}

void testReleaseWithZeroSustain()
{
    // With a sustain of 0, juce::ADSR cuts a note off instantly once its settings are updated.
    // Here it should fade over the release time like any other note.
    auto envelope = makeEnvelope (0.0f, 1.0f, 0.0f, 0.05f);
    envelope.noteOn();
    next (envelope, 4800);
    envelope.noteOff();
    envelope.setSettings ({ 0.0f, 1.0f, 0.0f, 0.05f });
    const auto levels = next (envelope, 3000);
    const auto largest = largestStepIn (levels);
    std::printf ("      sustain 0, released mid-decay: largest step %.5f (a 50 ms release from ~0.9 moves ~0.0004 a sample)\n", largest);
    check (largest < 0.001f && levels[0] > 0.8f, "with a sustain of 0, the release still fades instead of cutting off");
}

void testSustainChangeGlides()
{
    // Holding at full level, the sustain is dropped to 0. The level should glide down over 20 ms
    // (960 samples) rather than jump.
    auto envelope = makeEnvelope (0.0f, 0.01f, 1.0f, 0.005f);
    envelope.noteOn();
    next (envelope, 100);
    envelope.setSettings ({ 0.0f, 0.01f, 0.0f, 0.005f });
    const auto levels = next (envelope, 2000);
    const auto largest = largestStepIn (levels);
    std::printf ("      sustain dropped from 100 %% to 0: largest step %.5f, at 0 after %d samples\n",
                 largest, (int) (std::find (levels.begin(), levels.end(), 0.0f) - levels.begin()));
    check (largest <= 1.0f / 960.0f + 1.0e-6f && levels[1000] == 0.0f, "changing the sustain level glides over 20 ms instead of jumping");
}

void testSustainRaisedDuringDecay()
{
    // Decaying from full level towards 0.2, the sustain is raised to 0.8 after the level has
    // already fallen below it. The level should glide back up, not jump.
    auto envelope = makeEnvelope (0.0f, 0.1f, 0.2f, 0.005f);
    envelope.noteOn();
    next (envelope, 4000);
    envelope.setSettings ({ 0.0f, 0.1f, 0.8f, 0.005f });
    const auto levels = next (envelope, 2000);
    check (largestStepIn (levels) <= 1.0f / 960.0f + 1.0e-6f && levels[1999] == 0.8f,
           "raising the sustain level during the decay glides up to it");
}

void testNoteOffWhenIdle()
{
    auto envelope = makeEnvelope (0.0f, 0.2f, 1.0f, 0.005f);
    envelope.noteOff();
    check (! envelope.isActive() && envelope.getNextSample() == 0.0f, "a note off with no note playing does nothing");
}

} // namespace

void runEnvelopeTests()
{
    testInstantAttack();
    testAttackTime();
    testDecayToSustain();
    testReleaseTime();
    testReleaseDuringAttack();
    testReleaseWithZeroSustain();
    testSustainChangeGlides();
    testSustainRaisedDuringDecay();
    testNoteOffWhenIdle();
}

} // namespace testing
