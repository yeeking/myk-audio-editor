#include "AudioEngine.h"
#include "../libs/tracktion_engine/examples/common/PluginWindow.h"

namespace
{
    class RenderTaskRunner : public juce::Thread
    {
    public:
        explicit RenderTaskRunner (te::ThreadPoolJobWithProgress& jobToRun)
            : juce::Thread (jobToRun.getJobName()), job (jobToRun)
        {
            startThread();
        }

        ~RenderTaskRunner() override
        {
            signalThreadShouldExit();
            waitForThreadToExit (10000);
        }

        void run() override
        {
            while (! threadShouldExit())
            {
                auto status = job.runJob();
                if (status != juce::ThreadPoolJob::jobNeedsRunningAgain)
                    break;
            }
        }

    private:
        te::ThreadPoolJobWithProgress& job;
    };

    class RenderProgressComponent : public juce::Component,
                                    private juce::Timer
    {
    public:
        RenderProgressComponent (te::ThreadPoolJobWithProgress& task, double& progressValue)
            : job (task), progress (progressValue), progressBar (progress)
        {
            addAndMakeVisible (titleLabel);
            titleLabel.setJustificationType (juce::Justification::centred);
            titleLabel.setText (job.getJobName().isNotEmpty() ? job.getJobName() : "Processing...",
                                juce::dontSendNotification);

            addAndMakeVisible (progressBar);
            progressBar.setPercentageDisplay (true);
            startTimerHz (20);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (10);
            titleLabel.setBounds (area.removeFromTop (24));
            progressBar.setBounds (area.removeFromTop (30));
        }

    private:
        void timerCallback() override
        {
            progress = juce::jlimit (0.0, 1.0, static_cast<double> (job.getCurrentTaskProgress()));
            repaint();
        }

        te::ThreadPoolJobWithProgress& job;
        double& progress;
        juce::Label titleLabel;
        juce::ProgressBar progressBar;
    };

    class AppUIBehaviour : public ExtendedUIBehaviour
    {
    public:
        void runTaskWithProgressBar (te::ThreadPoolJobWithProgress& task) override
        {
            double progressValue = task.getCurrentTaskProgress();
            std::unique_ptr<juce::DialogWindow> dialog;
            {
                const juce::MessageManagerLock mmLock;
                auto progressComponent = std::make_unique<RenderProgressComponent> (task, progressValue);

                juce::DialogWindow::LaunchOptions options;
                options.dialogTitle = task.getJobName().isNotEmpty() ? task.getJobName() : juce::String ("Processing");
                options.escapeKeyTriggersCloseButton = false;
                options.useNativeTitleBar = true;
                options.resizable = false;
                options.content.setOwned (progressComponent.release());
                dialog.reset (options.create());
                dialog->centreWithSize (360, 140);
                dialog->setVisible (true);
            }

            RenderTaskRunner runner (task);

            while (runner.isThreadRunning())
            {
                if (! juce::MessageManager::getInstance()->runDispatchLoopUntil (50))
                    break;
            }

            const juce::MessageManagerLock mmLock;
            dialog.reset();
        }
    };
}

AudioEngine::AudioEngine()
    : engine (ProjectInfo::projectName, std::make_unique<AppUIBehaviour>(), nullptr)
{
    auto& devMan = engine.getDeviceManager();
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    devMan.deviceManager.getAudioDeviceSetup (setup);

    auto* currentDevice = devMan.deviceManager.getCurrentAudioDevice();
    const auto deviceName = currentDevice != nullptr ? currentDevice->getName() : juce::String ("<none>");
    const auto sr = currentDevice != nullptr ? currentDevice->getCurrentSampleRate() : setup.sampleRate;
    const auto buffer = currentDevice != nullptr ? currentDevice->getCurrentBufferSizeSamples() : setup.bufferSize;

    if (currentDevice != nullptr)
    {
        const int availableInputs = currentDevice->getInputChannelNames().size();
        const int activeInputs = setup.inputChannels.countNumberOfSetBits();

        if (availableInputs > 0 && activeInputs < availableInputs)
        {
            setup.inputChannels.setRange (0, availableInputs, true);
            devMan.deviceManager.setAudioDeviceSetup (setup, true);
            devMan.rescanWaveDeviceList();
        }
    }

    juce::String info;
    info << "Audio device: " << deviceName
         << " | input: " << (setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName : "<none>")
         << " (" << setup.inputChannels.countNumberOfSetBits() << " ch)"
         << " | output: " << (setup.outputDeviceName.isNotEmpty() ? setup.outputDeviceName : "<none>")
         << " (" << setup.outputChannels.countNumberOfSetBits() << " ch)"
         << " | rate: " << sr << " Hz"
         << " | buffer: " << buffer << " samples"
         << " | latency: " << juce::String (devMan.getOutputLatencySeconds(), 4) << " s";

    DBG (info);
}

te::Engine& AudioEngine::getEngine()
{
    return engine;
}

std::unique_ptr<te::Edit> AudioEngine::createEmptyEdit (const juce::String& tempName)
{
    auto editFile = engine.getTemporaryFileManager().getTempFile (tempName)
                      .withFileExtension (te::projectFileSuffix);
    auto newEdit = te::createEmptyEdit (engine, editFile);
    newEdit->playInStopEnabled = true;
    return newEdit;
}
