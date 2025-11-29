#include "AudioEngine.h"
#include "../libs/tracktion_engine/examples/common/PluginWindow.h"
#include "../libs/tracktion_engine/examples/common/Utilities.h"
#include <algorithm>

using namespace tracktion::literals;
using namespace std::literals;

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
            progressBar.setColour (juce::ProgressBar::foregroundColourId, juce::Colours::yellow);
            progressBar.setColour (juce::ProgressBar::backgroundColourId, juce::Colours::darkgrey);
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
                juce::MessageManager::getInstance()->runDispatchLoopUntil (50);

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

te::Edit* AudioEngine::getEdit()
{
    return edit.get();
}

te::AudioTrack* AudioEngine::getTrack()
{
    return track;
}

bool AudioEngine::createNewEdit (const juce::String& tempName)
{
    auto editFile = engine.getTemporaryFileManager().getTempFile (tempName)
                      .withFileExtension (te::projectFileSuffix);
    edit = te::createEmptyEdit (engine, editFile);
    edit->playInStopEnabled = true;

    track = EngineHelpers::getOrInsertAudioTrackAt (*edit, 0);
    segments.clear();
    clipboard.clear();
    thumbnail.reset();
    loadedFile = juce::File();
    loadedFileLength = 0s;
    insertionPoint = 0_tp;
    undoStack.clear();
    return track != nullptr;
}

bool AudioEngine::loadFile (const juce::File& file, juce::String& statusOut)
{
    if (edit == nullptr)
    {
        statusOut = "No edit available";
        return false;
    }

    te::AudioFile audioFile (engine, file);

    if (! audioFile.isValid())
    {
        statusOut = "Unsupported audio file";
        return false;
    }

    loadedFile = file;
    loadedFileLength = te::TimeDuration::fromSeconds (audioFile.getLength());
    segments.clear();
    clipboard.clear();
    insertionPoint = 0_tp;

    segments.push_back ({ loadedFileLength, 0s });

    // thumbnail = std::make_unique<te::SmartThumbnail> (engine, audioFile, thumbnailComponent, nullptr);
    rebuildTrack();
    statusOut = "Loaded " + file.getFileName();
    return true;
}

const std::vector<IAudioEngine::Segment>& AudioEngine::getSegments() const
{
    return segments;
}

IAudioEngine::TimeDuration AudioEngine::getTotalLength() const
{
    TimeDuration total { 0s };
    for (auto& seg : segments)
        total = total + seg.length;
    return total;
}

te::SmartThumbnail* AudioEngine::getThumbnail() const
{
    return thumbnail.get();
}

juce::File AudioEngine::getDisplayFile() const
{
    return displayFile;
}

void AudioEngine::setInsertionPoint (TimePosition pos)
{
    insertionPoint = clampToTimeline (pos);

    if (edit != nullptr)
        edit->getTransport().setPosition (insertionPoint);
}

IAudioEngine::TimePosition AudioEngine::getInsertionPoint() const
{
    return insertionPoint;
}

bool AudioEngine::copySelection (TimeRange selection)
{
    clipboard.clear();

    auto total = getTotalLength();
    auto clamped = selection.getIntersectionWith ({ 0_tp, TimePosition::fromSeconds (total.inSeconds()) });

    auto pos = 0_tp;
    for (auto& seg : segments)
    {
        const auto segRange = TimeRange { pos, pos + seg.length };
        const auto intersection = segRange.getIntersectionWith (clamped);

        if (! intersection.isEmpty())
        {
            auto relStart = intersection.getStart() - clamped.getStart();
            auto offset = seg.sourceOffset + (intersection.getStart() - segRange.getStart());
            clipboard.push_back ({ relStart, intersection.getLength(), offset });
        }

        pos = pos + seg.length;
    }

    return ! clipboard.empty();
}

bool AudioEngine::cutSelection (TimeRange selection)
{
    if (segments.empty())
        return false;

    std::vector<Segment> newSegments;
    auto sel = selection;
    auto pos = 0_tp;

    for (auto& seg : segments)
    {
        const auto segRange = TimeRange { pos, pos + seg.length };
        const auto intersection = segRange.getIntersectionWith (sel);

        if (intersection.isEmpty())
        {
            newSegments.push_back (seg);
        }
        else
        {
            auto preLen  = intersection.getStart() - segRange.getStart();
            auto postLen = segRange.getEnd() - intersection.getEnd();

            if (preLen > 0s)
                newSegments.push_back ({ preLen, seg.sourceOffset });

            if (postLen > 0s)
            {
                auto newOffset = seg.sourceOffset + (seg.length - postLen);
                newSegments.push_back ({ postLen, newOffset });
            }
        }

        pos = pos + seg.length;
    }

    segments = std::move (newSegments);
    rebuildTrack();
    return true;
}

bool AudioEngine::pasteClipboard (TimePosition insertAt)
{
    if (clipboard.empty())
        return false;

    auto insertion = clampToTimeline (insertAt);
    std::vector<Segment> newSegments;

    auto sortedClipboard = clipboard;
    std::sort (sortedClipboard.begin(), sortedClipboard.end(),
               [] (const ClipboardFragment& a, const ClipboardFragment& b)
               {
                   return a.relativeStart < b.relativeStart;
               });

    auto appendClipboard = [&]
    {
        for (auto& frag : sortedClipboard)
            newSegments.push_back ({ frag.length, frag.sourceOffset });
    };

    auto pos = 0_tp;
    bool inserted = false;

    for (auto& seg : segments)
    {
        const auto segRange = TimeRange { pos, pos + seg.length };

        if (! inserted && insertion < segRange.getEnd())
        {
            auto preLen = insertion - segRange.getStart();
            auto postLen = segRange.getEnd() - insertion;

            if (preLen > 0s)
                newSegments.push_back ({ preLen, seg.sourceOffset });

            appendClipboard();

            if (postLen > 0s)
            {
                auto newOffset = seg.sourceOffset + (seg.length - postLen);
                newSegments.push_back ({ postLen, newOffset });
            }

            inserted = true;
        }
        else
        {
            newSegments.push_back (seg);
        }

        pos = pos + seg.length;
    }

    if (! inserted)
        appendClipboard();

    segments = std::move (newSegments);
    rebuildTrack();
    return true;
}

bool AudioEngine::hasClipboard() const
{
    return ! clipboard.empty();
}

void AudioEngine::pushUndoState (const std::optional<TimeRange>& selection, TimePosition insertion)
{
    if (applyingUndo)
        return;

    UndoState state;
    state.segments = segments;
    state.clipboard = clipboard;
    state.selection = selection;
    state.insertionPoint = insertion;
    state.loadedFile = loadedFile;
    state.loadedFileLength = loadedFileLength;

    undoStack.push_back (std::move (state));
    if (undoStack.size() > maxUndoHistory)
        undoStack.erase (undoStack.begin());
}

bool AudioEngine::undo (std::optional<TimeRange>& selectionOut, TimePosition& insertionOut)
{
    if (undoStack.empty())
        return false;

    applyingUndo = true;
    auto state = std::move (undoStack.back());
    undoStack.pop_back();

    segments = std::move (state.segments);
    clipboard = std::move (state.clipboard);
    selectionOut = state.selection;
    insertionOut = state.insertionPoint;
    loadedFile = state.loadedFile;
    loadedFileLength = state.loadedFileLength;

    if (loadedFile.existsAsFile())
    {
        te::AudioFile audioFile (engine, loadedFile);
        thumbnail = std::make_unique<te::SmartThumbnail> (engine, audioFile, thumbnailComponent, nullptr);
    }
    else
    {
        thumbnail.reset();
    }

    rebuildTrack();
    applyingUndo = false;
    return true;
}

bool AudioEngine::normaliseRange (TimeRange range, juce::String& statusOut)
{
    if (edit == nullptr || track == nullptr || segments.empty())
    {
        statusOut = "Normalise failed: no audio";
        return false;
    }

    auto& transport = edit->getTransport();
    if (transport.isPlaying())
        transport.stop (false, true);
    if (transport.isPlayContextActive())
        transport.freePlaybackContext();

    edit->flushState();

    te::AudioClipBase* regionClip = nullptr;

    
    DBG("AE:norm clips at start " << track->getClips().size());


    for (auto* c : track->getClips())
    {
        if (auto* ac = dynamic_cast<te::AudioClipBase*> (c))
        {
            auto pos = ac->getPosition().time;
            if (pos.getStart() <= range.getStart() && pos.getEnd() >= range.getEnd())
            {
                regionClip = ac;
                break;
            }
        }
    }

    if (regionClip == nullptr)
    {
        statusOut = "Normalise failed: no clip";
        return false;
    }

    auto* clipTrack = dynamic_cast<te::ClipTrack*> (regionClip->getTrack());
    if (clipTrack == nullptr)
    {
        statusOut = "Normalise failed: track";
        return false;
    }

    const auto clipRange = regionClip->getPosition().time;
    if (range.getEnd() < clipRange.getEnd())
        clipTrack->splitClip (*regionClip, range.getEnd());
    if (range.getStart() > clipRange.getStart())
        clipTrack->splitClip (*regionClip, range.getStart());

    regionClip = nullptr;
    for (auto* c : clipTrack->getClips())
    {
        if (auto* ac = dynamic_cast<te::AudioClipBase*> (c))
        {
            if (ac->getPosition().time == range)
            {
                regionClip = ac;
                break;
            }
        }
    }

    if (regionClip == nullptr)
    {
        statusOut = "Normalise failed: region";
        return false;
    }

    regionClip->enableEffects (true, false);
    if (regionClip->getClipEffects() != nullptr)
    {
        // auto vt = te::ClipEffect::create (te::ClipEffect::EffectType::normalise);
        auto vt = te::ClipEffect::create (te::ClipEffect::EffectType::pitchShift);

        regionClip->addEffect (vt);
        regionClip->setEffectsVisible (true);
        regionClip->setGainDB(20.0f);
        statusOut = "Normalise effect added";
        updateDisplayThumbnailFromTrack();
        return true;
    }

    statusOut = "Normalise failed: effects";
    return false;
}

void AudioEngine::rebuildTrack()
{
    if (edit == nullptr || track == nullptr || ! loadedFile.existsAsFile())
        return;

    EngineHelpers::removeAllClips (*track);

    auto name = loadedFile.getFileNameWithoutExtension();
    auto timelinePos = 0_tp;

    for (auto& seg : segments)
    {
        te::ClipPosition pos { { timelinePos, timelinePos + seg.length }, seg.sourceOffset };
        track->insertWaveClip (name, loadedFile, pos, false);
        timelinePos = timelinePos + seg.length;
    }

    edit->getTransport().setLoopRange ({ 0_tp, timelinePos });
    edit->getTransport().ensureContextAllocated();
    updateDisplayThumbnailFromTrack();
}

void AudioEngine::logTrackClipDebugInfo() const
{
    if (track == nullptr)
    {
        DBG ("AudioEngine::logTrackClipDebugInfo: no track");
        return;
    }

    DBG ("AudioEngine::logTrackClipDebugInfo: clip count = " << track->getClips().size());

    for (auto* c : track->getClips())
    {
        DBG ("  raw clip type=" << (c != nullptr ? juce::String (typeid (*c).name()) : "<null>")
             << " name=" << (c != nullptr ? c->getName() : "<null>"));

        if (auto* ac = dynamic_cast<te::AudioClipBase*> (c))
        {
            auto clipPos = ac->getPosition();
            auto pos = clipPos.time;
            auto sourceFile = ac->getSourceFileReference().getFile();
            auto playbackFile = ac->getPlaybackFile().getFile();
            auto* clipEffects = ac->getClipEffects();

            juce::String info;
            info << "    audio clip timeline: " << pos.getStart().inSeconds() << "s -> " << pos.getEnd().inSeconds() << "s"
                 << " sourceOffset: " << clipPos.offset.inSeconds() << "s"
                 << " source: " << (sourceFile.existsAsFile() ? sourceFile.getFullPathName() : "<missing>")
                 << " playback: " << (playbackFile.existsAsFile() ? playbackFile.getFullPathName() : "<missing>");
            DBG (info);

            if (clipEffects != nullptr)
            {
                juce::String effectsInfo;
                effectsInfo << "    effects visible=" << (ac->getEffectsVisible() ? "yes" : "no");

                int idx = 0;
                for (auto* eff : clipEffects->objects)
                {
                    juce::String effectName;
                    if (eff != nullptr)
                        effectName = eff->state[te::IDs::name].toString();
                    if (effectName.isEmpty())
                        effectName = "<unknown>";
                    effectsInfo << "\n      [" << idx++ << "] " << effectName;
                }

                effectsInfo << "\n    effects range: start=" << clipEffects->getEffectsStartTime().inSeconds()
                            << "s len=" << clipEffects->getEffectsLength().inSeconds() << "s";

                DBG (effectsInfo);
            }
        }
    }
}

// void AudioEngine::updateDisplayThumbnailFromTrack()
// {
 

//     if (loadedFile.existsAsFile())
//     {
//         // displayFile = loadedFile;
//         te::AudioFile audioFile (engine, loadedFile);
//         thumbnail = std::make_unique<te::SmartThumbnail> (engine, audioFile, thumbnailComponent, nullptr);
//     }
// }

void AudioEngine::updateDisplayThumbnailFromTrack()
{
 

    if (loadedFile.existsAsFile())
    {
        // displayFile = loadedFile;
        te::AudioFile audioFile (engine, loadedFile);
        thumbnail = std::make_unique<te::SmartThumbnail> (engine, audioFile, thumbnailComponent, nullptr);
    }
}

AudioEngine::TimePosition AudioEngine::clampToTimeline (TimePosition pos) const
{
    auto total = getTotalLength();
    if (total <= 0s)
        return 0_tp;

    return TimePosition::fromSeconds (juce::jlimit (0.0, total.inSeconds(), pos.inSeconds()));
}
