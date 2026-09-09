#pragma once

#include <JuceHeader.h>
#include <vector>
#include <atomic>
#include <mutex>
#include <array>

//==============================================================================
/**
 * @struct BiquadFilter
 * @brief Zero-allocation biquad IIR filter for real-time lowpass/bandpass audio processing.
 */
struct BiquadFilter
{
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void reset() { z1 = 0.0f; z2 = 0.0f; }

    void makeLowPass(double sampleRate, double cutoffHz, double q = 0.7071)
    {
        double w0 = 2.0 * juce::double_Pi * cutoffHz / sampleRate;
        double cosw = std::cos(w0);
        double sinw = std::sin(w0);
        double alpha = sinw / (2.0 * q);

        double a0 = 1.0 + alpha;
        b0 = static_cast<float>((1.0 - cosw) * 0.5 / a0);
        b1 = static_cast<float>((1.0 - cosw) / a0);
        b2 = static_cast<float>((1.0 - cosw) * 0.5 / a0);
        a1 = static_cast<float>(-2.0 * cosw / a0);
        a2 = static_cast<float>((1.0 - alpha) / a0);
    }

    void makeBandPass(double sampleRate, double centerHz, double q = 16.0)
    {
        double w0 = 2.0 * juce::double_Pi * centerHz / sampleRate;
        double cosw = std::cos(w0);
        double sinw = std::sin(w0);
        double alpha = sinw / (2.0 * q);

        double a0 = 1.0 + alpha;
        b0 = static_cast<float>(alpha / a0);
        b1 = 0.0f;
        b2 = static_cast<float>(-alpha / a0);
        a1 = static_cast<float>(-2.0 * cosw / a0);
        a2 = static_cast<float>((1.0 - alpha) / a0);
    }

    inline float process(float in)
    {
        float out = b0 * in + z1;
        z1 = b1 * in - a1 * out + z2;
        z2 = b2 * in - a2 * out;
        return out;
    }
};

/**
 * @class TunerBPMPluginAudioProcessor
 * @brief High-performance Audio Processor for STT2 (SUPREME TEMPO & TUNER - BY VEVI).
 */
class TunerBPMPluginAudioProcessor  : public juce::AudioProcessor
{
public:
    //==============================================================================
    TunerBPMPluginAudioProcessor();
    ~TunerBPMPluginAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;
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
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

    //==============================================================================
    // --- Scale & Key Detection Interface ---
    void resetScale();
    juce::String getDetectedScaleName() const;
    juce::String getRelativeKeyName() const;
    juce::String getCamelotCode() const;
    float getScaleProgress() const { return scaleProgress.load(); }
    bool isScaleLocked() const { return scaleIsLocked.load(); }

    //==============================================================================
    // --- BPM Detection Interface ---
    float getDetectedAudioBpm() const { return detectedAudioBpm.load(); }
    bool isBpmLocked() const { return bpmIsLocked.load(); }
    void unlockBpm();
    juce::String getBpmStatus() const;

    //==============================================================================
    // --- Pitch & Tuning Interface ---
    float getDetectedFrequency() const { return detectedFrequency.load(); }
    float getCentsDeviation() const { return centsDeviation.load(); }
    juce::String getDetectedNoteName() const;

    //==============================================================================
    // --- Transport & Beat Pulse Interface (Visual Only, Zero Audio Clicks) ---
    float getTempo() const { return currentTempo.load(); }
    bool isHostPlaying() const { return hostIsPlaying.load(); }
    bool getAndClearBeatTriggered(int& outBeatNumber);

    //==============================================================================
    // --- Oscilloscope Interface ---
    static const int oscilloscopeSize = 512;
    void getOscilloscopeBuffer(float* dest)
    {
        std::lock_guard<std::mutex> lock(oscMutex);
        std::copy(oscilloscopeBuffer.begin(), oscilloscopeBuffer.end(), dest);
    }

private:
    //==============================================================================
    // Polyphonic IIR Chromagram & Scale Detection Internals
    void processPolyphonicChroma(const float* inputData, int numSamples);
    void runKeyCorrelation();

    double currentSampleRate = 44100.0;
    
    // Monophonic pitch buffer for real-time tuner dock
    std::vector<float> pitchBuffer;
    int pitchBufferWritePos = 0;

    // 36 Semitone IIR Bandpass Filters (Octaves 3, 4, 5: C3 to B5, 130 Hz - 988 Hz)
    std::array<BiquadFilter, 36> chromaFilters;
    std::array<float, 36> chromaBlockEnergies { 0.0f };
    std::array<float, 12> accumulatedChroma { 0.0f };
    int chromaSampleCount = 0;
    double audioAnalyzedSeconds = 0.0;
    int chromaEvalCounter = 0;

    std::atomic<float> detectedFrequency { 0.0f };
    std::atomic<float> centsDeviation { 0.0f };
    std::atomic<int> detectedNoteIndex { -1 };
    std::atomic<int> detectedKeyIndex { -1 }; // 0-11: Major, 12-23: Minor, -1: None
    std::atomic<float> scaleProgress { 0.0f };
    std::atomic<bool> scaleIsLocked { false };

    const std::vector<juce::String> noteNames = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    //==============================================================================
    // Harmonic Comb Resonator & Kick Transient Alignment Engine (v2.0)
    BiquadFilter kickBandPass; // 62 Hz kick punch
    BiquadFilter kickLowPass;  // 110 Hz lowpass
    BiquadFilter midBandPass;   // 1200 Hz snare/clap accents

    float prevKickBlockEnergy = 0.0f;
    float prevMidBlockEnergy = 0.0f;
    float fluxThreshold = 0.0f;
    float kickSlowEnergy = 0.0f;

    double streamTimeSeconds = 0.0;
    double lastKickOnsetSec = -1.0;
    std::vector<double> kickOnsetsRing;

    // Novelty buffer: 1200 frames at 200 Hz (6.0 seconds history)
    static const int noveltyBufferSize = 1200;
    std::vector<float> noveltyBuffer;
    int noveltyWritePos = 0;
    int frameSampleCounter = 0;
    float frameKickSum = 0.0f;
    float frameMidSum = 0.0f;
    int analysisFrameCounter = 0;

    std::atomic<float> detectedAudioBpm { 0.0f };
    std::atomic<bool> bpmIsLocked { false };
    std::vector<int> recentBpmCandidates;
    mutable std::mutex bpmMutex;
    juce::String bpmStatusText = "Listening for tempo...";

    void processKickBpm(const float* inputData, int numSamples);
    void calculateTempoFromCombResonator();

    //==============================================================================
    // Beat Pulse Internals (Visual pulse for LEDs only, NO AUDIO GENERATION)
    double lastPPQ = -1.0;
    std::atomic<float> currentTempo { 120.0f };
    std::atomic<bool> hostIsPlaying { false };
    double internalSampleCounter = 0.0;
    int internalBeatNumber = 0;

    std::atomic<bool> beatTriggered { false };
    std::atomic<int> lastBeatNumber { 0 };

    //==============================================================================
    // Oscilloscope Ring Buffer
    std::vector<float> oscilloscopeBuffer;
    int oscWritePos = 0;
    std::mutex oscMutex;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TunerBPMPluginAudioProcessor)
};
