#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include <vector>

/**
 * @class TunerBPMPluginAudioProcessorEditor
 * @brief GUI Editor component for the Supreme Tuner BPM Audio Plugin.
 * 
 * Provides an interactive UI with modern aesthetics including:
 *  - Real-time chromatic pitch tuner display (note name, pitch offset indicator in cents, frequency).
 *  - Audio waveform oscilloscope display.
 *  - Interactive Tap Tempo button with sliding-window interval averaging.
 *  - Metronome volume, tempo, and sync-mode controls (DAW Sync vs. Internal Metronome).
 *  - Key and musical scale detection display with manual reset control.
 *  - Animated metronome beat indicator.
 */
class TunerBPMPluginAudioProcessorEditor  : public juce::AudioProcessorEditor,
                                             private juce::Timer
{
public:
    /** Constructor: Configures layout, attachments, and visual timers. */
    TunerBPMPluginAudioProcessorEditor (TunerBPMPluginAudioProcessor&);

    /** Destructor. */
    ~TunerBPMPluginAudioProcessorEditor() override;

    //==============================================================================
    /** Main component rendering callback. Draws oscilloscope, tuner dial, and meters. */
    void paint (juce::Graphics&) override;

    /** Component resize/layout positioning callback. */
    void resized() override;

private:
    /** Periodic UI timer callback (30 FPS refresh rate) for smooth animations. */
    void timerCallback() override;
    
    /** Processes mouse tap events for tap tempo calculation. */
    void handleTapTempo();

    /** Reference to the underlying Audio Processor. */
    TunerBPMPluginAudioProcessor& audioProcessor;

    //==============================================================================
    // --- GUI Controls ---
    juce::Slider volumeSlider;
    juce::Slider tempoSlider;
    
    juce::Label volumeLabel;
    juce::Label tempoLabel;
    
    juce::ComboBox syncModeComboBox;
    juce::TextButton internalPlayButton;
    juce::TextButton tapTempoButton;
    juce::TextButton resetScaleButton;

    // APVTS Parameter Attachments
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volumeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tempoAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> syncModeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> internalPlayAttachment;

    //==============================================================================
    // --- Animation & State Caching ---
    float beatFlashLevel = 0.0f;
    int currentBeatNum = 1;
    
    // Smoothed meter values
    float smoothedCents = 0.0f;
    float smoothedFreq = 0.0f;
    int activeNoteIndex = -1;
    juce::String activeNoteName = "---";

    // Tap tempo timestamp history buffer
    std::vector<juce::int64> tapTimes;

    // Oscilloscope paint data cache
    float oscilloscopeData[TunerBPMPluginAudioProcessor::oscilloscopeSize];

    // Scale and Audio BPM visualization caching
    juce::String currentScaleName = "Detecting...";
    float currentScaleConfidence = 0.0f;
    float currentAudioBpm = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerBPMPluginAudioProcessorEditor)
};
