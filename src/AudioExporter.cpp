#include "AudioExporter.h"

namespace
{
    std::unique_ptr<juce::AudioFormat> createFormatFromName (const juce::String& name)
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

    juce::String describeRange (te::TimeRange range)
    {
        return juce::String (range.getStart().inSeconds(), 2) + "s to "
             + juce::String (range.getEnd().inSeconds(), 2) + "s ("
             + juce::String (range.getLength().inSeconds(), 2) + "s)";
    }
}

void AudioExporter::showExportDialog (const ExportContext& context)
{
    if (context.edit == nullptr || context.engine == nullptr)
        return;

    juce::AlertWindow dialog ("Export Options", "Choose export settings", juce::MessageBoxIconType::NoIcon);
    const juce::String formatId = "format", rateId = "rate", depthId = "depth",
                        qualId = "quality", nameId = "name", bitrateId = "bitrate";

    dialog.addTextEditor (nameId, context.defaultName.isNotEmpty() ? context.defaultName : juce::String ("Export"), "Filename");

    auto* wholeFileButton = new juce::ToggleButton ("Export whole file");
    auto* selectionButton = new juce::ToggleButton ("Export selected region");
    wholeFileButton->setRadioGroupId (1);
    selectionButton->setRadioGroupId (1);
    wholeFileButton->setSize (220, 24);
    selectionButton->setSize (220, 24);
    selectionButton->setEnabled (context.hasSelection);
    selectionButton->setToggleState (context.hasSelection, juce::dontSendNotification);
    wholeFileButton->setToggleState (! context.hasSelection, juce::dontSendNotification);
    dialog.addCustomComponent (wholeFileButton);
    dialog.addCustomComponent (selectionButton);

    auto* rangeLabel = new juce::Label ("rangeLabel", {});
    rangeLabel->setJustificationType (juce::Justification::centredLeft);
    rangeLabel->setSize (340, 24);
    dialog.addCustomComponent (rangeLabel);

    dialog.addComboBox (formatId, { "WAV", "AIFF", "FLAC", "OGG", "MP3", "M4A" }, "Format");
    auto* formatBox = dialog.getComboBoxComponent (formatId);
    formatBox->setSelectedId (1); // WAV

    dialog.addComboBox (rateId, { "44100", "48000", "96000" }, "Sample rate (Hz)");
    auto* rateBox = dialog.getComboBoxComponent (rateId);
    rateBox->setSelectedId (1);

    dialog.addComboBox (depthId, { "16", "24", "32 (float)" }, "Bit depth");
    auto* depthBox = dialog.getComboBoxComponent (depthId);
    depthBox->setSelectedId (1);

    dialog.addComboBox (qualId, { "Low (0)", "Medium (4)", "High (6)", "Max (10)" }, "Ogg quality");
    auto* qualityBox = dialog.getComboBoxComponent (qualId);
    qualityBox->setSelectedId (3);

    dialog.addComboBox (bitrateId, { "128", "192", "256", "320" }, "Bitrate (kbps)");
    auto* bitrateBox = dialog.getComboBoxComponent (bitrateId);
    bitrateBox->setSelectedId (3);

    auto updateVisibility = [formatBox, depthBox, qualityBox, bitrateBox]
    {
        auto fmt = formatBox->getText().toLowerCase();
        const bool isOgg = fmt.contains ("ogg");
        const bool isMp3 = fmt.contains ("mp3");
        const bool isM4a = fmt.contains ("m4a");
        const bool compressed = isOgg || isMp3 || isM4a;

        if (depthBox != nullptr)
            depthBox->setEnabled (! compressed);
        if (qualityBox != nullptr)
            qualityBox->setVisible (isOgg);
        if (bitrateBox != nullptr)
            bitrateBox->setVisible (isMp3 || isM4a);
    };

    auto updateExportRangeLabel = [rangeLabel, selectionButton, &context]
    {
        if (rangeLabel == nullptr)
            return;

        const bool useSelection = context.hasSelection && selectionButton != nullptr && selectionButton->getToggleState();
        const auto range = useSelection ? context.selectionRange : context.fullRange;
        rangeLabel->setText ("Will export: " + describeRange (range), juce::dontSendNotification);
    };

    wholeFileButton->onClick = updateExportRangeLabel;
    selectionButton->onClick = updateExportRangeLabel;
    updateExportRangeLabel();

    formatBox->onChange = updateVisibility;
    updateVisibility();

    dialog.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    dialog.addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));

    if (dialog.runModalLoop() != 1)
        return;

    auto fmtName = formatBox != nullptr ? formatBox->getText() : juce::String ("WAV");
    auto rateStr = rateBox != nullptr ? rateBox->getText() : juce::String ("44100");
    auto depthStr = depthBox != nullptr ? depthBox->getText() : juce::String ("16");
    auto qualIndex = qualityBox != nullptr ? qualityBox->getSelectedItemIndex() : 2;
    auto bitrateStr = bitrateBox != nullptr ? bitrateBox->getText() : juce::String ("256");

    auto oggQuality = juce::jlimit (0, 10, qualIndex == 0 ? 0 : (qualIndex == 1 ? 4 : (qualIndex == 2 ? 6 : 10)));
    auto bitrate = juce::jlimit (64, 512, bitrateStr.getIntValue());

    double chosenRate = rateStr.getDoubleValue();
    int chosenDepth = depthStr.contains ("32") ? 32 : depthStr.getIntValue();
    const bool exportSelection = context.hasSelection && selectionButton != nullptr && selectionButton->getToggleState();
    auto exportRange = exportSelection ? context.selectionRange : context.fullRange;

    auto fmtLower = fmtName.toLowerCase();
    juce::String extension = "wav";
    if (fmtLower.contains ("aiff")) extension = "aiff";
    else if (fmtLower.contains ("flac")) extension = "flac";
    else if (fmtLower.contains ("ogg")) extension = "ogg";
    else if (fmtLower.contains ("mp3")) extension = "mp3";
    else if (fmtLower.contains ("m4a")) extension = "m4a";

    auto pattern = "*." + extension;
    auto chooser = std::make_shared<juce::FileChooser> ("Choose export destination",
                                                        context.engine->getPropertyStorage().getDefaultLoadSaveDirectory ("editExport")
                                                            .getChildFile (dialog.getTextEditorContents (nameId))
                                                            .withFileExtension (extension),
                                                        pattern);

    auto enginePtr = context.engine;
    auto editPtr = context.edit;
    auto setStatus = context.setStatus;

    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [chooser, fmtName, chosenRate, chosenDepth, oggQuality, bitrate, exportRange, enginePtr, editPtr, setStatus] (const juce::FileChooser&) mutable
                          {
                              auto f = chooser->getResult();
                              if (f == juce::File() || enginePtr == nullptr || editPtr == nullptr)
                                  return;

                              auto& transport = editPtr->getTransport();
                              if (transport.isPlaying())
                                  transport.stop (false, true);
                              if (transport.isPlayContextActive())
                                  transport.freePlaybackContext();

                              editPtr->flushState();

                              if (! te::Renderer::checkTargetFile (*enginePtr, f))
                              {
                                  if (setStatus)
                                      setStatus ("Export cancelled");
                                  return;
                              }

                              enginePtr->getPropertyStorage().setDefaultLoadSaveDirectory ("editExport", f.getParentDirectory());
                              if (setStatus)
                                  setStatus ("Exporting...");

                              auto lower = fmtName.toLowerCase();
                              const bool isOgg = lower.contains ("ogg");
                              const bool isMp3 = lower.contains ("mp3");
                              const bool isM4a = lower.contains ("m4a");

                              te::Renderer::Parameters params (*enginePtr);
                              params.edit = editPtr;
                              params.destFile = f;
                              params.tracksToDo = toBitSet (getAllTracks (*editPtr));
                              params.time = exportRange;
                              params.sampleRateForAudio = chosenRate > 0 ? chosenRate : 44100.0;
                              params.blockSizeForAudio = enginePtr->getDeviceManager().getBlockSize();
                              params.bitDepth = isOgg || isMp3 || isM4a ? 16 : chosenDepth;
                              params.quality = isOgg ? oggQuality : (isMp3 || isM4a ? bitrate : 0);
                              auto format = createFormatFromName (fmtName);
                              if (format == nullptr)
                                  format = std::make_unique<juce::WavAudioFormat>();
                              params.audioFormat = format.release(); // managed below
                              params.ditheringEnabled = params.bitDepth < 32 && ! (isOgg || isMp3 || isM4a);

                              std::unique_ptr<juce::AudioFormat> ownedFormat (params.audioFormat);
                              auto rendered = te::Renderer::renderToFile ("Exporting", params);

                              if (rendered.existsAsFile())
                              {
                                  if (setStatus)
                                      setStatus ("Exported to " + f.getFileName());
                                  juce::AlertWindow::showMessageBoxAsync (juce::AlertWindow::InfoIcon,
                                                                          "Export Complete",
                                                                          "File written to:\n" + rendered.getFullPathName());
                              }
                              else
                              {
                                  if (setStatus)
                                      setStatus ("Export failed");
                                  enginePtr->getUIBehaviour().showWarningAlert ("Export failed",
                                                                                 "The file could not be written.");
                              }
                          });
}
