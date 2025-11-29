#include "MainController.h"
#include "AudioEngine.h"
#include "AudioExporter.h"
#include "NonDestructiveEditorComponent.h"

class NonDestructiveEditorApplication::MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (juce::String name, AudioEngine& audioEngineRef, AudioExporter& exporter)
        : DocumentWindow (std::move (name),
                          juce::Desktop::getInstance().getDefaultLookAndFeel()
                            .findColour (juce::ResizableWindow::backgroundColourId),
                          juce::DocumentWindow::allButtons),
          audioEngine (audioEngineRef),
          audioExporter (exporter)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setContentOwned (new NonDestructiveEditorComponent (audioEngine, audioExporter), true);
        centreWithSize (1000, 640);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    AudioEngine& audioEngine;
    AudioExporter& audioExporter;
};

const juce::String NonDestructiveEditorApplication::getApplicationName()
{
    return "NonDestructiveEditorApp";
}

const juce::String NonDestructiveEditorApplication::getApplicationVersion()
{
    return "0.1.0";
}

bool NonDestructiveEditorApplication::moreThanOneInstanceAllowed()
{
    return true;
}

void NonDestructiveEditorApplication::initialise (const juce::String&)
{
    audioEngine = std::make_unique<AudioEngine>();
    audioExporter = std::make_unique<AudioExporter>();
    mainWindow.reset (new MainWindow ("Non-Destructive Editor", *audioEngine, *audioExporter));
}

void NonDestructiveEditorApplication::shutdown()
{
    mainWindow = nullptr;
    audioExporter.reset();
    audioEngine.reset();
}

void NonDestructiveEditorApplication::systemRequestedQuit()
{
    quit();
}

START_JUCE_APPLICATION (NonDestructiveEditorApplication)
