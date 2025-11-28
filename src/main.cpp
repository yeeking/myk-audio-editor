#include <JuceHeader.h>
#include "NonDestructiveEditorComponent.h"

namespace te = tracktion;

class NonDestructiveEditorApplication  : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override       { return "NonDestructiveEditorApp"; }
    const juce::String getApplicationVersion() override    { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow.reset (new MainWindow ("Non-Destructive Editor", engine));
    }

    void shutdown() override
    {
        mainWindow = nullptr;
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name, te::Engine& e)
            : DocumentWindow (std::move (name),
                              juce::Desktop::getInstance().getDefaultLookAndFeel()
                                .findColour (juce::ResizableWindow::backgroundColourId),
                              juce::DocumentWindow::allButtons),
              engine (e)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, false);
            setContentOwned (new NonDestructiveEditorComponent (engine), true);
            centreWithSize (1000, 640);
            setVisible (true);
        }

        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        te::Engine& engine;
    };

    std::unique_ptr<MainWindow> mainWindow;
    te::Engine engine { ProjectInfo::projectName, std::make_unique<ExtendedUIBehaviour>(), nullptr };
};

START_JUCE_APPLICATION (NonDestructiveEditorApplication)
