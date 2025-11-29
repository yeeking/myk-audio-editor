#pragma once

#include <JuceHeader.h>

class NonDestructiveEditorApplication : public juce::JUCEApplication
{
public:
    NonDestructiveEditorApplication() = default;
    const juce::String getApplicationName() override;
    const juce::String getApplicationVersion() override;
    bool moreThanOneInstanceAllowed() override;

    void initialise (const juce::String&) override;
    void shutdown() override;
    void systemRequestedQuit() override;

private:
    class MainWindow;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<class AudioEngine> audioEngine;
    std::unique_ptr<class AudioExporter> audioExporter;
};
