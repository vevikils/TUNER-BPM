#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <vector>
#include <atomic>
#include <mutex>
#include <array>
#include <thread>

//==============================================================================
/**
 * @struct BiquadFilter
 * @brief Zero-allocation biquad IIR filter for real-time audio filtering.
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

    void makeHighPass(double sampleRate, double cutoffHz, double q = 0.7071)
    {
        double w0 = 2.0 * juce::double_Pi * cutoffHz / sampleRate;
        double cosw = std::cos(w0);
        double sinw = std::sin(w0);
        double alpha = sinw / (2.0 * q);

        double a0 = 1.0 + alpha;
        b0 = static_cast<float>((1.0 + cosw) * 0.5 / a0);
        b1 = static_cast<float>(-(1.0 + cosw) / a0);
        b2 = static_cast<float>((1.0 + cosw) * 0.5 / a0);
        a1 = static_cast<float>(-2.0 * cosw / a0);
        a2 = static_cast<float>((1.0 - alpha) / a0);
    }

    void makeBandPass(double sampleRate, double centerHz, double q = 1.0)
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
 * @brief Studio-grade Audio Processor for STT2 (SUPREME TEMPO & TUNER — BY VEVI).
 * Powered by Tunebat / Essentia HPCP Key Detection & Multi-Band Pulse-Train BPM Algorithm.
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
    // --- Scale & Key Detection Interface (Tunebat HPCP + bgate/edma/temperley) ---
    void resetScale();
    juce::String getDetectedScaleName() const;
    juce::String getRelativeKeyName() const;
    juce::String getCamelotCode() const;
    float getScaleProgress() const { return scaleProgress.load(); }
    bool isScaleLocked() const { return scaleIsLocked.load(); }

    //==============================================================================
    // --- BPM Detection Interface (Percival & Peak Interval Histogram) ---
    float getDetectedAudioBpm() const { return detectedAudioBpm.load(); }
    bool isBpmLocked() const { return bpmIsLocked.load(); }
    void unlockBpm();
    juce::String getBpmStatus() const;

    //==============================================================================
    // --- Offline File Analysis (Drag & Drop like Tunebat Web) ---
    void loadAndAnalyzeAudioFile (const juce::File& file);
    bool isAnalyzingFile() const { return fileAnalysisActive.load(); }
    juce::String getLoadedAudioFileName() const
    {
        std::lock_guard<std::mutex> lock(fileMutex);
        return loadedAudioFileName;
    }
    bool hasLoadedAudioFile() const
    {
        return isAudioFileLoaded.load(std::memory_order_acquire);
    }
    void clearLoadedAudioFile();

    //==============================================================================
    // --- Pitch & Tuning Interface ---
    float getDetectedFrequency() const { return detectedFrequency.load(); }
    float getCentsDeviation() const { return centsDeviation.load(); }
    juce::String getDetectedNoteName() const;

    //==============================================================================
    // --- Transport & Beat Pulse Interface (Visual Only, Zero Audio Clicks) ---
    float getTempo() const { return currentTempo.load(); }
    bool isHostPlaying() const { return hostIsPlaying.load(); }
    bool getAndClearBeatTriggered (int& outBeatNumber);

    //==============================================================================
    // --- Oscilloscope Interface ---
    static const int oscilloscopeSize = 512;
    void getOscilloscopeBuffer (float* dest)
    {
        std::lock_guard<std::mutex> lock(oscMutex);
        std::copy(oscilloscopeBuffer.begin(), oscilloscopeBuffer.end(), dest);
    }

private:
    //==============================================================================
    // Tunebat / Essentia HPCP Key Detection Engine
    static const int fftOrder = 11; // 2048 samples (~46 ms at 44.1 kHz)
    static const int fftSize = 1 << fftOrder;
    static const int hopSize = 1024; // 50% overlap

    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> windowFunction;

    std::vector<float> fftFifo;
    std::vector<float> fftBuffer;
    int fftFifoIndex = 0;

    void processFftHpcp (const float* samples, int numSamples);
    void computeFrameHpcp (const float* timeDomainWindowed);
    void runKeyCorrelation();

    double currentSampleRate = 44100.0;

    // Accumulated 12-class HPCP distribution
    std::array<float, 12> accumulatedChroma { 0.0f };
    double audioAnalyzedSeconds = 0.0;
    int chromaEvalCounter = 0;

    std::atomic<float> scaleProgress { 0.0f };
    std::atomic<bool> scaleIsLocked { false };
    std::atomic<int> detectedKeyIndex { -1 }; // 0..11: Major, 12..23: Minor, -1: None

    const std::vector<juce::String> noteNames = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    //==============================================================================
    // Monophonic pitch buffer for real-time tuner dock
    std::vector<float> pitchBuffer;
    int pitchBufferWritePos = 0;

    std::atomic<float> detectedFrequency { 0.0f };
    std::atomic<float> centsDeviation { 0.0f };
    std::atomic<int> detectedNoteIndex { -1 };

    //==============================================================================
    // Tunebat BPM Engine: 3-Band Novelty Flux + Harmonic Comb + Peak Interval Histogram
    BiquadFilter kickLowPass;   // Low band: kick thump (< 200 Hz)
    BiquadFilter midBandPass;   // Mid band: snare & transients (250 Hz - 2500 Hz)
    BiquadFilter hihatHighPass; // High band: hats & percs (> 2800 Hz)

    float prevLowEnergy = 0.0f;
    float prevMidEnergy = 0.0f;
    float prevHighEnergy = 0.0f;
    float noveltyThreshold = 0.0f;

    double streamTimeSeconds = 0.0;
    double lastKickOnsetSec = -1.0;
    std::vector<double> detectedOnsetTimes;

    // Novelty history: 1500 frames at 100 Hz = 15.0 seconds of tempo context
    static const int noveltyBufferSize = 1500;
    std::vector<float> noveltyBuffer;
    int noveltyWritePos = 0;
    int frameSampleCounter = 0;
    float frameLowSum = 0.0f;
    float frameMidSum = 0.0f;
    float frameHighSum = 0.0f;
    int bpmAnalysisTimer = 0;

    std::atomic<float> detectedAudioBpm { 0.0f };
    std::atomic<bool> bpmIsLocked { false };
    std::vector<float> recentBpmCandidates;
    mutable std::mutex bpmMutex;
    juce::String bpmStatusText = "Listening for tempo...";

    void processKickBpm (const float* inputData, int numSamples);
    void calculateTempoFromCombAndIntervals();

    // Offline audio file analyzer (Tunebat web style)
    juce::AudioFormatManager formatManager;
    std::atomic<bool> fileAnalysisActive { false };
    std::atomic<bool> isAudioFileLoaded { false };
    std::atomic<uint32_t> currentJobId { 0 };
    std::atomic<bool> workerShouldExit { false };
    std::thread workerThread;
    std::condition_variable workerCv;
    std::mutex workerMutex;
    juce::File pendingFile;
    uint32_t pendingJobId { 0 };
    mutable std::mutex fileMutex;
    juce::String loadedAudioFileName;

    void runAnalysisWorker();
    void analyzeFileInternal(const juce::File& file, uint32_t myJobId);

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
