#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>
#include <numeric>

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
       apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
    pitchBuffer.resize(2048, 0.0f);
    noveltyBuffer.resize(noveltyBufferSize, 0.0f);
    oscilloscopeBuffer.resize(oscilloscopeSize, 0.0f);
    recentBpmCandidates.reserve(16);
    kickOnsetsRing.reserve(32);

    for (int i = 0; i < 36; ++i)
    {
        int midiNote = 48 + i; // C3 (48) to B5 (83)
        double freq = 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0);
        chromaFilters[i].makeBandPass(44100.0, freq, 18.0);
    }

    kickBandPass.makeBandPass(44100.0, 62.0, 1.3);
    kickLowPass.makeLowPass(44100.0, 110.0, 0.7071);
    midBandPass.makeBandPass(44100.0, 1200.0, 0.8);
}

TunerBPMPluginAudioProcessor::~TunerBPMPluginAudioProcessor()
{
}

const juce::String TunerBPMPluginAudioProcessor::getName() const
{
    return JucePlugin_Name;
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

    std::fill(pitchBuffer.begin(), pitchBuffer.end(), 0.0f);
    pitchBufferWritePos = 0;

    std::fill(noveltyBuffer.begin(), noveltyBuffer.end(), 0.0f);
    noveltyWritePos = 0;
    frameSampleCounter = 0;
    frameKickSum = 0.0f;
    frameMidSum = 0.0f;
    analysisFrameCounter = 0;

    for (int i = 0; i < 36; ++i)
    {
        int midiNote = 48 + i; // C3 (48) to B5 (83)
        double freq = 440.0 * std::pow(2.0, (midiNote - 69.0) / 12.0);
        chromaFilters[i].makeBandPass(sampleRate, freq, 18.0);
        chromaFilters[i].reset();
    }

    kickBandPass.makeBandPass(sampleRate, 62.0, 1.3);
    kickBandPass.reset();
    kickLowPass.makeLowPass(sampleRate, 110.0, 0.7071);
    kickLowPass.reset();
    midBandPass.makeBandPass(sampleRate, 1200.0, 0.8);
    midBandPass.reset();

    prevKickBlockEnergy = 0.0f;
    prevMidBlockEnergy = 0.0f;
    fluxThreshold = 0.0f;
    kickSlowEnergy = 0.0f;
    streamTimeSeconds = 0.0;
    lastKickOnsetSec = -1.0;
    kickOnsetsRing.clear();
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

    const float* inputData = buffer.getReadPointer(0);

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

    // 3. Polyphonic IIR Chromagram & Fast Scale Detection
    processPolyphonicChroma(inputData, numSamples);

    // 4. Kick-Drum Repetition & Fixed BPM Detection
    processKickBpm(inputData, numSamples);

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
// Polyphonic IIR Chromagram & Monotonic Scale Detection Engine
// ==============================================================================
void TunerBPMPluginAudioProcessor::processPolyphonicChroma(const float* inputData, int numSamples)
{
    // Part A: Monophonic Pitch for Bottom Tuner Dock
    for (int i = 0; i < numSamples; ++i)
    {
        float currentSample = inputData[i];

        if (i % 2 == 0)
        {
            float avg = currentSample;
            if (i + 1 < numSamples)
                avg = (currentSample + inputData[i + 1]) * 0.5f;

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

    // Part B: 36 Semitone IIR Bandpass Filters (Octaves 3, 4, 5: 130 Hz to 988 Hz)
    for (int i = 0; i < numSamples; ++i)
    {
        float s = inputData[i];
        for (int k = 0; k < 36; ++k)
        {
            float y = chromaFilters[k].process(s);
            chromaBlockEnergies[k] += y * y;
        }
    }
    chromaSampleCount += numSamples;

    if (chromaSampleCount >= 2048)
    {
        float totalBlockEnergy = 0.0f;
        for (int k = 0; k < 36; ++k)
            totalBlockEnergy += chromaBlockEnergies[k];

        if (totalBlockEnergy > 0.0001f) // Real musical audio present
        {
            audioAnalyzedSeconds += static_cast<double>(chromaSampleCount) / currentSampleRate;

            // Monotonically increasing progress: 0% to 100% over 12 seconds
            if (!scaleIsLocked.load())
            {
                float prog = juce::jlimit(0.0f, 1.0f, static_cast<float>(audioAnalyzedSeconds / 12.0));
                scaleProgress.store(prog);
            }

            // Fold 36 semitones into 12 chromatic pitch classes
            for (int pc = 0; pc < 12; ++pc)
            {
                float e = std::sqrt(chromaBlockEnergies[pc])
                        + std::sqrt(chromaBlockEnergies[pc + 12])
                        + std::sqrt(chromaBlockEnergies[pc + 24]);
                accumulatedChroma[pc] += e;
            }

            chromaEvalCounter++;
            if (chromaEvalCounter >= 10)
            {
                chromaEvalCounter = 0;
                runKeyCorrelation();
            }
        }

        std::fill(chromaBlockEnergies.begin(), chromaBlockEnergies.end(), 0.0f);
        chromaSampleCount = 0;
    }
}

void TunerBPMPluginAudioProcessor::runKeyCorrelation()
{
    if (audioAnalyzedSeconds < 2.0)
        return;

    // Temperley (1999) Cognitive Key Profiles (Gold standard for modern pop/rock/EDM)
    const float tempMajor[12] = { 5.0f, 2.0f, 3.5f, 2.0f, 4.5f, 4.0f, 2.0f, 4.5f, 2.0f, 3.5f, 1.5f, 4.0f };
    const float tempMinor[12] = { 5.0f, 2.0f, 3.5f, 4.5f, 2.0f, 4.0f, 2.0f, 4.5f, 3.5f, 2.0f, 1.5f, 4.0f };

    // Krumhansl-Schmuckler profiles for secondary confirmation
    const float kkMajor[12]   = { 6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f };
    const float kkMinor[12]   = { 6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f };

    float chromaMean = 0.0f;
    for (int c = 0; c < 12; ++c)
        chromaMean += accumulatedChroma[c];
    chromaMean /= 12.0f;

    float bestCorr = -2.0f;
    int bestKey = -1;

    // Test all 24 keys: 0..11 Major, 12..23 Minor
    for (int key = 0; key < 24; ++key)
    {
        bool isMinor = (key >= 12);
        int tonic = key % 12;
        const float* profT = isMinor ? tempMinor : tempMajor;
        const float* profK = isMinor ? kkMinor : kkMajor;

        float meanT = 0.0f, meanK = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            meanT += profT[i];
            meanK += profK[i];
        }
        meanT /= 12.0f;
        meanK /= 12.0f;

        float numT = 0.0f, denXT = 0.0f, denYT = 0.0f;
        float numK = 0.0f, denXK = 0.0f, denYK = 0.0f;

        for (int i = 0; i < 12; ++i)
        {
            float x = accumulatedChroma[(tonic + i) % 12] - chromaMean;
            float yt = profT[i] - meanT;
            float yk = profK[i] - meanK;

            numT  += x * yt;
            denXT += x * x;
            denYT += yt * yt;

            numK  += x * yk;
            denXK += x * x;
            denYK += yk * yk;
        }

        float denomT = std::sqrt(denXT * denYT);
        float rT = (denomT > 1e-9f) ? (numT / denomT) : -1.0f;

        float denomK = std::sqrt(denXK * denYK);
        float rK = (denomK > 1e-9f) ? (numK / denomK) : -1.0f;

        // Weighted combination heavily favoring Temperley profile
        float r = 0.75f * rT + 0.25f * rK;

        if (r > bestCorr)
        {
            bestCorr = r;
            bestKey = key;
        }
    }

    if (bestKey >= 0)
    {
        detectedKeyIndex.store(bestKey);

        // Lock definitively when analyzed time reaches ~10s or high correlation after 6s
        if (audioAnalyzedSeconds >= 10.0 || (bestCorr >= 0.70f && audioAnalyzedSeconds >= 6.0))
        {
            scaleIsLocked.store(true);
            scaleProgress.store(1.0f);
        }
    }
}

void TunerBPMPluginAudioProcessor::resetScale()
{
    std::fill(accumulatedChroma.begin(), accumulatedChroma.end(), 0.0f);
    std::fill(chromaBlockEnergies.begin(), chromaBlockEnergies.end(), 0.0f);
    audioAnalyzedSeconds = 0.0;
    chromaSampleCount = 0;
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
// Harmonic Comb Resonator & Kick Alignment Tempo Detection Engine (v2.0)
// ==============================================================================
void TunerBPMPluginAudioProcessor::processKickBpm(const float* inputData, int numSamples)
{
    const int frameInterval = std::max(1, static_cast<int>(currentSampleRate / 200.0)); // 200 Hz frame rate (5 ms)

    for (int i = 0; i < numSamples; ++i)
    {
        float s = inputData[i];
        streamTimeSeconds += 1.0 / currentSampleRate;

        // Kick band (62 Hz punch + 110 Hz lowpass) & Mid band (1200 Hz snare/clap)
        float yKick = kickLowPass.process(kickBandPass.process(s));
        float yMid  = midBandPass.process(s);

        frameKickSum += yKick * yKick;
        frameMidSum  += yMid * yMid;
        frameSampleCounter++;

        if (frameSampleCounter >= frameInterval)
        {
            float kickEnergy = frameKickSum / static_cast<float>(frameSampleCounter);
            float midEnergy  = frameMidSum  / static_cast<float>(frameSampleCounter);
            frameKickSum = 0.0f;
            frameMidSum = 0.0f;
            frameSampleCounter = 0;

            // Half-wave rectified novelty flux
            float dKick = std::max(0.0f, kickEnergy - prevKickBlockEnergy);
            float dMid  = std::max(0.0f, midEnergy  - prevMidBlockEnergy);
            prevKickBlockEnergy = kickEnergy;
            prevMidBlockEnergy  = midEnergy;

            float flux = dKick + 0.35f * dMid;

            // Adaptive moving average threshold subtraction (~250 ms)
            fluxThreshold = 0.95f * fluxThreshold + 0.05f * flux;
            float novelty = std::max(0.0f, flux - 1.15f * fluxThreshold);

            noveltyBuffer[noveltyWritePos] = novelty;
            noveltyWritePos = (noveltyWritePos + 1) % noveltyBufferSize;

            // Background slow energy average (5-second time constant at 200 Hz)
            kickSlowEnergy = 0.999f * kickSlowEnergy + 0.001f * kickEnergy;

            // Discrete kick onset detection: sharp low-end pulse above background
            if (kickEnergy > (kickSlowEnergy * 2.2f + 0.0001f)
                && (streamTimeSeconds - lastKickOnsetSec) > 0.23)
            {
                lastKickOnsetSec = streamTimeSeconds;
                kickOnsetsRing.push_back(streamTimeSeconds);
                if (kickOnsetsRing.size() > 32)
                    kickOnsetsRing.erase(kickOnsetsRing.begin());
            }

            // Run tempo comb evaluation every 40 frames (every 200 ms)
            analysisFrameCounter++;
            if (analysisFrameCounter >= 40)
            {
                analysisFrameCounter = 0;
                calculateTempoFromCombResonator();
            }
        }
    }
}

void TunerBPMPluginAudioProcessor::calculateTempoFromCombResonator()
{
    if (audioAnalyzedSeconds < 1.5)
        return;

    const int bufSize = noveltyBufferSize; // 1200 frames = 6 seconds
    std::vector<float> X(bufSize);
    int startPos = (noveltyWritePos - bufSize + bufSize) % bufSize;
    for (int i = 0; i < bufSize; ++i)
        X[i] = noveltyBuffer[(startPos + i) % bufSize];

    double sumX = 0.0;
    for (int i = 0; i < bufSize; ++i) sumX += X[i];
    double meanX = sumX / bufSize;

    double varX = 0.0;
    for (int i = 0; i < bufSize; ++i)
    {
        double d = X[i] - meanX;
        varX += d * d;
    }

    if (varX < 0.0001)
    {
        if (!bpmIsLocked.load())
        {
            std::lock_guard<std::mutex> lock(bpmMutex);
            bpmStatusText = "Listening for tempo...";
        }
        return;
    }

    // Normalized autocorrelation r[tau] for tau from 60 (200 BPM) to 550 (21.8 BPM)
    const int minLag = 60;
    const int maxLag = 550;
    const int N = bufSize - maxLag; // 650 frames = 3.25 seconds analysis window

    std::vector<float> normR(maxLag + 1, 0.0f);
    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        double sumCross = 0.0;
        double sumSq1 = 0.0;
        double sumSq2 = 0.0;
        for (int n = 0; n < N; ++n)
        {
            double a = X[n] - meanX;
            double b = X[n + tau] - meanX;
            sumCross += a * b;
            sumSq1 += a * a;
            sumSq2 += b * b;
        }
        double denom = std::sqrt(sumSq1 * sumSq2);
        normR[tau] = (denom > 1e-9) ? static_cast<float>(sumCross / denom) : 0.0f;
    }

    float bestScore = -1e9f;
    int bestBpm = 0;

    for (int B = 70; B <= 185; ++B)
    {
        double T_sec = 60.0 / static_cast<double>(B);
        double L_frame = T_sec * 200.0;

        int L1 = juce::roundToInt(L_frame);
        int L2 = juce::roundToInt(2.0 * L_frame);
        int L3 = juce::roundToInt(3.0 * L_frame);
        int L4 = juce::roundToInt(4.0 * L_frame);

        float r1 = (L1 >= minLag && L1 <= maxLag) ? normR[L1] : 0.0f;
        float r2 = (L2 >= minLag && L2 <= maxLag) ? normR[L2] : 0.0f;
        float r3 = (L3 >= minLag && L3 <= maxLag) ? normR[L3] : 0.0f;
        float r4 = (L4 >= minLag && L4 <= maxLag) ? normR[L4] : 0.0f;

        // Multi-harmonic comb summation
        float combScore = 1.0f * r1 + 0.95f * r2 + 0.40f * r3 + 0.85f * r4;

        // Kick transient interval alignment score
        float kickScore = 0.0f;
        if (kickOnsetsRing.size() >= 3)
        {
            int numPairs = 0;
            for (size_t i = 0; i < kickOnsetsRing.size(); ++i)
            {
                for (size_t j = 0; j < i; ++j)
                {
                    double dt = kickOnsetsRing[i] - kickOnsetsRing[j];
                    if (dt >= 0.25 && dt <= 4.2)
                    {
                        double beats = dt / T_sec;
                        double nearestGrid = std::round(beats * 2.0) / 2.0;
                        double err = std::abs(dt - nearestGrid * T_sec);
                        if (err < 0.045)
                        {
                            float g = std::exp(-static_cast<float>((err * err) / (2.0 * 0.022 * 0.022)));
                            kickScore += g;
                            numPairs++;
                        }
                    }
                }
            }
            if (numPairs > 0)
                kickScore /= static_cast<float>(numPairs);
        }

        // Tempo prior centered at 112 BPM (covers typical pop/trap/urban comfortably)
        double ratio = static_cast<double>(B) / 112.0;
        double logRatio = std::log2(ratio);
        float prior = std::exp(-static_cast<float>((logRatio * logRatio) / (2.0 * 0.42 * 0.42)));

        float totalScore = (combScore + 1.2f * kickScore) * prior;

        if (totalScore > bestScore)
        {
            bestScore = totalScore;
            bestBpm = B;
        }
    }

    if (bestBpm >= 70 && bestBpm <= 185)
    {
        recentBpmCandidates.push_back(bestBpm);
        if (recentBpmCandidates.size() > 8)
            recentBpmCandidates.erase(recentBpmCandidates.begin());

        int consensusCount = 0;
        for (int b : recentBpmCandidates)
        {
            if (std::abs(b - bestBpm) <= 1)
                consensusCount++;
        }

        // Lock definitively when 4 of 8 cycles agree and at least 3.0 seconds analyzed
        if (consensusCount >= 4 && audioAnalyzedSeconds >= 3.0)
        {
            detectedAudioBpm.store(static_cast<float>(bestBpm));
            bpmIsLocked.store(true);
            std::lock_guard<std::mutex> lock(bpmMutex);
            bpmStatusText = "TEMPO LOCKED: " + juce::String(bestBpm) + " BPM";
        }
        else if (!bpmIsLocked.load())
        {
            detectedAudioBpm.store(static_cast<float>(bestBpm));
            std::lock_guard<std::mutex> lock(bpmMutex);
            bpmStatusText = "ANALYZING TEMPO...";
        }
    }
}

void TunerBPMPluginAudioProcessor::unlockBpm()
{
    bpmIsLocked.store(false);
    detectedAudioBpm.store(0.0f);
    recentBpmCandidates.clear();
    kickOnsetsRing.clear();
    std::fill(noveltyBuffer.begin(), noveltyBuffer.end(), 0.0f);
    noveltyWritePos = 0;
    frameSampleCounter = 0;
    frameKickSum = 0.0f;
    frameMidSum = 0.0f;
    analysisFrameCounter = 0;
    prevKickBlockEnergy = 0.0f;
    prevMidBlockEnergy = 0.0f;
    fluxThreshold = 0.0f;
    kickSlowEnergy = 0.0f;
    lastKickOnsetSec = -1.0;

    std::lock_guard<std::mutex> lock(bpmMutex);
    bpmStatusText = "Listening for tempo...";
}

juce::String TunerBPMPluginAudioProcessor::getBpmStatus() const
{
    std::lock_guard<std::mutex> lock(bpmMutex);
    return bpmStatusText;
}

// Global factory function
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
