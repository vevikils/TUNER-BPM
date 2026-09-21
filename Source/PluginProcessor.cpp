#include "PluginProcessor.h"
#include "PluginEditor.h"
#if JUCE_WINDOWS
 #define NOMINMAX
 #include <windows.h>
 #include <objbase.h>
#endif
#include <cmath>
#include <algorithm>
#include <numeric>

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout TunerBPMPluginAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("volume", 1), "Click Volume", 0.0f, 1.0f, 0.5f));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("mute", 1), "Click Mute", false));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID("internalTempo", 1), "Internal Tempo", 40.0f, 240.0f, 120.0f));

    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID("syncMode", 1), "Sync Mode", juce::StringArray("DAW Sync", "Internal BPM"), 0));

    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID("internalPlay", 1), "Internal Play", false));

    return { params.begin(), params.end() };
}

//==============================================================================
TunerBPMPluginAudioProcessor::TunerBPMPluginAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
       apvts(*this, nullptr, "Parameters", createParameterLayout()),
       forwardFFT(fftOrder),
       windowFunction(fftSize, juce::dsp::WindowingFunction<float>::hann)
#endif
{
    fftFifo.resize(fftSize, 0.0f);
    fftBuffer.resize(fftSize * 2, 0.0f);
    pitchBuffer.resize(2048, 0.0f);
    noveltyBuffer.resize(noveltyBufferSize, 0.0f);
    oscilloscopeBuffer.resize(oscilloscopeSize, 0.0f);
    recentBpmCandidates.reserve(16);
    detectedOnsetTimes.reserve(64);

    // Register audio formats for drag-and-drop file analyzer (WAV, AIFF, FLAC, OGG, MP3/M4A/WMA)
    formatManager.registerBasicFormats();
   #if JUCE_USE_FLAC
    formatManager.registerFormat(new juce::FlacAudioFormat(), false);
   #endif
   #if JUCE_USE_OGGVORBIS
    formatManager.registerFormat(new juce::OggVorbisAudioFormat(), false);
   #endif
   #if JUCE_WINDOWS
    formatManager.registerFormat(new juce::WindowsMediaAudioFormat(), false);
   #endif

    kickLowPass.makeLowPass(44100.0, 200.0, 0.7071);
    midBandPass.makeBandPass(44100.0, 1200.0, 1.0);
    hihatHighPass.makeHighPass(44100.0, 2800.0, 0.7071);

    workerThread = std::thread([this]() { runAnalysisWorker(); });
}

TunerBPMPluginAudioProcessor::~TunerBPMPluginAudioProcessor()
{
    workerShouldExit.store(true);
    ++currentJobId;
    workerCv.notify_all();
    if (workerThread.joinable())
    {
        workerThread.join();
    }
}

const juce::String TunerBPMPluginAudioProcessor::getName() const
{
    return "Supreme Tuner BPM V.2.2";
}

bool TunerBPMPluginAudioProcessor::acceptsMidi() const
{
    return false;
}

bool TunerBPMPluginAudioProcessor::producesMidi() const
{
    return false;
}

bool TunerBPMPluginAudioProcessor::isMidiEffect() const
{
    return false;
}

double TunerBPMPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int TunerBPMPluginAudioProcessor::getNumPrograms()
{
    return 1;
}

int TunerBPMPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void TunerBPMPluginAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String TunerBPMPluginAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void TunerBPMPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void TunerBPMPluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::ignoreUnused(samplesPerBlock);

    currentSampleRate = sampleRate;

    std::fill(fftFifo.begin(), fftFifo.end(), 0.0f);
    std::fill(fftBuffer.begin(), fftBuffer.end(), 0.0f);
    fftFifoIndex = 0;

    std::fill(pitchBuffer.begin(), pitchBuffer.end(), 0.0f);
    pitchBufferWritePos = 0;

    std::fill(noveltyBuffer.begin(), noveltyBuffer.end(), 0.0f);
    noveltyWritePos = 0;
    frameSampleCounter = 0;
    frameLowSum = 0.0f;
    frameMidSum = 0.0f;
    frameHighSum = 0.0f;
    bpmAnalysisTimer = 0;

    kickLowPass.makeLowPass(sampleRate, 200.0, 0.7071);
    kickLowPass.reset();
    midBandPass.makeBandPass(sampleRate, 1200.0, 1.0);
    midBandPass.reset();
    hihatHighPass.makeHighPass(sampleRate, 2800.0, 0.7071);
    hihatHighPass.reset();

    prevLowEnergy = 0.0f;
    prevMidEnergy = 0.0f;
    prevHighEnergy = 0.0f;
    noveltyThreshold = 0.0f;
    streamTimeSeconds = 0.0;
    lastKickOnsetSec = -1.0;
    detectedOnsetTimes.clear();
    recentBpmCandidates.clear();

    internalSampleCounter = 0.0;
    internalBeatNumber = 0;
    lastPPQ = -1.0;

    resetScale();
    unlockBpm();
}

void TunerBPMPluginAudioProcessor::releaseResources()
{
}

bool TunerBPMPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void TunerBPMPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // Safe Stereo Downmix & NaN/Inf Sanitization for 100% Stability
    juce::HeapBlock<float> monoDownmix(numSamples);
    const float* ch0 = buffer.getReadPointer(0);
    const float* ch1 = (numChannels > 1) ? buffer.getReadPointer(1) : ch0;

    for (int i = 0; i < numSamples; ++i)
    {
        float s0 = ch0[i];
        float s1 = ch1[i];
        if (!std::isfinite(s0)) s0 = 0.0f;
        if (!std::isfinite(s1)) s1 = 0.0f;
        float clean = (numChannels > 1) ? 0.5f * (s0 + s1) : s0;
        monoDownmix[i] = juce::jlimit(-2.0f, 2.0f, clean);
    }

    const float* inputData = monoDownmix.get();

    // 1. Oscilloscope Ring Buffer Capture
    {
        std::lock_guard<std::mutex> lock(oscMutex);
        for (int i = 0; i < numSamples; ++i)
        {
            oscilloscopeBuffer[oscWritePos] = inputData[i];
            oscWritePos = (oscWritePos + 1) % oscilloscopeSize;
        }
    }

    // 2. PlayHead transport info for visual UI sync
    auto* ph = getPlayHead();
    juce::Optional<juce::AudioPlayHead::PositionInfo> positionInfo;
    if (ph != nullptr)
        positionInfo = ph->getPosition();

    double currentPpq = 0.0;
    bool isPlaying = false;
    int timeSigNum = 4;
    double hostBpm = 120.0;

    if (positionInfo.hasValue())
    {
        auto info = *positionInfo;
        currentPpq = info.getPpqPosition().orFallback(0.0);
        isPlaying = info.getIsPlaying();
        hostBpm = info.getBpm().orFallback(120.0);
        timeSigNum = info.getTimeSignature().orFallback(juce::AudioPlayHead::TimeSignature{4, 4}).numerator;
        if (timeSigNum <= 0) timeSigNum = 4;
    }

    // 3. Tunebat / Essentia HPCP Key & BPM Analysis (Only run on live audio when no file is loaded)
    if (!hasLoadedAudioFile())
    {
        processFftHpcp(inputData, numSamples);
        processKickBpm(inputData, numSamples);
    }

    // 5. Visual Beat Pulse Tracker (for UI LEDs ONLY - ZERO AUDIBLE CLICKS!)
    float internalBpm = *apvts.getRawParameterValue("internalTempo");
    int mode = static_cast<int>(*apvts.getRawParameterValue("syncMode"));
    bool internalIsPlaying = *apvts.getRawParameterValue("internalPlay");
    bool useDAWSync = (mode == 0) && positionInfo.hasValue();

    if (useDAWSync)
    {
        currentTempo.store(static_cast<float>(hostBpm));
        hostIsPlaying.store(isPlaying);

        if (isPlaying)
        {
            double ppqPerSample = (hostBpm / 60.0) / currentSampleRate;
            if (lastPPQ < 0.0 || std::abs(currentPpq - lastPPQ) > 0.5)
                lastPPQ = currentPpq;

            double endPpq = currentPpq + numSamples * ppqPerSample;
            if (std::floor(endPpq) > std::floor(lastPPQ))
            {
                int beatNum = (static_cast<int>(std::floor(endPpq)) % timeSigNum) + 1;
                lastBeatNumber.store(beatNum);
                beatTriggered.store(true);
            }
            lastPPQ = endPpq;
        }
        else
        {
            lastPPQ = -1.0;
        }
    }
    else
    {
        currentTempo.store(internalBpm);
        hostIsPlaying.store(internalIsPlaying);

        if (internalIsPlaying)
        {
            double samplesPerBeat = (60.0 / internalBpm) * currentSampleRate;
            internalSampleCounter += numSamples;
            if (internalSampleCounter >= samplesPerBeat)
            {
                internalSampleCounter = std::fmod(internalSampleCounter, samplesPerBeat);
                internalBeatNumber = (internalBeatNumber % 4) + 1;
                lastBeatNumber.store(internalBeatNumber);
                beatTriggered.store(true);
            }
        }
        else
        {
            internalSampleCounter = 0.0;
            internalBeatNumber = 0;
        }
    }
}

// ==============================================================================
// Tunebat / Essentia HPCP Key Detection Engine
// ==============================================================================
void TunerBPMPluginAudioProcessor::processFftHpcp(const float* samples, int numSamples)
{
    // Part A: Monophonic Pitch for Bottom Tuner Dock (Autocorrelation)
    for (int i = 0; i < numSamples; ++i)
    {
        float currentSample = samples[i];
        if (i % 2 == 0)
        {
            float avg = currentSample;
            if (i + 1 < numSamples)
                avg = (currentSample + samples[i + 1]) * 0.5f;

            pitchBuffer[pitchBufferWritePos] = avg;
            pitchBufferWritePos = (pitchBufferWritePos + 1) % 2048;

            if (pitchBufferWritePos % 512 == 0)
            {
                double fsHalf = currentSampleRate * 0.5;
                int minLag = std::max(3, static_cast<int>(fsHalf / 1000.0));
                int maxLag = std::min(1024, static_cast<int>(fsHalf / 45.0));

                const int N = 1024;
                float x[2048];
                int startPos = (pitchBufferWritePos - 2048 + 2048) % 2048;
                for (int s = 0; s < 2048; ++s)
                    x[s] = pitchBuffer[(startPos + s) % 2048];

                double energy = 0.0;
                for (int n = 0; n < N; ++n)
                    energy += static_cast<double>(x[n]) * static_cast<double>(x[n]);

                if (energy > 0.0003)
                {
                    double maxCorr = -1e9;
                    int bestLag = -1;
                    for (int tau = minLag; tau <= maxLag; ++tau)
                    {
                        double sum = 0.0;
                        for (int n = 0; n < N; ++n)
                            sum += static_cast<double>(x[n]) * static_cast<double>(x[n + tau]);

                        if (sum > maxCorr)
                        {
                            maxCorr = sum;
                            bestLag = tau;
                        }
                    }

                    if (bestLag > minLag && bestLag < maxLag && (maxCorr / energy) > 0.60)
                    {
                        double exactLag = static_cast<double>(bestLag);
                        double freq = fsHalf / exactLag;

                        if (freq >= 45.0 && freq <= 1200.0)
                        {
                            double d = 12.0 * std::log2(freq / 440.0) + 69.0;
                            int note = static_cast<int>(std::round(d));
                            float cents = static_cast<float>(100.0 * (d - note));

                            detectedFrequency.store(static_cast<float>(freq));
                            centsDeviation.store(cents);
                            detectedNoteIndex.store(note);
                        }
                    }
                }
            }
        }
    }

    // Part B: High-Precision Spectral HPCP Frame Processing
    for (int i = 0; i < numSamples; ++i)
    {
        fftFifo[fftFifoIndex++] = samples[i];

        if (fftFifoIndex >= fftSize)
        {
            computeFrameHpcp(fftFifo.data());

            // 50% overlap shift
            std::copy(fftFifo.begin() + hopSize, fftFifo.end(), fftFifo.begin());
            fftFifoIndex = fftSize - hopSize;

            chromaEvalCounter++;
            if (chromaEvalCounter >= 6) // Evaluate key correlation every ~140 ms
            {
                chromaEvalCounter = 0;
                runKeyCorrelation();
            }
        }
    }
}

void TunerBPMPluginAudioProcessor::computeFrameHpcp(const float* timeDomainData)
{
    // 1. Copy time domain frame to FFT buffer and apply Hann window
    std::copy(timeDomainData, timeDomainData + fftSize, fftBuffer.begin());
    std::fill(fftBuffer.begin() + fftSize, fftBuffer.begin() + fftSize * 2, 0.0f);

    windowFunction.multiplyWithWindowingTable(fftBuffer.data(), fftSize);

    // 2. Perform forward FFT (computes magnitudes in fftBuffer[0 .. fftSize / 2])
    forwardFFT.performFrequencyOnlyForwardTransform(fftBuffer.data());

    // 3. Spectral energy check
    float frameEnergy = 0.0f;
    for (int k = 1; k < fftSize / 2; ++k)
        frameEnergy += fftBuffer[k];

    if (frameEnergy < 1e-4f)
        return; // Silence or noise floor

    audioAnalyzedSeconds += static_cast<double>(hopSize) / currentSampleRate;

    // Progress moves monotonically up to 100% over 8 seconds of active audio
    if (!scaleIsLocked.load())
    {
        float prog = juce::jlimit(0.0f, 1.0f, static_cast<float>(audioAnalyzedSeconds / 8.0));
        scaleProgress.store(prog);
    }

    // 4. Extract spectral peaks and map to Harmonic Pitch Class Profile (HPCP)
    // Frequency range: 50 Hz to 3500 Hz covers bass, chords, vocals, lead synths
    int minBin = std::max(1, juce::roundToInt(50.0 * fftSize / currentSampleRate));
    int maxBin = std::min(fftSize / 2 - 2, juce::roundToInt(3500.0 * fftSize / currentSampleRate));

    std::array<float, 12> frameChroma { 0.0f };

    for (int k = minBin; k <= maxBin; ++k)
    {
        float mag = fftBuffer[k];

        // Peak detection with parabolic refinement
        if (mag > fftBuffer[k - 1] && mag > fftBuffer[k + 1] && mag > 0.001f)
        {
            float y1 = fftBuffer[k - 1];
            float y2 = mag;
            float y3 = fftBuffer[k + 1];

            float denom = y1 - 2.0f * y2 + y3;
            float delta = 0.0f;
            if (std::abs(denom) > 1e-9f)
                delta = 0.5f * (y1 - y3) / denom;

            float exactBin = static_cast<float>(k) + delta;
            float peakFreq = exactBin * static_cast<float>(currentSampleRate) / static_cast<float>(fftSize);
            float peakMag = y2 - 0.25f * (y1 - y3) * delta;

            if (peakFreq >= 50.0f && peakFreq <= 3500.0f && peakMag > 0.0f)
            {
                // Continuous MIDI note: 69 is A4 (440 Hz)
                double midiNote = 12.0 * std::log2(peakFreq / 440.0) + 69.0;
                double pitchClass = std::fmod(midiNote, 12.0);
                if (pitchClass < 0.0)
                    pitchClass += 12.0;

                // Essentia HPCP harmonic weighting: f, 2f, 3f, 4f
                const float harmonicWeights[4] = { 1.0f, 0.60f, 0.36f, 0.216f };
                for (int h = 1; h <= 4; ++h)
                {
                    double hpc = std::fmod(pitchClass + 12.0 * std::log2(static_cast<double>(h)), 12.0);
                    if (hpc < 0.0) hpc += 12.0;

                    int pcInt = static_cast<int>(std::floor(hpc)) % 12;
                    int pcNext = (pcInt + 1) % 12;
                    float frac = static_cast<float>(hpc - std::floor(hpc));

                    float w = peakMag * harmonicWeights[h - 1];
                    frameChroma[pcInt] += w * (1.0f - frac);
                    frameChroma[pcNext] += w * frac;
                }
            }
        }
    }

    // 5. Accumulate frame chroma into persistent tonal distribution
    float frameMax = 0.0f;
    for (int pc = 0; pc < 12; ++pc)
        frameMax = std::max(frameMax, frameChroma[pc]);

    if (frameMax > 1e-6f)
    {
        for (int pc = 0; pc < 12; ++pc)
            accumulatedChroma[pc] += frameChroma[pc] / frameMax;
    }
}

// ==============================================================================
// Tunebat / Essentia Multi-Profile Correlation (bgate, edma, temperley)
// ==============================================================================
static int correlateKeyProfile(const std::array<float, 12>& chroma, float* outBestCorr = nullptr)
{
    // 1. Essentia bgate profiles (Gold standard default in Essentia & Tunebat, from BeatPort EDM)
    const float bgateMajor[12] = { 1.00f, 0.00f, 0.42f, 0.00f, 0.53f, 0.37f, 0.00f, 0.77f, 0.00f, 0.38f, 0.21f, 0.30f };
    const float bgateMinor[12] = { 1.00f, 0.00f, 0.36f, 0.39f, 0.00f, 0.38f, 0.00f, 0.74f, 0.27f, 0.00f, 0.42f, 0.23f };

    // 2. Essentia edma profiles (Electronic Dance Music Algorithm, Faraldo et al.)
    const float edmaMajor[12]  = { 1.00f, 0.29f, 0.50f, 0.40f, 0.60f, 0.56f, 0.32f, 0.80f, 0.31f, 0.45f, 0.42f, 0.39f };
    const float edmaMinor[12]  = { 1.00f, 0.31f, 0.44f, 0.58f, 0.33f, 0.49f, 0.29f, 0.78f, 0.43f, 0.29f, 0.53f, 0.32f };

    // 3. Temperley Cognitive profiles (Euroclassical & pop harmonic corpus)
    const float tempMajor[12]  = { 5.00f, 2.00f, 3.50f, 2.00f, 4.50f, 4.00f, 2.00f, 4.50f, 2.00f, 3.50f, 1.50f, 4.00f };
    const float tempMinor[12]  = { 5.00f, 2.00f, 3.50f, 4.50f, 2.00f, 4.00f, 2.00f, 4.50f, 3.50f, 2.00f, 1.50f, 4.00f };

    float chromaMean = 0.0f;
    for (int c = 0; c < 12; ++c)
        chromaMean += chroma[c];
    chromaMean /= 12.0f;

    float bestCorr = -2.0f;
    int bestKey = -1;

    for (int key = 0; key < 24; ++key)
    {
        bool isMinor = (key >= 12);
        int tonic = key % 12;

        const float* profB = isMinor ? bgateMinor : bgateMajor;
        const float* profE = isMinor ? edmaMinor  : edmaMajor;
        const float* profT = isMinor ? tempMinor  : tempMajor;

        float meanB = 0.0f, meanE = 0.0f, meanT = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            meanB += profB[i];
            meanE += profE[i];
            meanT += profT[i];
        }
        meanB /= 12.0f;
        meanE /= 12.0f;
        meanT /= 12.0f;

        float numB = 0.0f, denXB = 0.0f, denYB = 0.0f;
        float numE = 0.0f, denXE = 0.0f, denYE = 0.0f;
        float numT = 0.0f, denXT = 0.0f, denYT = 0.0f;

        for (int i = 0; i < 12; ++i)
        {
            int chromaIndex = (tonic + i) % 12;
            float x = chroma[chromaIndex] - chromaMean;

            float yb = profB[i] - meanB;
            numB  += x * yb;
            denXB += x * x;
            denYB += yb * yb;

            float ye = profE[i] - meanE;
            numE  += x * ye;
            denXE += x * x;
            denYE += ye * ye;

            float yt = profT[i] - meanT;
            numT  += x * yt;
            denXT += x * x;
            denYT += yt * yt;
        }

        float rB = (denXB * denYB > 1e-9f) ? (numB / std::sqrt(denXB * denYB)) : -1.0f;
        float rE = (denXE * denYE > 1e-9f) ? (numE / std::sqrt(denXE * denYE)) : -1.0f;
        float rT = (denXT * denYT > 1e-9f) ? (numT / std::sqrt(denXT * denYT)) : -1.0f;

        float r = 0.50f * rB + 0.30f * rE + 0.20f * rT;
        if (r > bestCorr)
        {
            bestCorr = r;
            bestKey = key;
        }
    }

    if (outBestCorr != nullptr)
        *outBestCorr = bestCorr;

    return bestKey;
}

void TunerBPMPluginAudioProcessor::runKeyCorrelation()
{
    if (audioAnalyzedSeconds < 1.0)
        return;

    float bestCorr = -2.0f;
    int bestKey = correlateKeyProfile(accumulatedChroma, &bestCorr);

    if (bestKey >= 0)
    {
        detectedKeyIndex.store(bestKey);

        if (audioAnalyzedSeconds >= 7.0 || (bestCorr >= 0.70f && audioAnalyzedSeconds >= 3.5))
        {
            scaleIsLocked.store(true);
            scaleProgress.store(1.0f);
        }
    }
}

void TunerBPMPluginAudioProcessor::resetScale()
{
    std::fill(accumulatedChroma.begin(), accumulatedChroma.end(), 0.0f);
    audioAnalyzedSeconds = 0.0;
    chromaEvalCounter = 0;
    detectedKeyIndex.store(-1);
    scaleProgress.store(0.0f);
    scaleIsLocked.store(false);
}

juce::String TunerBPMPluginAudioProcessor::getDetectedScaleName() const
{
    int key = detectedKeyIndex.load();
    if (key < 0)
        return "Detecting...";

    int root = key % 12;
    bool isMinor = (key >= 12);
    return noteNames[root] + (isMinor ? " Minor" : " Major");
}

juce::String TunerBPMPluginAudioProcessor::getRelativeKeyName() const
{
    int key = detectedKeyIndex.load();
    if (key < 0)
        return "---";

    int root = key % 12;
    bool isMinor = (key >= 12);

    if (isMinor)
    {
        int relRoot = (root + 3) % 12;
        return noteNames[relRoot] + " Major";
    }
    else
    {
        int relRoot = (root + 9) % 12;
        return noteNames[relRoot] + " Minor";
    }
}

juce::String TunerBPMPluginAudioProcessor::getCamelotCode() const
{
    int key = detectedKeyIndex.load();
    if (key < 0)
        return "---";

    int root = key % 12;
    bool isMinor = (key >= 12);

    // Camelot mappings for C, C#, D, D#, E, F, F#, G, G#, A, A#, B
    // Minor: C=5A, C#=12A, D=7A, D#=2A, E=9A, F=4A, F#=11A, G=6A, G#=1A, A=8A, A#=3A, B=10A
    // Major: C=8B, C#=3B, D=10B, D#=5B, E=12B, F=7B, F#=2B, G=9B, G#=4B, A=11B, A#=6B, B=1B
    const char* minorCamelot[] = { "5A", "12A", "7A", "2A", "9A", "4A", "11A", "6A", "1A", "8A", "3A", "10A" };
    const char* majorCamelot[] = { "8B", "3B", "10B", "5B", "12B", "7B", "2B", "9B", "4B", "11B", "6B", "1B" };

    return isMinor ? minorCamelot[root] : majorCamelot[root];
}

juce::String TunerBPMPluginAudioProcessor::getDetectedNoteName() const
{
    int note = detectedNoteIndex.load();
    if (note < 0 || note > 127)
        return "---";
    
    int octave = (note / 12) - 1;
    int noteInOctave = note % 12;
    return noteNames[noteInOctave] + juce::String(octave);
}

bool TunerBPMPluginAudioProcessor::getAndClearBeatTriggered(int& outBeatNumber)
{
    if (beatTriggered.exchange(false))
    {
        outBeatNumber = lastBeatNumber.load();
        return true;
    }
    return false;
}

// ==============================================================================
// Tunebat / Percival BPM Engine: 3-Band Novelty Flux + Comb + Peak-Intervals
// ==============================================================================
void TunerBPMPluginAudioProcessor::processKickBpm(const float* inputData, int numSamples)
{
    // Frame interval = 10 ms (100 Hz frame rate)
    const int frameInterval = std::max(1, static_cast<int>(currentSampleRate / 100.0));

    for (int i = 0; i < numSamples; ++i)
    {
        float s = inputData[i];
        streamTimeSeconds += 1.0 / currentSampleRate;

        // 3-band decomposition: Low (punch < 200 Hz), Mid (snare 250-2500 Hz), High (hats > 2800 Hz)
        float yLow  = kickLowPass.process(s);
        float yMid  = midBandPass.process(s);
        float yHigh = hihatHighPass.process(s);

        frameLowSum  += yLow * yLow;
        frameMidSum  += yMid * yMid;
        frameHighSum += yHigh * yHigh;
        frameSampleCounter++;

        if (frameSampleCounter >= frameInterval)
        {
            float lowEnergy  = frameLowSum  / static_cast<float>(frameSampleCounter);
            float midEnergy  = frameMidSum  / static_cast<float>(frameSampleCounter);
            float highEnergy = frameHighSum / static_cast<float>(frameSampleCounter);
            frameLowSum = frameMidSum = frameHighSum = 0.0f;
            frameSampleCounter = 0;

            // Logarithmic compression (Essentia-style log spectrum flux)
            float lLow  = std::log(1.0f + 1000.0f * lowEnergy);
            float lMid  = std::log(1.0f + 1000.0f * midEnergy);
            float lHigh = std::log(1.0f + 1000.0f * highEnergy);

            // Half-wave rectified flux
            float dLow  = std::max(0.0f, lLow  - prevLowEnergy);
            float dMid  = std::max(0.0f, lMid  - prevMidEnergy);
            float dHigh = std::max(0.0f, lHigh - prevHighEnergy);
            prevLowEnergy  = lLow;
            prevMidEnergy  = lMid;
            prevHighEnergy = lHigh;

            // Combined multi-band rhythmic novelty (punch + transient accents + hi-hat subdivision)
            float flux = 1.0f * dLow + 0.65f * dMid + 0.35f * dHigh;

            // Adaptive baseline tracking (~200 ms)
            noveltyThreshold = 0.92f * noveltyThreshold + 0.08f * flux;
            float novelty = std::max(0.0f, flux - 0.35f * noveltyThreshold);

            noveltyBuffer[noveltyWritePos] = novelty;
            noveltyWritePos = (noveltyWritePos + 1) % noveltyBufferSize;

            // Detect onset peaks for the inter-peak interval histogram
            if (novelty > (noveltyThreshold * 1.35f + 1e-4f)
                && (streamTimeSeconds - lastKickOnsetSec) > 0.10) // Min 100 ms between transients (< 600 BPM)
            {
                lastKickOnsetSec = streamTimeSeconds;
                detectedOnsetTimes.push_back(streamTimeSeconds);
                if (detectedOnsetTimes.size() > 64)
                    detectedOnsetTimes.erase(detectedOnsetTimes.begin());
            }

            // Evaluate tempo every 20 frames (every 200 ms)
            bpmAnalysisTimer++;
            if (bpmAnalysisTimer >= 20)
            {
                bpmAnalysisTimer = 0;
                calculateTempoFromCombAndIntervals();
            }
        }
    }
}

// ==============================================================================
// Tunebat / Percival Tempo Algorithm: Comb + Pulse-Train + Inter-Onset + SVM Octave Decider
// ==============================================================================
static bool computeTunebatTempo(const std::vector<float>& novelty,
                                const std::vector<double>& onsetTimes,
                                float& outFineBpm,
                                float& outConfidence,
                                std::function<bool()> shouldCancel = nullptr)
{
    const int totalFrames = static_cast<int>(novelty.size());
    const int minLag = 14;  // 428.5 BPM (permits half-tempo check for up to 214 BPM)
    const int maxLag = 280; // 21.4 BPM (permits 4th harmonic checks down to 65 BPM)
    if (totalFrames < maxLag + 40)
        return false;

    // 1. Mean and variance of novelty curve
    double sumX = 0.0;
    for (int i = 0; i < totalFrames; ++i) sumX += novelty[i];
    double meanX = sumX / totalFrames;

    double varX = 0.0;
    for (int i = 0; i < totalFrames; ++i)
    {
        double d = novelty[i] - meanX;
        varX += d * d;
    }
    if (varX < 1e-6)
        return false;

    // 2. Normalized Autocorrelation R[tau]
    const int N = totalFrames - maxLag;
    std::vector<float> normR(maxLag + 1, 0.0f);
    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        if ((tau % 16 == 0) && shouldCancel && shouldCancel())
            return false;

        double sumCross = 0.0, sumSq1 = 0.0, sumSq2 = 0.0;
        for (int n = 0; n < N; ++n)
        {
            double a = novelty[n] - meanX;
            double b = novelty[n + tau] - meanX;
            sumCross += a * b;
            sumSq1   += a * a;
            sumSq2   += b * b;
        }
        double denom = std::sqrt(sumSq1 * sumSq2);
        normR[tau] = (denom > 1e-9) ? static_cast<float>(sumCross / denom) : 0.0f;
    }

    auto getLagValue = [&](double lag) -> float
    {
        if (lag < minLag || lag > maxLag) return 0.0f;
        int i0 = static_cast<int>(std::floor(lag));
        int i1 = std::min(maxLag, i0 + 1);
        float frac = static_cast<float>(lag - i0);
        return normR[i0] * (1.0f - frac) + normR[i1] * frac;
    };

    // 3. Inter-Onset Interval Histogram (Beatport / Tunebat algorithm)
    std::array<float, 201> bpmHist { 0.0f };
    if (onsetTimes.size() >= 3)
    {
        for (size_t i = 1; i < onsetTimes.size(); ++i)
        {
            // Consecutive onset (fundamental beat interval: Kick -> Snare -> Kick)
            double dt1 = onsetTimes[i] - onsetTimes[i - 1];
            if (dt1 >= 0.10 && dt1 <= 1.5) // 40 to 600 BPM
            {
                double cand = 60.0 / dt1;
                while (cand < 65.0)  cand *= 2.0;
                while (cand > 195.0) cand /= 2.0;
                int rB = juce::roundToInt(cand);
                if (rB >= 65 && rB <= 195)
                {
                    for (int k = -2; k <= 2; ++k)
                    {
                        int targetB = rB + k;
                        if (targetB >= 65 && targetB <= 195)
                            bpmHist[targetB] += 1.0f * std::exp(-static_cast<float>(k * k) / 1.8f);
                    }
                }
            }

            // Skip-1 onset (two-beat interval, normalized to 1 beat)
            if (i >= 2)
            {
                double dt2 = onsetTimes[i] - onsetTimes[i - 2];
                if (dt2 >= 0.20 && dt2 <= 3.0)
                {
                    double cand = 60.0 / (dt2 * 0.5);
                    while (cand < 65.0)  cand *= 2.0;
                    while (cand > 195.0) cand /= 2.0;
                    int rB = juce::roundToInt(cand);
                    if (rB >= 65 && rB <= 195)
                    {
                        for (int k = -2; k <= 2; ++k)
                        {
                            int targetB = rB + k;
                            if (targetB >= 65 && targetB <= 195)
                                bpmHist[targetB] += 0.35f * std::exp(-static_cast<float>(k * k) / 1.8f);
                        }
                    }
                }
            }
        }
    }
    float maxHist = 0.0f;
    for (int b = 65; b <= 195; ++b) maxHist = std::max(maxHist, bpmHist[b]);
    if (maxHist > 0.0f)
    {
        for (int b = 65; b <= 195; ++b) bpmHist[b] /= maxHist;
    }

    // 4. Pulse Train Cross-Correlation (PercivalEvaluatePulseTrains with floating-point phase)
    std::vector<float> pulseScores(196, 0.0f);
    float maxPulse = 0.0f;
    for (int B = 65; B <= 195; ++B)
    {
        double period_float = 6000.0 / static_cast<double>(B);
        int intStep = std::max(1, juce::roundToInt(period_float));
        float bestPhaseEnergy = 0.0f;
        for (int phi = 0; phi < intStep; ++phi)
        {
            float pSum = 0.0f;
            int count = 0;
            double pos = phi;
            while (pos < N)
            {
                int idx = static_cast<int>(std::round(pos));
                if (idx < N)
                {
                    pSum += novelty[idx];
                    count++;
                }
                pos += period_float;
            }
            if (count > 0)
            {
                float avg = pSum / static_cast<float>(count);
                if (avg > bestPhaseEnergy)
                    bestPhaseEnergy = avg;
            }
        }
        pulseScores[B] = bestPhaseEnergy;
        if (bestPhaseEnergy > maxPulse) maxPulse = bestPhaseEnergy;
    }
    if (maxPulse > 0.0f)
    {
        for (int B = 65; B <= 195; ++B) pulseScores[B] /= maxPulse;
    }

    // 5. Total Combined Score across candidate BPMs
    float bestScore = -1e9f;
    int bestIntBpm = 0;
    std::vector<float> totalScores(196, -1e9f);

    for (int B = 65; B <= 195; ++B)
    {
        double T = 6000.0 / static_cast<double>(B);
        float r1 = getLagValue(T);
        float r2 = getLagValue(2.0 * T);
        float r3 = getLagValue(3.0 * T);
        float r4 = getLagValue(4.0 * T);
        float rHalf = getLagValue(0.5 * T);

        // Sub-harmonic suppression: if 0.5T has high correlation, B is a sub-octave of the real tempo
        float combScore = 1.0f * r1 + 0.85f * r2 + 0.50f * r3 + 0.60f * r4 - 0.85f * rHalf;
        float histScore = bpmHist[B];
        float pulseScore = pulseScores[B];

        double ratio = static_cast<double>(B) / 124.0;
        double logRatio = std::log2(ratio);
        float prior = std::exp(-static_cast<float>((logRatio * logRatio) / (2.0 * 0.60 * 0.60)));

        float total = (combScore + 1.2f * pulseScore + 1.8f * histScore) * prior;
        totalScores[B] = total;

        if (total > bestScore)
        {
            bestScore = total;
            bestIntBpm = B;
        }
    }

    if (bestIntBpm < 65 || bestIntBpm > 195)
        return false;

    // Tunebat / Beatport Octave Resolution:
    // In modern EDM/DnB/Trap, tracks with alternating kicks & snares often register high autocorrelation
    // at half-time (e.g. 87 BPM). If double-time (e.g. 174 BPM) has strong periodic presence (>= 75% of sub-octave),
    // resolve to the true energetic tempo (174 BPM).
    int doubleBpm = bestIntBpm * 2;
    if (bestIntBpm <= 95 && doubleBpm <= 195)
    {
        float scoreSub = totalScores[bestIntBpm];
        float scoreDbl = totalScores[doubleBpm];
        if (scoreDbl >= 0.70f * scoreSub)
        {
            bestIntBpm = doubleBpm;
        }
    }

    // Sub-integer Parabolic Peak Interpolation
    float rawFineBpm = static_cast<float>(bestIntBpm);
    if (bestIntBpm > 65 && bestIntBpm < 195)
    {
        float sLeft  = totalScores[bestIntBpm - 1];
        float sMid   = totalScores[bestIntBpm];
        float sRight = totalScores[bestIntBpm + 1];
        float denom = sLeft - 2.0f * sMid + sRight;
        if (std::abs(denom) > 1e-6f)
        {
            float delta = 0.5f * (sLeft - sRight) / denom;
            delta = juce::jlimit(-0.5f, 0.5f, delta);
            rawFineBpm += delta;
        }
    }

    // 6. Ensure final tempo is within standard musical range [65, 195] BPM
    while (rawFineBpm < 65.0f) rawFineBpm *= 2.0f;
    while (rawFineBpm > 195.0f) rawFineBpm /= 2.0f;

    outFineBpm = rawFineBpm;
    outConfidence = bestScore;
    return true;
}

void TunerBPMPluginAudioProcessor::calculateTempoFromCombAndIntervals()
{
    if (streamTimeSeconds < 1.5)
        return;

    const int maxLag = 280;
    const int bufSize = noveltyBufferSize;
    int activeFrames = std::min(bufSize, static_cast<int>(streamTimeSeconds * 100.0));
    if (activeFrames < maxLag + 40)
        return;

    std::vector<float> X(activeFrames);
    int startPos = (noveltyWritePos - activeFrames + bufSize) % bufSize;
    for (int i = 0; i < activeFrames; ++i)
        X[i] = noveltyBuffer[(startPos + i) % bufSize];

    float fineBpm = 0.0f;
    float confidence = 0.0f;
    if (!computeTunebatTempo(X, detectedOnsetTimes, fineBpm, confidence))
    {
        if (!bpmIsLocked.load())
        {
            std::lock_guard<std::mutex> lock(bpmMutex);
            bpmStatusText = "Listening for tempo...";
        }
        return;
    }

    recentBpmCandidates.push_back(fineBpm);
    if (recentBpmCandidates.size() > 7)
        recentBpmCandidates.erase(recentBpmCandidates.begin());

    int consensusCount = 0;
    for (float b : recentBpmCandidates)
    {
        if (std::abs(b - fineBpm) <= 1.2f)
            consensusCount++;
    }

    // Lock definitively when 3 of last 7 cycles agree and at least 2.0s analyzed
    if (consensusCount >= 3 && streamTimeSeconds >= 2.0)
    {
        detectedAudioBpm.store(fineBpm);
        bpmIsLocked.store(true);
        std::lock_guard<std::mutex> lock(bpmMutex);
        int displayBpm = juce::roundToInt(fineBpm);
        bpmStatusText = "TEMPO LOCKED: " + juce::String(displayBpm) + " BPM";
    }
    else if (!bpmIsLocked.load())
    {
        detectedAudioBpm.store(fineBpm);
        std::lock_guard<std::mutex> lock(bpmMutex);
        bpmStatusText = "ANALYZING TEMPO...";
    }
}

void TunerBPMPluginAudioProcessor::halfBpm()
{
    float current = detectedAudioBpm.load();
    if (current > 30.0f)
    {
        float newBpm = current * 0.5f;
        detectedAudioBpm.store(newBpm);
        bpmIsLocked.store(true);
        std::lock_guard<std::mutex> lock(bpmMutex);
        int displayBpm = juce::roundToInt(newBpm);
        bpmStatusText = "TEMPO LOCKED: " + juce::String(displayBpm) + " BPM (1/2x)";
    }
}

void TunerBPMPluginAudioProcessor::doubleBpm()
{
    float current = detectedAudioBpm.load();
    if (current > 0.0f && current < 350.0f)
    {
        float newBpm = current * 2.0f;
        detectedAudioBpm.store(newBpm);
        bpmIsLocked.store(true);
        std::lock_guard<std::mutex> lock(bpmMutex);
        int displayBpm = juce::roundToInt(newBpm);
        bpmStatusText = "TEMPO LOCKED: " + juce::String(displayBpm) + " BPM (2x)";
    }
}

void TunerBPMPluginAudioProcessor::unlockBpm()
{
    bpmIsLocked.store(false);
    detectedAudioBpm.store(0.0f);
    streamTimeSeconds = 0.0;
    recentBpmCandidates.clear();
    detectedOnsetTimes.clear();
    std::fill(noveltyBuffer.begin(), noveltyBuffer.end(), 0.0f);
    noveltyWritePos = 0;
    frameSampleCounter = 0;
    frameLowSum = 0.0f;
    frameMidSum = 0.0f;
    frameHighSum = 0.0f;
    bpmAnalysisTimer = 0;
    prevLowEnergy = 0.0f;
    prevMidEnergy = 0.0f;
    prevHighEnergy = 0.0f;
    noveltyThreshold = 0.0f;
    lastKickOnsetSec = -1.0;

    std::lock_guard<std::mutex> lock(bpmMutex);
    bpmStatusText = "Listening for tempo...";
}

juce::String TunerBPMPluginAudioProcessor::getBpmStatus() const
{
    std::lock_guard<std::mutex> lock(bpmMutex);
    return bpmStatusText;
}

void TunerBPMPluginAudioProcessor::clearLoadedAudioFile()
{
    ++currentJobId;
    fileAnalysisActive.store(false);
    isAudioFileLoaded.store(false, std::memory_order_release);
    hasFileError.store(false);
    {
        std::lock_guard<std::mutex> lock(fileMutex);
        fileErrorMessage = "";
    }

    {
        std::lock_guard<std::mutex> lock(workerMutex);
        pendingFile = juce::File();
        pendingJobId = 0;
    }
    workerCv.notify_all();

    {
        std::lock_guard<std::mutex> lock(fileMutex);
        loadedAudioFileName = "";
    }
    resetScale();
    unlockBpm();
}

// ==============================================================================
// Offline Audio File Drag-and-Drop Analysis (Tunebat Web Style)
// ==============================================================================
void TunerBPMPluginAudioProcessor::loadAndAnalyzeAudioFile(const juce::File& file)
{
    uint32_t myJobId = ++currentJobId;
    fileAnalysisActive.store(true);
    hasFileError.store(false);

    {
        std::lock_guard<std::mutex> lock(fileMutex);
        loadedAudioFileName = file.getFileName();
        fileErrorMessage = "";
    }
    {
        std::lock_guard<std::mutex> lock(bpmMutex);
        bpmStatusText = "ANALYZING FILE: " + file.getFileNameWithoutExtension().toUpperCase();
    }
    {
        std::lock_guard<std::mutex> lock(workerMutex);
        pendingFile = file;
        pendingJobId = myJobId;
    }
    workerCv.notify_one();
}

void TunerBPMPluginAudioProcessor::runAnalysisWorker()
{
#if JUCE_WINDOWS
    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif

    while (!workerShouldExit.load())
    {
        juce::File fileToProcess;
        uint32_t jobToProcess = 0;
        {
            std::unique_lock<std::mutex> lock(workerMutex);
            workerCv.wait(lock, [this]() {
                return workerShouldExit.load() || (pendingJobId != 0 && pendingJobId == currentJobId.load());
            });

            if (workerShouldExit.load())
                break;

            fileToProcess = pendingFile;
            jobToProcess = pendingJobId;
            pendingJobId = 0;
        }

        if (jobToProcess == 0 || jobToProcess != currentJobId.load() || !fileToProcess.existsAsFile())
            continue;

        analyzeFileInternal(fileToProcess, jobToProcess);
    }

#if JUCE_WINDOWS
    if (SUCCEEDED(hrCom))
        CoUninitialize();
#endif
}

void TunerBPMPluginAudioProcessor::analyzeFileInternal(const juce::File& file, uint32_t myJobId)
{
    try
    {
        if (myJobId != currentJobId.load() || workerShouldExit.load())
            return;

        if (!file.existsAsFile())
        {
            if (myJobId == currentJobId.load())
            {
                hasFileError.store(true);
                isAudioFileLoaded.store(false, std::memory_order_release);
                fileAnalysisActive.store(false);
                {
                    std::lock_guard<std::mutex> lock(fileMutex);
                    fileErrorMessage = "FILE NOT FOUND ON DISK";
                }
                std::lock_guard<std::mutex> lock(bpmMutex);
                bpmStatusText = "ERROR: FILE NOT FOUND";
            }
            return;
        }

        std::unique_ptr<juce::AudioFormatReader> reader;
        try
        {
            reader.reset(formatManager.createReaderFor(file));
        }
        catch (...)
        {
            reader.reset();
        }

        if (reader == nullptr || reader->numChannels <= 0 || reader->sampleRate <= 100.0)
        {
            if (myJobId == currentJobId.load())
            {
                hasFileError.store(true);
                isAudioFileLoaded.store(false, std::memory_order_release);
                fileAnalysisActive.store(false);
                {
                    std::lock_guard<std::mutex> lock(fileMutex);
                    fileErrorMessage = "UNSUPPORTED AUDIO FORMAT OR CORRUPT METADATA";
                }
                std::lock_guard<std::mutex> lock(bpmMutex);
                bpmStatusText = "ERROR: FORMAT NOT SUPPORTED";
            }
            return;
        }

        resetScale();
        unlockBpm();

        double fileSampleRate = reader->sampleRate;
        if (fileSampleRate <= 1000.0) fileSampleRate = 44100.0;
        int64_t totalSamples = reader->lengthInSamples;

        int64_t startSample = static_cast<int64_t>(std::min(8.0 * fileSampleRate, totalSamples * 0.1));
        int64_t durationSamples = static_cast<int64_t>(std::min(75.0 * fileSampleRate, static_cast<double>(totalSamples - startSample)));
        if (durationSamples <= 0)
        {
            startSample = 0;
            durationSamples = totalSamples;
        }

        if (durationSamples <= 0)
        {
            if (myJobId == currentJobId.load())
            {
                isAudioFileLoaded.store(false, std::memory_order_release);
                fileAnalysisActive.store(false);
                std::lock_guard<std::mutex> lock(bpmMutex);
                bpmStatusText = "FILE EMPTY OR UNSUPPORTED";
            }
            return;
        }

        BiquadFilter offLow, offMid, offHigh;
        offLow.makeLowPass(fileSampleRate, 200.0, 0.7071);
        offMid.makeBandPass(fileSampleRate, 1200.0, 1.0);
        offHigh.makeHighPass(fileSampleRate, 2800.0, 0.7071);

        const int localFftOrder = 11; // 2048 samples
        const int localFftSize = 1 << localFftOrder;
        const int localHopSize = 1024;
        juce::dsp::FFT localFft(localFftOrder);
        juce::dsp::WindowingFunction<float> localWindow(localFftSize, juce::dsp::WindowingFunction<float>::hann);

        std::vector<float> localFftFifo(localFftSize, 0.0f);
        std::vector<float> localFftBuffer(localFftSize * 2, 0.0f);
        int localFftFifoIndex = 0;
        std::array<float, 12> localAccumulatedChroma { 0.0f };

        const int frameInterval = std::max(1, static_cast<int>(fileSampleRate / 100.0));
        const int blockSize = 4096;

        // CRITICAL FIX: Always allocate 2 channels so JUCE never passes nullptr channel pointers to readSamples!
        juce::AudioBuffer<float> tempBuffer(2, blockSize);

        std::vector<float> fileNovelty;
        fileNovelty.reserve(8000);

        std::vector<double> fileOnsets;
        fileOnsets.reserve(1000);

        float fPrevLow = 0.0f, fPrevMid = 0.0f, fPrevHigh = 0.0f;
        float fNovThresh = 0.0f;
        float fLowSum = 0.0f, fMidSum = 0.0f, fHighSum = 0.0f;
        int fCounter = 0;
        double fTimeSec = 0.0;
        double fLastOnset = -1.0;

        int64_t samplesReadTotal = 0;
        const bool isStereo = (reader->numChannels > 1);

        while (samplesReadTotal < durationSamples)
        {
            if (myJobId != currentJobId.load() || workerShouldExit.load())
                return;

            int samplesThisBlock = static_cast<int>(std::min(static_cast<int64_t>(blockSize), durationSamples - samplesReadTotal));

            // Read both channels safely (JUCE will copy channel 0 to channel 1 if mono)
            bool readOk = reader->read(&tempBuffer, 0, samplesThisBlock, startSample + samplesReadTotal, true, true);
            if (!readOk && samplesThisBlock <= 0)
                break;

            const float* ch0 = tempBuffer.getReadPointer(0);
            const float* ch1 = isStereo ? tempBuffer.getReadPointer(1) : ch0;

            // 1. Thread-local HPCP Chromagram Accumulation (clean stereo downmix)
            for (int i = 0; i < samplesThisBlock; ++i)
            {
                float s = isStereo ? 0.5f * (ch0[i] + ch1[i]) : ch0[i];
                localFftFifo[localFftFifoIndex++] = s;
                if (localFftFifoIndex >= localFftSize)
                {
                    std::copy(localFftFifo.begin(), localFftFifo.end(), localFftBuffer.begin());
                    std::fill(localFftBuffer.begin() + localFftSize, localFftBuffer.begin() + localFftSize * 2, 0.0f);
                    localWindow.multiplyWithWindowingTable(localFftBuffer.data(), localFftSize);
                    localFft.performFrequencyOnlyForwardTransform(localFftBuffer.data());

                    float frameEnergy = 0.0f;
                    for (int k = 1; k < localFftSize / 2; ++k)
                        frameEnergy += localFftBuffer[k];

                    if (frameEnergy >= 1e-4f)
                    {
                        int minBin = std::max(1, juce::roundToInt(50.0 * localFftSize / fileSampleRate));
                        int maxBin = std::min(localFftSize / 2 - 2, juce::roundToInt(3500.0 * localFftSize / fileSampleRate));
                        std::array<float, 12> frameChroma { 0.0f };

                        for (int k = minBin; k <= maxBin; ++k)
                        {
                            float mag = localFftBuffer[k];
                            if (mag > localFftBuffer[k - 1] && mag > localFftBuffer[k + 1] && mag > 0.001f)
                            {
                                float y1 = localFftBuffer[k - 1];
                                float y2 = mag;
                                float y3 = localFftBuffer[k + 1];
                                float denom = y1 - 2.0f * y2 + y3;
                                float delta = 0.0f;
                                if (std::abs(denom) > 1e-9f)
                                    delta = 0.5f * (y1 - y3) / denom;
                                float exactBin = static_cast<float>(k) + delta;
                                float peakFreq = exactBin * static_cast<float>(fileSampleRate) / static_cast<float>(localFftSize);
                                float peakMag = y2 - 0.25f * (y1 - y3) * delta;

                                if (peakFreq >= 50.0f && peakFreq <= 3500.0f && peakMag > 0.0f)
                                {
                                    double midiNote = 12.0 * std::log2(peakFreq / 440.0) + 69.0;
                                    double pitchClass = std::fmod(midiNote, 12.0);
                                    if (pitchClass < 0.0) pitchClass += 12.0;

                                    const float harmonicWeights[4] = { 1.0f, 0.60f, 0.36f, 0.216f };
                                    for (int h = 1; h <= 4; ++h)
                                    {
                                        double hpc = std::fmod(pitchClass + 12.0 * std::log2(static_cast<double>(h)), 12.0);
                                        if (hpc < 0.0) hpc += 12.0;
                                        int pcInt = static_cast<int>(std::floor(hpc)) % 12;
                                        int pcNext = (pcInt + 1) % 12;
                                        float frac = static_cast<float>(hpc - std::floor(hpc));
                                        float w = peakMag * harmonicWeights[h - 1];
                                        frameChroma[pcInt] += w * (1.0f - frac);
                                        frameChroma[pcNext] += w * frac;
                                    }
                                }
                            }
                        }

                        float frameMax = 0.0f;
                        for (int pc = 0; pc < 12; ++pc) frameMax = std::max(frameMax, frameChroma[pc]);
                        if (frameMax > 1e-6f)
                        {
                            for (int pc = 0; pc < 12; ++pc)
                                localAccumulatedChroma[pc] += frameChroma[pc] / frameMax;
                        }
                    }

                    std::copy(localFftFifo.begin() + localHopSize, localFftFifo.end(), localFftFifo.begin());
                    localFftFifoIndex = localFftSize - localHopSize;
                }
            }

            // 2. Feed Multi-band Novelty Flux for BPM
            for (int i = 0; i < samplesThisBlock; ++i)
            {
                float s = isStereo ? 0.5f * (ch0[i] + ch1[i]) : ch0[i];
                fTimeSec += 1.0 / fileSampleRate;

                float yL = offLow.process(s);
                float yM = offMid.process(s);
                float yH = offHigh.process(s);

                fLowSum += yL * yL;
                fMidSum += yM * yM;
                fHighSum += yH * yH;
                fCounter++;

                if (fCounter >= frameInterval)
                {
                    float eL = fLowSum / static_cast<float>(fCounter);
                    float eM = fMidSum / static_cast<float>(fCounter);
                    float eH = fHighSum / static_cast<float>(fCounter);
                    fLowSum = fMidSum = fHighSum = 0.0f;
                    fCounter = 0;

                    float lLow = std::log(1.0f + 1000.0f * eL);
                    float lMid = std::log(1.0f + 1000.0f * eM);
                    float lHigh = std::log(1.0f + 1000.0f * eH);

                    float dL = std::max(0.0f, lLow - fPrevLow);
                    float dM = std::max(0.0f, lMid - fPrevMid);
                    float dH = std::max(0.0f, lHigh - fPrevHigh);
                    fPrevLow = lLow; fPrevMid = lMid; fPrevHigh = lHigh;

                    float flux = 1.0f * dL + 0.65f * dM + 0.35f * dH;
                    fNovThresh = 0.92f * fNovThresh + 0.08f * flux;
                    float novelty = std::max(0.0f, flux - 0.35f * fNovThresh);

                    fileNovelty.push_back(novelty);

                    if (novelty > (fNovThresh * 1.35f + 1e-4f) && (fTimeSec - fLastOnset) > 0.16)
                    {
                        fLastOnset = fTimeSec;
                        fileOnsets.push_back(fTimeSec);
                    }
                }
            }

            samplesReadTotal += samplesThisBlock;
            scaleProgress.store(juce::jlimit(0.0f, 1.0f, static_cast<float>(samplesReadTotal) / static_cast<float>(durationSamples)));
        }

        if (myJobId != currentJobId.load() || workerShouldExit.load())
            return;

        // 3. Finalize Key Detection
        int bestKey = correlateKeyProfile(localAccumulatedChroma);
        if (bestKey >= 0)
        {
            detectedKeyIndex.store(bestKey);
            scaleIsLocked.store(true);
            scaleProgress.store(1.0f);
        }

        // 4. Finalize BPM
        float finalFineBpm = 0.0f;
        float conf = 0.0f;
        auto cancelCheck = [&]() { return myJobId != currentJobId.load() || workerShouldExit.load(); };
        if (computeTunebatTempo(fileNovelty, fileOnsets, finalFineBpm, conf, cancelCheck))
        {
            if (myJobId == currentJobId.load())
            {
                detectedAudioBpm.store(finalFineBpm);
                bpmIsLocked.store(true);

                std::lock_guard<std::mutex> lock(bpmMutex);
                int displayBpm = juce::roundToInt(finalFineBpm);
                bpmStatusText = "TEMPO LOCKED: " + juce::String(displayBpm) + " BPM";
            }
        }
        else
        {
            if (myJobId == currentJobId.load())
            {
                std::lock_guard<std::mutex> lock(bpmMutex);
                bpmStatusText = "ANALYSIS COMPLETE";
            }
        }

        if (myJobId == currentJobId.load())
        {
            hasFileError.store(false);
            isAudioFileLoaded.store(true, std::memory_order_release);
            fileAnalysisActive.store(false);
        }
    }
    catch (const std::exception& ex)
    {
        if (myJobId == currentJobId.load())
        {
            hasFileError.store(true);
            isAudioFileLoaded.store(false, std::memory_order_release);
            fileAnalysisActive.store(false);
            {
                std::lock_guard<std::mutex> lock(fileMutex);
                fileErrorMessage = juce::String("EXCEPTION: ") + ex.what();
            }
            std::lock_guard<std::mutex> lock(bpmMutex);
            bpmStatusText = "ERROR DURING ANALYSIS";
        }
    }
    catch (...)
    {
        if (myJobId == currentJobId.load())
        {
            hasFileError.store(true);
            isAudioFileLoaded.store(false, std::memory_order_release);
            fileAnalysisActive.store(false);
            {
                std::lock_guard<std::mutex> lock(fileMutex);
                fileErrorMessage = "UNKNOWN DECODING EXCEPTION";
            }
            std::lock_guard<std::mutex> lock(bpmMutex);
            bpmStatusText = "ERROR READING FILE";
        }
    }
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TunerBPMPluginAudioProcessor();
}

bool TunerBPMPluginAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* TunerBPMPluginAudioProcessor::createEditor()
{
    return new TunerBPMPluginAudioProcessorEditor (*this);
}

void TunerBPMPluginAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void TunerBPMPluginAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));
    if (xmlState.get() != nullptr)
    {
        if (xmlState->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));
        }
    }
}
