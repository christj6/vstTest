#include "TestHelpers.h"

// Editor tests: the plug-in's window opens, closes and opens again, and makes no difference to the
// sound.

namespace testing
{

namespace
{

// Closes the plug-in's editor the way JUCE's plug-in wrappers do when the host closes its window:
// telling the processor first, then deleting it.
void closeEditor (juce::AudioProcessor& processor)
{
    if (auto* editor = processor.getActiveEditor())
    {
        processor.editorBeingDeleted (editor);
        delete editor;
    }
}

void testEditorOpens()
{
    std::unique_ptr<juce::AudioProcessor> processor (createPluginFilter());
    check (processor->hasEditor(), "the plug-in says it has an editor");

    auto* editor = processor->createEditorAndMakeActive();
    check (editor != nullptr && editor->getWidth() > 0 && editor->getHeight() > 0, "its editor opens, with a size");
    check (processor->getActiveEditor() == editor, "the plug-in knows its editor is open");

    // Hosts close the editor when its window closes, and ask for a new one when it's opened again.
    closeEditor (*processor);
    check (processor->getActiveEditor() == nullptr, "closing the editor leaves no editor open");

    auto* reopened = processor->createEditorAndMakeActive();
    check (reopened != nullptr && processor->getActiveEditor() == reopened, "the editor opens again after closing");
    closeEditor (*processor);
}

void testEditorDoesNotChangeTheSound()
{
    // Rendering with the editor open gives exactly the same output as without it.
    const std::vector<TimedMessage> events { { 0, noteOn (60) }, { 9000, noteOn (67) }, { 30000, noteOff (60) } };
    const auto withoutEditor = render ({ .events = events });
    const auto withEditor = render ({ .events = events,
                                      .setUp = [] (juce::AudioProcessor& p) { p.createEditorAndMakeActive(); },
                                      .beforeEachBlock = [] (juce::AudioProcessor& p, int blockStart)
                                      {
                                          // Closed before the processor goes, as a host would close it.
                                          if (blockStart + 512 >= 48000)
                                              closeEditor (p);
                                      } });
    check (withoutEditor == withEditor, "having the editor open makes no difference to the sound");
}

} // namespace

void runEditorTests()
{
    testEditorOpens();
    testEditorDoesNotChangeTheSound();
}

} // namespace testing
