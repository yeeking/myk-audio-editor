/*
    Minimal non-destructive audio editor component.
    Reuses helper utilities from examples/common.
*/

#pragma once

#include <JuceHeader.h>
#include "../libs/tracktion_engine/examples/common/Utilities.h"
#include "../libs/tracktion_engine/examples/common/Components.h"
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
    NonDestructiveEditorComponent (IAudioEngine& engineInterface, IAudioExporter& exporterInterface)
        : audioEngine (engineInterface),
          audioExporter (exporterInterface),
          engine (engineInterface.getEngine()),
          waveformView (*this)
    {
        addAndMakeVisible (loadButton);
        addAndMakeVisible (playPauseButton);
        addAndMakeVisible (exportButton);
        addAndMakeVisible (copyButton);
        addAndMakeVisible (cutButton);
        addAndMakeVisible (pasteButton);
        addAndMakeVisible (selectionLabel);
        addAndMakeVisible (statusLabel);
        addAndMakeVisible (waveformView);

        selectionLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::lightgreen);

        setupCallbacks();
        createNewEdit();

        setSize (900, 540);
        setWantsKeyboardFocus (true);
        startTimerHz (20);
    }

    ~NonDestructiveEditorComponent() override
    {
        if (edit != nullptr)
            edit->getTransport().removeChangeListener (this);

        engine.getTemporaryFileManager().getTempDirectory().deleteRecursively();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (6);
        auto top = r.removeFromTop (32);
        const int buttonW = 120;

        loadButton.setBounds (top.removeFromLeft (buttonW).reduced (2));
        playPauseButton.setBounds (top.removeFromLeft (buttonW).reduced (2));
        exportButton.setBounds (top.removeFromLeft (buttonW).reduced (2));
        copyButton.setBounds (top.removeFromLeft (buttonW / 2).reduced (2));
        cutButton.setBounds (top.removeFromLeft (buttonW / 2).reduced (2));
        pasteButton.setBounds (top.removeFromLeft (buttonW / 2).reduced (2));

        auto info = r.removeFromTop (26);
        selectionLabel.setBounds (info.removeFromLeft (r.getWidth() / 2));
        statusLabel.setBounds (info);

        waveformView.setBounds (r.reduced (2));
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        if (key.getKeyCode() == juce::KeyPress::leftKey)
        {
            startScrub (Direction::backward);
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::rightKey)
        {
            startScrub (Direction::forward);
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::homeKey)
        {
            auto target = selection ? selection->getStart() : 0_tp;
            setInsertionPoint (target);
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::endKey)
        {
            auto target = selection ? selection->getEnd() : TimePosition::fromSeconds (getTotalLength().inSeconds());
            setInsertionPoint (target);
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::pageDownKey)
        {
            zoomAroundPlayhead (true);
            return true;
        }

        if (key.getKeyCode() == juce::KeyPress::pageUpKey)
        {
            zoomAroundPlayhead (false);
            return true;
        }

        if (key == juce::KeyPress::spaceKey)
        {
            togglePlay();
            return true;
        }

        const bool command = key.getModifiers().isCommandDown();

        if (command && key.getKeyCode() == 'O')
        {
            loadFromChooser();
            return true;
        }

        if (command && key.getKeyCode() == 'C')   { copySelection(); return true; }
        if (command && key.getKeyCode() == 'X')   { cutSelection(); return true; }
        if (command && key.getKeyCode() == 'V')   { pasteClipboard(); return true; }

        return false;
    }

    bool keyStateChanged (bool) override
    {
        auto leftDown = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::leftKey);
        auto rightDown = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::rightKey);

        if (leftDown && ! rightDown)
            startScrub (Direction::backward);
        else if (rightDown && ! leftDown)
            startScrub (Direction::forward);
        else
            stopScrub();

        return false;
    }

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
        explicit WaveformView (NonDestructiveEditorComponent& ownerRef) : owner (ownerRef) {}

        void paint (juce::Graphics& g) override
        {
            g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId).darker (0.15f));

            auto bounds = getLocalBounds().reduced (6);
            g.setColour (juce::Colours::grey);
            g.drawRect (bounds);

            auto header = bounds.removeFromTop (22);
            auto waveformBounds = bounds;

            const auto viewRange = owner.getViewRange();
            const auto viewSeconds = viewRange.getLength().inSeconds();
            auto* thumb = owner.getThumbnail();

            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawFittedText (owner.hasLoadedFile() ? owner.getLoadedFileName() : "No file loaded",
                              header, juce::Justification::centredLeft, 1);

            if (viewSeconds <= 0.0 || owner.segments.empty())
            {
                g.setColour (juce::Colours::grey);
                g.drawText ("Load an audio file to view its waveform", waveformBounds, juce::Justification::centred);
                return;
            }

            if (thumb == nullptr)
            {
                g.setColour (juce::Colours::grey);
                g.drawText ("Waveform unavailable", waveformBounds, juce::Justification::centred);
                return;
            }

            TimePosition cursor { 0_tp };
            for (auto& seg : owner.segments)
            {
                auto segRange = TimeRange { cursor, cursor + seg.length };
                auto visible = segRange.getIntersectionWith (viewRange);
                if (visible.isEmpty())
                {
                    cursor = cursor + seg.length;
                    continue;
                }

                auto segStartPx = int (((visible.getStart().inSeconds() - viewRange.getStart().inSeconds()) / viewSeconds) * waveformBounds.getWidth()) + waveformBounds.getX();
                auto segEndPx   = int (((visible.getEnd().inSeconds()   - viewRange.getStart().inSeconds()) / viewSeconds) * waveformBounds.getWidth()) + waveformBounds.getX();
                auto segRect    = juce::Rectangle<int> (segStartPx, waveformBounds.getY(), juce::jmax (6, segEndPx - segStartPx), waveformBounds.getHeight());

                g.setColour (juce::Colours::darkslategrey.withAlpha (0.35f));
                g.fillRect (segRect);

                const auto fileRange = TimeRange {
                    TimePosition::fromSeconds ((seg.sourceOffset + (visible.getStart() - segRange.getStart())).inSeconds()),
                    TimePosition::fromSeconds ((seg.sourceOffset + (visible.getEnd()   - segRange.getStart())).inSeconds())
                };
                g.setColour (juce::Colours::lightblue);
                thumb->drawChannels (g, segRect, fileRange, 1.1f);

                g.setColour (juce::Colours::darkgrey);
                g.drawRect (segRect);
                cursor = cursor + seg.length;
            }

            if (auto sel = owner.selection)
            {
                auto selRect = timeRangeToRect (*sel, waveformBounds, viewRange);
                g.setColour (juce::Colours::yellow.withAlpha (0.25f));
                g.fillRect (selRect);
                g.setColour (juce::Colours::yellow);
                g.drawRect (selRect, 2);
            }

            drawPlayhead (g, waveformBounds, owner.edit != nullptr ? owner.edit->getTransport().getPosition() : 0_tp,
                          juce::Colours::cyan, viewRange);
            drawPlayhead (g, waveformBounds, owner.insertionPoint,
                          juce::Colours::lightgreen.withAlpha (0.9f), viewRange);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (owner.segments.empty())
                return;

            auto bounds = getWaveformBounds();
            auto viewRange = owner.getViewRange();
            auto clickTime = xToTime (e.position.x, bounds, viewRange);

            if (e.mods.isRightButtonDown())
            {
                owner.setInsertionPoint (clickTime);
                return;
            }

            if (! e.mods.isLeftButtonDown())
                return;

            dragMode = DragMode::create;
            dragStart = clickTime;

            if (owner.selection)
            {
                auto startX = timeToX (owner.selection->getStart(), bounds, viewRange);
                auto endX   = timeToX (owner.selection->getEnd(),   bounds, viewRange);
                auto distStart = std::abs (e.position.x - startX);
                auto distEnd   = std::abs (e.position.x - endX);
                const float handleRadius = 8.0f;

                if (distStart <= handleRadius)
                {
                    dragMode = DragMode::adjustStart;
                    dragAnchor = owner.selection->getEnd();
                }
                else if (distEnd <= handleRadius)
                {
                    dragMode = DragMode::adjustEnd;
                    dragAnchor = owner.selection->getStart();
                }
            }

            dragging = true;

            if (dragMode == DragMode::create)
            {
                owner.beginSelection (dragStart);
            }
            else if (dragMode == DragMode::adjustStart)
            {
                owner.updateSelection (clickTime, dragAnchor);
            }
            else if (dragMode == DragMode::adjustEnd)
            {
                owner.updateSelection (dragAnchor, clickTime);
            }

            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (! dragging || owner.segments.empty())
                return;

            auto bounds = getWaveformBounds();
            auto viewRange = owner.getViewRange();
            auto currentTime = xToTime (e.position.x, bounds, viewRange);

            if (dragMode == DragMode::create)
            {
                owner.updateSelection (dragStart, currentTime);
            }
            else if (dragMode == DragMode::adjustStart)
            {
                owner.updateSelection (currentTime, dragAnchor);
            }
            else if (dragMode == DragMode::adjustEnd)
            {
                owner.updateSelection (dragAnchor, currentTime);
            }

            repaint();
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! dragging || owner.segments.empty())
                return;

            auto bounds = getWaveformBounds();
            auto viewRange = owner.getViewRange();
            auto clicked = xToTime (e.position.x, bounds, viewRange);
            dragging = false;

            if (! e.mouseWasDraggedSinceMouseDown() && dragMode == DragMode::create)
            {
                owner.setInsertionPoint (clicked);
                owner.clearSelection();
            }
            else
            {
                owner.finaliseSelection();
                if (owner.selection)
                    owner.setInsertionPoint (owner.selection->getStart());
                else
                    owner.setInsertionPoint (clicked);
            }

            dragMode = DragMode::create;
            repaint();
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
        {
            if (wheel.deltaY == 0.0f)
                return;

            owner.zoomAroundPlayhead (wheel.deltaY > 0.0f);
            repaint();
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            updateCursorForPosition (e.position.x);
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
        }

    private:
        NonDestructiveEditorComponent& owner;
        TimePosition dragStart {};
        bool dragging = false;
        TimePosition dragAnchor {};
        enum class DragMode { create, adjustStart, adjustEnd };
        DragMode dragMode { DragMode::create };

        juce::Rectangle<int> timeRangeToRect (TimeRange tr, juce::Rectangle<int> bounds, TimeRange viewRange) const
        {
            const auto viewSeconds = viewRange.getLength().inSeconds();
            if (viewSeconds <= 0.0)
                return { bounds.getX(), bounds.getY(), 0, bounds.getHeight() };

            auto startPx = bounds.getX() + int (((tr.getStart().inSeconds() - viewRange.getStart().inSeconds()) / viewSeconds) * bounds.getWidth());
            auto endPx   = bounds.getX() + int (((tr.getEnd().inSeconds()   - viewRange.getStart().inSeconds()) / viewSeconds) * bounds.getWidth());
            return { startPx, bounds.getY(), juce::jmax (2, endPx - startPx), bounds.getHeight() };
        }

        void drawPlayhead (juce::Graphics& g, juce::Rectangle<int> bounds, TimePosition pos, juce::Colour colour, TimeRange viewRange) const
        {
            const auto viewSeconds = viewRange.getLength().inSeconds();
            if (viewSeconds <= 0.0)
                return;

            const auto x = bounds.getX() + int (((pos.inSeconds() - viewRange.getStart().inSeconds()) / viewSeconds) * bounds.getWidth());
            g.setColour (colour);
            g.drawLine (float (x), float (bounds.getY()), float (x), float (bounds.getBottom()), 2.0f);
        }

        TimePosition xToTime (float x, juce::Rectangle<int> bounds, TimeRange viewRange) const
        {
            const auto viewSeconds = viewRange.getLength().inSeconds();
            if (viewSeconds <= 0.0 || bounds.getWidth() <= 0)
                return 0_tp;

            const auto norm = juce::jlimit (0.0f, 1.0f, (x - float (bounds.getX())) / float (bounds.getWidth()));
            return TimePosition::fromSeconds (viewRange.getStart().inSeconds() + viewSeconds * norm);
        }

        float timeToX (TimePosition t, juce::Rectangle<int> bounds, TimeRange viewRange) const
        {
            const auto viewSeconds = viewRange.getLength().inSeconds();
            if (viewSeconds <= 0.0 || bounds.getWidth() <= 0)
                return float (bounds.getX());

            const auto norm = (t.inSeconds() - viewRange.getStart().inSeconds()) / viewSeconds;
            return float (bounds.getX()) + float (bounds.getWidth()) * float (norm);
        }

        juce::Rectangle<int> getWaveformBounds() const
        {
            auto bounds = getLocalBounds().reduced (6);
            bounds.removeFromTop (22);
            return bounds;
        }

        void updateCursorForPosition (float mouseX)
        {
            if (! owner.selection)
            {
                setMouseCursor (juce::MouseCursor::NormalCursor);
                return;
            }

            auto bounds = getWaveformBounds();
            auto viewRange = owner.getViewRange();
            auto startX = timeToX (owner.selection->getStart(), bounds, viewRange);
            auto endX   = timeToX (owner.selection->getEnd(),   bounds, viewRange);
            const float handleRadius = 8.0f;

            if (std::abs (mouseX - startX) <= handleRadius
                || std::abs (mouseX - endX) <= handleRadius)
                setMouseCursor (juce::MouseCursor::LeftRightResizeCursor);
            else
                setMouseCursor (juce::MouseCursor::NormalCursor);
        }
    };

    // Timeline view removed; waveform view now handles selection and playhead overlays.

    void setupCallbacks()
    {
        loadButton.onClick = [this] { loadFromChooser(); };
        exportButton.onClick = [this] { exportToFile(); };
        playPauseButton.onClick = [this] { togglePlay(); };
        copyButton.onClick = [this] { copySelection(); };
        cutButton.onClick = [this] { cutSelection(); };
        pasteButton.onClick = [this] { pasteClipboard(); };
    }

    void createNewEdit()
    {
        edit = audioEngine.createEmptyEdit ("nonDestructiveMVP");
        edit->getTransport().addChangeListener (this);
        track = EngineHelpers::getOrInsertAudioTrackAt (*edit, 0);
        selection.reset();
        clipboard.clear();
        segments.clear();
        thumbnail.reset();
        loadedFile = juce::File();
        loadedFileLength = 0s;
        updateSelectionLabel();
        waveformView.repaint();
        statusLabel.setText ("New blank edit created", juce::dontSendNotification);
    }

    void loadFromChooser()
    {
        EngineHelpers::browseForAudioFile (engine, [this] (const juce::File& f)
        {
            if (f.existsAsFile())
                loadFile (f);
        });
    }

    void loadFile (const juce::File& file)
    {
        te::AudioFile audioFile (engine, file);

        if (! audioFile.isValid())
        {
            statusLabel.setText ("Unsupported audio file", juce::dontSendNotification);
            return;
        }

        auto length = TimeDuration::fromSeconds (audioFile.getLength());
        loadedFile = file;
        loadedFileLength = length;
        segments.clear();
        clipboard.clear();
        selection.reset();
        insertionPoint = 0_tp;

        segments.push_back ({ length, 0s });

        thumbnail = std::make_unique<te::SmartThumbnail> (engine, audioFile, *this, nullptr);
        setViewToWholeFile();
        rebuildTrack();
        setInsertionPoint (0_tp);
        lastPlayStart = 0_tp;

        statusLabel.setText ("Loaded " + file.getFileName(), juce::dontSendNotification);
        updateSelectionLabel();
        waveformView.repaint();
    }

    void rebuildTrack()
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
        clampViewToTotal();
    }

    void togglePlay()
    {
        if (edit != nullptr)
        {
            auto& transport = edit->getTransport();
            if (! transport.isPlaying())
                lastPlayStart = transport.getPosition();
            EngineHelpers::togglePlay (*edit, EngineHelpers::ReturnToStart::no);
        }
    }

    void addPlugin()
    {
    }

    void beginSelection (TimePosition start)
    {
        selection = TimeRange { start, start };
        insertionPoint = start;
        edit->getTransport().setPosition (start);
        updateSelectionLabel();
        repaintViews();
    }

    void updateSelection (TimePosition start, TimePosition end)
    {
        auto clampedStart = clampToTimeline (start);
        auto clampedEnd   = clampToTimeline (end);
        auto s = juce::jmin (clampedStart, clampedEnd);
        auto e = juce::jmax (clampedStart, clampedEnd);
        selection = TimeRange { s, e };
        updateSelectionLabel();
        repaintViews();
    }

    void finaliseSelection()
    {
        if (selection && selection->getLength() < 0.01s)
            selection.reset();

        updateSelectionLabel();
        repaintViews();
    }

    void clearSelection()
    {
        selection.reset();
        updateSelectionLabel();
        repaintViews();
    }

    void setInsertionPoint (TimePosition pos)
    {
        insertionPoint = clampToTimeline (pos);

        if (edit != nullptr)
            edit->getTransport().setPosition (insertionPoint);

        repaintViews();
    }

    void startScrub (Direction d)
    {
        if (segments.empty() || edit == nullptr)
            return;

        if ((d == Direction::backward && scrubLeft) || (d == Direction::forward && scrubRight))
            return;

        scrubLeft = (d == Direction::backward);
        scrubRight = (d == Direction::forward);
        scrubStartMs = juce::Time::getMillisecondCounterHiRes();

        if (! edit->getTransport().isPlaying())
            edit->getTransport().play (false);
    }

    void stopScrub()
    {
        scrubLeft = scrubRight = false;
    }

    void copySelection()
    {
        if (! selection)
            return;

        clipboard.clear();
        auto sel = selection->isEmpty() ? TimeRange { selection->getStart(), selection->getStart() } : *selection;
        auto total = getTotalLength();
        sel = sel.getIntersectionWith ({ 0_tp, TimePosition::fromSeconds (total.inSeconds()) });

        auto pos = 0_tp;
        for (auto& seg : segments)
        {
            const auto segRange = TimeRange { pos, pos + seg.length };
            const auto intersection = segRange.getIntersectionWith (sel);

            if (! intersection.isEmpty())
            {
                auto relStart = intersection.getStart() - sel.getStart();
                auto offset = seg.sourceOffset + (intersection.getStart() - segRange.getStart());
                clipboard.push_back ({ relStart, intersection.getLength(), offset });
            }

            pos = pos + seg.length;
        }

        statusLabel.setText ("Copied selection", juce::dontSendNotification);
    }

    void cutSelection()
    {
        copySelection();
        if (clipboard.empty() || ! selection)
            return;

        std::vector<Segment> newSegments;
        auto sel = *selection;
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
        selection.reset();
        updateSelectionLabel();
        rebuildTrack();
        repaintViews();
        statusLabel.setText ("Cut selection (ripple)", juce::dontSendNotification);
    }

    void pasteClipboard()
    {
        if (clipboard.empty())
            return;

        auto insertAt = clampToTimeline (insertionPoint);
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

            if (! inserted && insertAt < segRange.getEnd())
            {
                auto preLen = insertAt - segRange.getStart();
                auto postLen = segRange.getEnd() - insertAt;

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
        selection.reset();
        updateSelectionLabel();
        rebuildTrack();
        repaintViews();
        statusLabel.setText ("Pasted clipboard at playhead", juce::dontSendNotification);
    }

    void exportToFile()
    {
        if (edit == nullptr || segments.empty())
            return;

        // Ensure playback and contexts are stopped before exporting to avoid active play contexts.
        auto& transport = edit->getTransport();
        if (transport.isPlaying())
            transport.stop (false, true);

        // Explicitly free playback context to satisfy renderer assertion.
        if (transport.isPlayContextActive())
            transport.freePlaybackContext();

        const bool hasSelection = selection && selection->getLength() > 0s;
        const auto fullRange = TimeRange { 0_tp, TimePosition::fromSeconds (getTotalLength().inSeconds()) };
        const auto selectionRange = hasSelection ? *selection : fullRange;

        ExportContext ctx;
        ctx.engine = &engine;
        ctx.edit = edit.get();
        ctx.selectionRange = selectionRange;
        ctx.fullRange = fullRange;
        ctx.hasSelection = hasSelection;
        ctx.defaultName = loadedFile.existsAsFile() ? loadedFile.getFileNameWithoutExtension() : juce::String ("Export");
        ctx.setStatus = [this] (const juce::String& text) { statusLabel.setText (text, juce::dontSendNotification); };

        audioExporter.showExportDialog (ctx);
    }

    TimeDuration getTotalLength() const
    {
        TimeDuration total { 0s };
        for (auto& seg : segments)
            total = total + seg.length;
        return total;
    }

    TimeRange getViewRange() const
    {
        auto total = getTotalLength();
        auto len = viewDuration > 0s ? viewDuration : total;
        len = juce::jlimit (minViewDuration, total > 0s ? total : minViewDuration, len);

        auto maxStart = total > len ? total - len : 0s;
        auto start = juce::jlimit (0.0, maxStart.inSeconds(), viewStart.inSeconds());
        return { TimePosition::fromSeconds (start), TimePosition::fromSeconds (start + len.inSeconds()) };
    }

    void setViewToWholeFile()
    {
        auto total = getTotalLength();
        if (total <= 0s)
        {
            viewStart = 0_tp;
            viewDuration = minViewDuration;
            return;
        }

        viewStart = 0_tp;
        viewDuration = total;
    }

    void clampViewToTotal()
    {
        auto total = getTotalLength();
        if (total <= 0s)
        {
            viewStart = 0_tp;
            viewDuration = minViewDuration;
            return;
        }

        viewDuration = juce::jlimit (minViewDuration, total, viewDuration > 0s ? viewDuration : total);
        auto maxStart = total - viewDuration;
        viewStart = TimePosition::fromSeconds (juce::jlimit (0.0, maxStart.inSeconds(), viewStart.inSeconds()));
    }

    void zoomAroundPlayhead (bool zoomIn)
    {
        auto total = getTotalLength();
        if (total <= 0s)
            return;

        auto currentRange = getViewRange();
        auto len = currentRange.getLength();
        auto playhead = edit != nullptr ? edit->getTransport().getPosition() : insertionPoint;

        double scale = zoomIn ? 0.8 : 1.25;
        auto newLen = TimeDuration::fromSeconds (juce::jlimit (minViewDuration.inSeconds(), total.inSeconds(), len.inSeconds() * scale));

        auto newStart = playhead - (newLen * 0.5);
        newStart = TimePosition::fromSeconds (juce::jlimit (0.0, juce::jmax (0.0, total.inSeconds() - newLen.inSeconds()), newStart.inSeconds()));

        viewStart = newStart;
        viewDuration = newLen;
        repaintViews();
    }

    void handleScrub()
    {
        if (! edit || (! scrubLeft && ! scrubRight))
            return;

        auto nowMs = juce::Time::getMillisecondCounterHiRes();
        auto holdSeconds = (nowMs - scrubStartMs) / 1000.0;
        auto speedFactor = juce::jlimit (0.3, 3.0, 0.5 + holdSeconds);
        auto direction = scrubRight ? 1.0 : -1.0;

        auto delta = TimeDuration::fromSeconds (timerIntervalSeconds * speedFactor * direction);
        auto currentPos = edit->getTransport().getPosition();
        setInsertionPoint (currentPos + delta);
    }

    void updateViewForPlayback()
    {
        if (edit == nullptr || ! edit->getTransport().isPlaying())
            return;

        auto total = getTotalLength();
        if (total <= 0s)
            return;

        auto view = getViewRange();
        if (view.getLength() >= total)
            return; // full view already visible

        auto playhead = edit->getTransport().getPosition();
        auto newStart = playhead - (view.getLength() * 0.5);
        newStart = TimePosition::fromSeconds (juce::jlimit (0.0,
                                                            juce::jmax (0.0, total.inSeconds() - view.getLength().inSeconds()),
                                                            newStart.inSeconds()));
        viewStart = newStart;
    }

    bool hasLoadedFile() const
    {
        return loadedFile.existsAsFile();
    }

    TimeDuration getLoadedFileLength() const
    {
        return loadedFileLength;
    }

    juce::String getLoadedFileName() const
    {
        return loadedFile.getFileName();
    }

    te::SmartThumbnail* getThumbnail() const
    {
        return thumbnail.get();
    }

    static std::unique_ptr<juce::AudioFormat> createFormat (const juce::String& name)
    {
        auto lower = name.toLowerCase();
        if (lower.contains ("aiff"))
            return std::make_unique<juce::AiffAudioFormat>();
        if (lower.contains ("flac"))
            return std::make_unique<juce::FlacAudioFormat>();
        if (lower.contains ("ogg"))
            return std::make_unique<juce::OggVorbisAudioFormat>();
        if (lower.contains ("mp3"))
        {
           #if JUCE_USE_MP3AUDIOFORMAT
            return std::make_unique<juce::MP3AudioFormat>();
           #elif JUCE_MAC
            return std::make_unique<juce::CoreAudioFormat>();
           #else
            return nullptr;
           #endif
        }
        if (lower.contains ("m4a"))
        {
           #if JUCE_MAC || JUCE_IOS
            return std::make_unique<juce::CoreAudioFormat>();
           #else
            return nullptr;
           #endif
        }

        return std::make_unique<juce::WavAudioFormat>();
    }

    TimePosition clampToTimeline (TimePosition pos) const
    {
        auto total = getTotalLength();
        if (total <= 0s)
            return 0_tp;

        return TimePosition::fromSeconds (juce::jlimit (0.0, total.inSeconds(), pos.inSeconds()));
    }

    void updateSelectionLabel()
    {
        if (selection)
        {
            const auto len = selection->getLength().inSeconds();
            selectionLabel.setText ("Selection: " + juce::String (selection->getStart().inSeconds(), 2)
                                    + "s to " + juce::String (selection->getEnd().inSeconds(), 2)
                                    + "s (" + juce::String (len, 2) + "s)",
                                    juce::dontSendNotification);
        }
        else
        {
            selectionLabel.setText ("Selection: none", juce::dontSendNotification);
        }
    }

    juce::String describeRange (TimeRange range) const
    {
        return juce::String (range.getStart().inSeconds(), 2) + "s to "
             + juce::String (range.getEnd().inSeconds(), 2) + "s ("
             + juce::String (range.getLength().inSeconds(), 2) + "s)";
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        const bool playing = edit != nullptr && edit->getTransport().isPlaying();
        playPauseButton.setButtonText (playing ? "Pause" : "Play");
    }

    void timerCallback() override
    {
        updateViewForPlayback();
        waveformView.repaint();

        handlePlaybackEnd();
        handleScrub();
    }

    void repaintViews()
    {
        waveformView.repaint();
    }

    void handlePlaybackEnd()
    {
        if (edit == nullptr)
            return;

        auto& transport = edit->getTransport();
        if (! transport.isPlaying())
            return;

        auto total = getTotalLength();
        if (total <= 0s)
            return;

        auto endPos = TimePosition::fromSeconds (total.inSeconds());
        auto pos = transport.getPosition();

        if (pos >= endPos)
        {
            transport.stop (false, false);
            setInsertionPoint (lastPlayStart);
        }
    }

    IAudioEngine& audioEngine;
    IAudioExporter& audioExporter;
    te::Engine& engine;
    std::unique_ptr<te::Edit> edit;
    te::AudioTrack* track = nullptr;
    juce::File loadedFile;
    TimeDuration loadedFileLength { 0s };
    std::unique_ptr<te::SmartThumbnail> thumbnail;
    TimePosition viewStart { 0_tp };
    TimeDuration viewDuration { 0s };
    const TimeDuration minViewDuration { 0.05s };
    bool scrubLeft = false, scrubRight = false;
    double scrubStartMs = 0.0;
    const double timerIntervalSeconds = 1.0 / 20.0;

    std::vector<Segment> segments;
    std::vector<ClipboardFragment> clipboard;
    std::optional<TimeRange> selection;
    TimePosition insertionPoint { 0_tp };
    TimePosition lastPlayStart { 0_tp };

    juce::TextButton loadButton { "Load Audio" }, playPauseButton { "Play" }, exportButton { "Export" },
                     copyButton { "Copy" }, cutButton { "Cut" }, pasteButton { "Paste" };
    juce::Label selectionLabel, statusLabel;
    WaveformView waveformView;
};
