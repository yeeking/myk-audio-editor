#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>

namespace te = tracktion;

class IAudioEngine
{
public:
    virtual ~IAudioEngine() = default;

    virtual te::Engine& getEngine() = 0;
    virtual std::unique_ptr<te::Edit> createEmptyEdit (const juce::String& tempName) = 0;
};

class AudioEngine : public IAudioEngine
{
public:
    AudioEngine();
    ~AudioEngine() override = default;

    te::Engine& getEngine() override;
    std::unique_ptr<te::Edit> createEmptyEdit (const juce::String& tempName) override;

private:
    te::Engine engine;
};
