#pragma once

#include <JuceHeader.h>
#include <tracktion_engine/tracktion_engine.h>
#include <optional>
#include <vector>

namespace te = tracktion;

class IAudioEngine
{
public:
    virtual ~IAudioEngine() = default;

    virtual te::Engine& getEngine() = 0;
    virtual te::Edit* getEdit() = 0;
    virtual te::AudioTrack* getTrack() = 0;

    virtual bool createNewEdit (const juce::String& tempName) = 0;
    virtual bool loadFile (const juce::File& file, juce::String& statusOut) = 0;

    using TimeRange = te::TimeRange;
    using TimePosition = te::TimePosition;
    using TimeDuration = te::TimeDuration;

    struct Segment
    {
        TimeDuration length {};
        TimeDuration sourceOffset {};
    };

    struct ClipboardFragment
    {
        TimeDuration relativeStart {};
        TimeDuration length {};
        TimeDuration sourceOffset {};
    };

    virtual const std::vector<Segment>& getSegments() const = 0;
    virtual TimeDuration getTotalLength() const = 0;
    virtual te::SmartThumbnail* getThumbnail() const = 0;
    virtual juce::File getDisplayFile() const = 0;

    virtual void setInsertionPoint (TimePosition pos) = 0;
    virtual TimePosition getInsertionPoint() const = 0;

    virtual bool copySelection (TimeRange selection) = 0;
    virtual bool cutSelection (TimeRange selection) = 0;
    virtual bool pasteClipboard (TimePosition insertAt) = 0;
    virtual bool hasClipboard() const = 0;

    virtual void pushUndoState (const std::optional<TimeRange>& selection, TimePosition insertion) = 0;
    virtual bool undo (std::optional<TimeRange>& selectionOut, TimePosition& insertionOut) = 0;

    virtual bool normaliseRange (TimeRange range, juce::String& statusOut) = 0;
};

class AudioEngine : public IAudioEngine
{
public:
    AudioEngine();
    ~AudioEngine() override = default;

    te::Engine& getEngine() override;
    te::Edit* getEdit() override;
    te::AudioTrack* getTrack() override;

    bool createNewEdit (const juce::String& tempName) override;
    bool loadFile (const juce::File& file, juce::String& statusOut) override;

    const std::vector<Segment>& getSegments() const override;
    TimeDuration getTotalLength() const override;
    te::SmartThumbnail* getThumbnail() const override;
    juce::File getDisplayFile() const override;

    void setInsertionPoint (TimePosition pos) override;
    TimePosition getInsertionPoint() const override;

    bool copySelection (TimeRange selection) override;
    bool cutSelection (TimeRange selection) override;
    bool pasteClipboard (TimePosition insertAt) override;
    bool hasClipboard() const override;

    void pushUndoState (const std::optional<TimeRange>& selection, TimePosition insertion) override;
    bool undo (std::optional<TimeRange>& selectionOut, TimePosition& insertionOut) override;

    bool normaliseRange (TimeRange range, juce::String& statusOut) override;

private:
    void rebuildTrack();
    void updateDisplayThumbnailFromTrack();
    TimePosition clampToTimeline (TimePosition pos) const;
    void logTrackClipDebugInfo() const;

    te::Engine engine;
    std::unique_ptr<te::Edit> edit;
    te::AudioTrack* track = nullptr;
    juce::File loadedFile;
    juce::File displayFile;
    TimeDuration loadedFileLength {};
    juce::Component thumbnailComponent;
    std::unique_ptr<te::SmartThumbnail> thumbnail;
    TimePosition insertionPoint {};

    std::vector<Segment> segments;
    std::vector<ClipboardFragment> clipboard;

    struct UndoState
    {
        std::vector<Segment> segments;
        std::vector<ClipboardFragment> clipboard;
        std::optional<TimeRange> selection;
        TimePosition insertionPoint {};
        juce::File loadedFile;
        TimeDuration loadedFileLength {};
    };

    std::vector<UndoState> undoStack;
    const size_t maxUndoHistory = 25;
    bool applyingUndo = false;
};
