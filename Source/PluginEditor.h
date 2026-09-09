#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <vector>

/**
 * @class TunerBPMPluginAudioProcessorEditor
 * @brief Minimalist Apple-inspired GUI Editor for STB2.
 * 
 * Features:
 *  - Ultra-clean two-card layout (Key & Scale + Fixed BPM).
 *  - High-precision live waveform dock.
 *  - Fast, responsive, zero-clutter modern aesthetic.
 */
class TunerBPMPluginAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    TunerBPMPluginAudioProcessorEditor (TunerBPMPluginAudioProcessor&);
    ~TunerBPMPluginAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    TunerBPMPluginAudioProcessor& audioProcessor;

    // Minimalist Apple Controls
    juce::TextButton resetAllButton;
    juce::TextButton unlockBpmButton;

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
    juce::String bpmStatus = "Listening for kicks...";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerBPMPluginAudioProcessorEditor)
};
