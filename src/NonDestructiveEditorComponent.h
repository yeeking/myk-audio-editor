/*
    Minimal non-destructive audio editor component.
    Reuses helper utilities from examples/common.
*/

#pragma once

#include <JuceHeader.h>
#include "../libs/tracktion_engine/examples/common/Utilities.h"
#include "AudioEngine.h"
#include "AudioExporter.h"
#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

using namespace tracktion::literals;
using namespace std::literals;
namespace te = tracktion;

class NonDestructiveEditorComponent  : public juce::Component,
                                       private juce::ChangeListener,
                                       private juce::Timer
{
public:
    NonDestructiveEditorComponent (IAudioEngine& engineInterface, IAudioExporter& exporterInterface);
    ~NonDestructiveEditorComponent() override;

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& key) override;
    bool keyStateChanged (bool isKeyDown) override;

private:
    using TimeRange = te::TimeRange;
    using TimePosition = te::TimePosition;
    using TimeDuration = te::TimeDuration;
    enum class Direction { forward, backward };

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

    class WaveformView  : public juce::Component
    {
    public:
        explicit WaveformView (NonDestructiveEditorComponent& ownerRef);

        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent& e) override;
        void mouseDrag (const juce::MouseEvent& e) override;
        void mouseUp (const juce::MouseEvent& e) override;
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent& e) override;

    private:
        NonDestructiveEditorComponent& owner;
        TimePosition dragStart {};
        bool dragging = false;
        TimePosition dragAnchor {};
        enum class DragMode { create, adjustStart, adjustEnd };
        DragMode dragMode { DragMode::create };

        juce::Rectangle<int> timeRangeToRect (TimeRange tr, juce::Rectangle<int> bounds, TimeRange viewRange) const;
        void drawPlayhead (juce::Graphics& g, juce::Rectangle<int> bounds, TimePosition pos, juce::Colour colour, TimeRange viewRange) const;
        TimePosition xToTime (float x, juce::Rectangle<int> bounds, TimeRange viewRange) const;
        float timeToX (TimePosition t, juce::Rectangle<int> bounds, TimeRange viewRange) const;
        juce::Rectangle<int> getWaveformBounds() const;
        void updateCursorForPosition (float mouseX);
    };

    void setupCallbacks();
    void createNewEdit();
    void loadFromChooser();
    void loadFile (const juce::File& file, bool pushUndo = true);
    void togglePlay();
    void addPlugin();
    void beginSelection (TimePosition start);
    void updateSelection (TimePosition start, TimePosition end);
    void finaliseSelection();
    void clearSelection();
    void setInsertionPoint (TimePosition pos);
    void startScrub (Direction d);
    void stopScrub();
    void copySelection();
    void cutSelection();
    void pasteClipboard();
    void exportToFile();
    void pushUndoState();
    void undoLastAction();
    void normaliseAudio();

    TimeDuration getTotalLength() const;
    TimeRange getViewRange() const;
    void setViewToWholeFile();
    void clampViewToTotal();
    void zoomAroundPlayhead (bool zoomIn);
    void handleScrub();
    void updateViewForPlayback();
    bool hasLoadedFile() const;
    TimeDuration getLoadedFileLength() const;
    juce::String getLoadedFileName() const;
    te::SmartThumbnail* getThumbnail() const;
    static std::unique_ptr<juce::AudioFormat> createFormat (const juce::String& name);
    TimePosition clampToTimeline (TimePosition pos) const;
    void updateSelectionLabel();
    juce::String describeRange (TimeRange range) const;
    const std::vector<IAudioEngine::Segment>& getSegments() const;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void repaintViews();
    void handlePlaybackEnd();

    IAudioEngine& audioEngine;
    IAudioExporter& audioExporter;
    te::Engine& engine;
    te::Edit* edit = nullptr;
    TimePosition viewStart { 0_tp };
    TimeDuration viewDuration { 0s };
    const TimeDuration minViewDuration { 0.05s };
    bool scrubLeft = false, scrubRight = false;
    double scrubStartMs = 0.0;
    const double timerIntervalSeconds = 1.0 / 20.0;

    std::optional<TimeRange> selection;
    TimePosition insertionPoint { 0_tp };
    TimePosition lastPlayStart { 0_tp };

    juce::TextButton loadButton { "Load Audio" }, playPauseButton { "Play" }, exportButton { "Export" },
                     normaliseButton { "Normalise" }, undoButton { "Undo" }, copyButton { "Copy" }, cutButton { "Cut" }, pasteButton { "Paste" };
    juce::Label selectionLabel, statusLabel;
    WaveformView waveformView;
};
