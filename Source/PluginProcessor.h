#pragma once

#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include <mutex>

/**
 * @class TunerBPMPluginAudioProcessor
 * @brief Core Audio Processor for the Supreme Tuner BPM Audio Plugin.
 * 
 * Handles real-time audio DSP processing including:
 *  - Autocorrelation-based pitch detection and tuning calculation (frequency & cents deviation).
 *  - Key and musical scale detection via chroma accumulation.
 *  - Audio-based BPM detection via onset envelope processing and tempo autocorrelation.
 *  - Host DAW beat sync & internal metronome click generation.
 *  - Thread-safe circular buffer for real-time UI oscilloscope visualization.
 */
class TunerBPMPluginAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    /** Constructor: Initializes parameter state layout and DSP buffers. */
    TunerBPMPluginAudioProcessor();

    /** Destructor. */
    ~TunerBPMPluginAudioProcessor() override;

    //==============================================================================
    /** Called before audio playback starts to set up sample rates and reset DSP states. */
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;

    /** Called when audio playback stops to release allocated resources. */
    void releaseResources() override;

    /** Validates supported bus channel layouts (mono/stereo input/output). */
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    /** Main real-time audio processing block callback. */
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    /** Creates the plugin custom GUI editor interface. */
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    /** Returns the plugin name. */
    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    /** Saves plugin state parameters to memory block. */
    void getStateInformation (juce::MemoryBlock& destData) override;

    /** Restores plugin state parameters from memory block. */
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** Helper method constructing the APVTS parameter layout. */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Audio Processor Value Tree State holding GUI plugin parameters. */
    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    // --- Thread-Safe Pitch Detection Interface (for Editor) ---

    /** Returns the detected fundamental frequency in Hz. */
    float getDetectedFrequency() const { return detectedFrequency.load(); }

    /** Returns the cents deviation relative to the nearest Western chromatic note (-50 to +50 cents). */
    float getCentsDeviation() const { return centsDeviation.load(); }

    /** Returns the detected chromatic note index (0: C, 1: C#, ..., 11: B, -1: None). */
    int getDetectedNoteIndex() const { return detectedNoteIndex.load(); }

    /** Returns the formatted string representation of the detected note (e.g., "A4"). */
    juce::String getDetectedNoteName() const;

    //==============================================================================
    // --- Metronome Beat Interface (for Editor) ---

    /** Atomically checks and clears beat trigger flags for visual sync. */
    bool getAndClearBeatTriggered(int& outBeatNumber);

    /** Returns current effective tempo in BPM (DAW synced or internal). */
    float getTempo() const { return currentTempo.load(); }

    /** Returns true if the host DAW transport is currently playing. */
    bool isHostPlaying() const { return hostIsPlaying.load(); }

    //==============================================================================
    // --- Scale and Key Detection ---

    /** Resets the chroma profile accumulator for fresh key detection. */
    void resetScale();

    /** Returns detected key signature name (e.g. "C Major", "A Minor"). */
    juce::String getDetectedScaleName() const;

    /** Returns statistical confidence of current key detection (0.0 to 1.0). */
    float getScaleConfidence() const { return scaleConfidence.load(); }

    //==============================================================================
    // --- Audio-based BPM Detection ---

    /** Returns estimated tempo detected directly from input audio onsets. */
    float getDetectedAudioBpm() const { return detectedAudioBpm.load(); }

    //==============================================================================
    // --- Oscilloscope Interface ---

    /** Size of the oscilloscope waveform display buffer. */
    static const int oscilloscopeSize = 512;

    /** Thread-safe copy of the latest oscilloscope audio frame. */
    void getOscilloscopeBuffer(float* dest)
    {
        std::lock_guard<std::mutex> lock(oscMutex);
        std::copy(oscilloscopeBuffer.begin(), oscilloscopeBuffer.end(), dest);
    }

private:
    //==============================================================================
    // --- DSP Pitch Detection Internals ---
    void processPitchDetection(const float* inputData, int numSamples);
    void runAutocorrelation();

    double currentSampleRate = 44100.0;
    
    // Pitch detection ring buffer (size 8192 to hold 4096 samples at half-rate)
    std::vector<float> inputRingBuffer;
    int ringBufferWritePos = 0;
    std::vector<float> downsampledBuffer; // Downsampled buffer by factor of 2
    int downsampleWritePos = 0;
    
    std::atomic<float> detectedFrequency { 0.0f };
    std::atomic<float> centsDeviation { 0.0f };
    std::atomic<int> detectedNoteIndex { -1 };
    
    const std::vector<juce::String> noteNames = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    //==============================================================================
    // --- Scale/Key Detection DSP Internals ---
    std::vector<float> chromaAccumulator;
    std::atomic<int> detectedKeyIndex { -1 }; // 0-11: Major keys, 12-23: Minor keys, -1: Unknown
    std::atomic<float> scaleConfidence { 0.0f };
    void runScaleDetection();

    //==============================================================================
    // --- Metronome & Click Generator Internals ---
    bool clickActive = false;
    double clickSampleRate = 44100.0;
    double clickFrequency = 1000.0;
    int clickSampleIndex = 0;
    double clickLengthSamples = 0.0;
    
    // Host transport state tracking
    double lastPPQ = -1.0;
    std::atomic<float> currentTempo { 120.0f };
    std::atomic<bool> hostIsPlaying { false };
    
    // Internal transport fallback
    double internalSampleCounter = 0.0;
    int internalBeatNumber = 0;

    // Visual indicator flags
    std::atomic<bool> beatTriggered { false };
    std::atomic<int> lastBeatNumber { 0 };

    //==============================================================================
    // --- Audio Onset BPM Detection Internals ---
    float envelopeFollower = 0.0f;
    float prevEnvelope = 0.0f;
    int onsetSampleCounter = 0;
    int onsetDownsampleRate = 441; // Downsample from audio rate to 100 Hz
    std::vector<float> onsetHistory;
    int onsetHistoryWritePos = 0;
    std::atomic<float> detectedAudioBpm { 0.0f };
    void processAudioBpm(const float* inputData, int numSamples);
    void runTempoAutocorrelation();

    //==============================================================================
    // --- Visual Oscilloscope Buffer ---
    std::vector<float> oscilloscopeBuffer;
    int oscWritePos = 0;
    std::mutex oscMutex;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerBPMPluginAudioProcessor)
};
