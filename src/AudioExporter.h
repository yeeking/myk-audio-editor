#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include "AudioEngine.h"

namespace te = tracktion;

struct ExportContext
{
    te::Engine* engine = nullptr;
    te::Edit* edit = nullptr;
    te::TimeRange selectionRange {};
    te::TimeRange fullRange {};
    bool hasSelection = false;
    juce::String defaultName;
    std::function<void (const juce::String&)> setStatus;
};

class IAudioExporter
{
public:
    virtual ~IAudioExporter() = default;
    virtual void showExportDialog (const ExportContext& context) = 0;
};

class AudioExporter : public IAudioExporter
{
public:
    AudioExporter() = default;
    ~AudioExporter() override = default;

    void showExportDialog (const ExportContext& context) override;
};
