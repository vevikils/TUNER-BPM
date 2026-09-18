#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <vector>

/**
 * @class TunerBPMPluginAudioProcessorEditor
 * @brief Luxury Studio Pro Obsidian Glass GUI Editor for STT2 (v2.0 PRO).
 * 
 * Features:
 *  - Ultra-clean high-end two-card layout (Harmonic Key & Scale + Acoustic Tempo BPM).
 *  - Prominent Loaded File display banner with instant Eject / Live DAW sync toggle.
 *  - Drag & Drop Audio File (MP3, WAV, FLAC, AIFF) analysis like Tunebat Analyzer.
 *  - Organic Luminous Dual-Pass Oscilloscope and Chromatic Pitch Cents Gauge.
 *  - Discrete 4-Beat LED visual metronome with exponential phosphor flash.
 */
class TunerBPMPluginAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                             public juce::FileDragAndDropTarget,
                                             private juce::Timer
{
public:
    TunerBPMPluginAudioProcessorEditor (TunerBPMPluginAudioProcessor&);
    ~TunerBPMPluginAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // FileDragAndDropTarget methods (Tunebat Web Drag & Drop)
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;

private:
    void timerCallback() override;
    void triggerOpenFileDialog();

    TunerBPMPluginAudioProcessor& audioProcessor;

    // Luxury Studio Pro Obsidian Glass Controls
    juce::TextButton openFileButton;
    juce::TextButton ejectFileButton;
    juce::TextButton resetAllButton;
    juce::TextButton unlockBpmButton;

    std::unique_ptr<juce::FileChooser> fileChooser;
    bool isFileHovering = false;

    // Animation & State Caching
    float beatFlashLevel = 0.0f;
    int currentBeatNum = 1;

    float smoothedCents = 0.0f;
    juce::String activeNoteName = "---";

    float oscilloscopeData[TunerBPMPluginAudioProcessor::oscilloscopeSize];

    juce::String currentScaleName = "Detecting...";
    juce::String currentRelativeKey = "---";
    juce::String currentCamelot = "---";
    float currentScaleProgress = 0.0f;
    bool isScaleLocked = false;

    float currentAudioBpm = 0.0f;
    bool isBpmLocked = false;
    juce::String bpmStatus = "Listening for tempo...";

    juce::String currentLoadedFileName;
    bool isAnalyzingFile = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerBPMPluginAudioProcessorEditor)
};
