#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

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
    inputRingBuffer.resize(8192, 0.0f);
    downsampledBuffer.resize(2048, 0.0f);
    oscilloscopeBuffer.resize(oscilloscopeSize, 0.0f);
    chromaAccumulator.resize(12, 0.0f);
    onsetHistory.resize(1024, 0.0f);
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
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool TunerBPMPluginAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool TunerBPMPluginAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double TunerBPMPluginAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int TunerBPMPluginAudioProcessor::getNumPrograms()
{
    return 1;   // NB: some hosts don't cope very well if you tell them there are 0 programs,
                // so this should be at least 1, even if you're not really implementing them.
}

int TunerBPMPluginAudioProcessor::getCurrentProgram()
{
    return 0;
}

void TunerBPMPluginAudioProcessor::setCurrentProgram (int index)
{
}

const juce::String TunerBPMPluginAudioProcessor::getProgramName (int index)
{
    return {};
}

void TunerBPMPluginAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
}

void TunerBPMPluginAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    clickSampleRate = sampleRate;
    
    std::fill(inputRingBuffer.begin(), inputRingBuffer.end(), 0.0f);
    ringBufferWritePos = 0;
    
    std::fill(downsampledBuffer.begin(), downsampledBuffer.end(), 0.0f);
    downsampleWritePos = 0;
    
    clickSampleIndex = 0;
    clickActive = false;
    clickLengthSamples = 0.05 * sampleRate; // 50 ms click window
    
    internalSampleCounter = 0.0;
    internalBeatNumber = 0;
    lastPPQ = -1.0;

    resetScale();

    envelopeFollower = 0.0f;
    prevEnvelope = 0.0f;
    onsetSampleCounter = 0;
    onsetDownsampleRate = static_cast<int>(sampleRate / 100.0);
    if (onsetDownsampleRate <= 0) onsetDownsampleRate = 441;
    std::fill(onsetHistory.begin(), onsetHistory.end(), 0.0f);
    onsetHistoryWritePos = 0;
    detectedAudioBpm.store(0.0f);
}

void TunerBPMPluginAudioProcessor::releaseResources()
{
}

bool TunerBPMPluginAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // FL Studio a veces prueba combinaciones de buses inusuales (como Mono -> Stereo)
    // Es más seguro devolver true si la salida es válida.
    return true;
  #endif
}

void TunerBPMPluginAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    auto totalNumInputChannels  = getTotalNumInputChannels();
    auto totalNumOutputChannels = getTotalNumOutputChannels();

    // Clear extra output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return;

    // --- Oscilloscope & Tuner Input Capture ---
    const float* inputData = buffer.getReadPointer(0);
    
    // Save to oscilloscope
    {
        std::lock_guard<std::mutex> lock(oscMutex);
        for (int i = 0; i < numSamples; ++i)
        {
            oscilloscopeBuffer[oscWritePos] = inputData[i];
            oscWritePos = (oscWritePos + 1) % oscilloscopeSize;
        }
    }

    // Run pitch detection
    processPitchDetection(inputData, numSamples);

    // Run audio BPM detection
    processAudioBpm(inputData, numSamples);

    // --- Metronome DSP ---
    // Read Parameters
    float clickVolume = *apvts.getRawParameterValue("volume");
    bool clickMute = *apvts.getRawParameterValue("mute");
    float internalBpm = *apvts.getRawParameterValue("internalTempo");
    int mode = static_cast<int>(*apvts.getRawParameterValue("syncMode"));
    bool internalIsPlaying = *apvts.getRawParameterValue("internalPlay");

    // Gather PlayHead data
    auto* playHead = getPlayHead();
    juce::Optional<juce::AudioPlayHead::PositionInfo> positionInfo;
    if (playHead != nullptr)
    {
        positionInfo = playHead->getPosition();
    }

    bool useDAWSync = (mode == 0) && positionInfo.hasValue();

    if (useDAWSync)
    {
        auto info = *positionInfo;
        double bpm = info.getBpm().orFallback(120.0);
        bool isPlaying = info.getIsPlaying();
        double ppqStart = info.getPpqPosition().orFallback(0.0);
        int numerator = info.getTimeSignature().orFallback(juce::AudioPlayHead::TimeSignature{4, 4}).numerator;
        if (numerator <= 0) numerator = 4;

        currentTempo.store(static_cast<float>(bpm));
        hostIsPlaying.store(isPlaying);

        if (isPlaying)
        {
            double ppqPerSample = (bpm / 60.0) / currentSampleRate;
            
            if (lastPPQ < 0.0 || std::abs(ppqStart - lastPPQ) > 0.5)
            {
                lastPPQ = ppqStart;
            }

            for (int i = 0; i < numSamples; ++i)
            {
                double currentPpq = ppqStart + i * ppqPerSample;

                // Sync beat trigger on integer boundary crossings
                if (std::floor(currentPpq) > std::floor(lastPPQ))
                {
                    int beatNum = (static_cast<int>(std::floor(currentPpq)) % numerator) + 1;
                    
                    clickActive = true;
                    clickSampleIndex = 0;
                    clickFrequency = (beatNum == 1) ? 1000.0 : 600.0;
                    
                    lastBeatNumber.store(beatNum);
                    beatTriggered.store(true);
                }

                lastPPQ = currentPpq;

                if (clickActive && !clickMute)
                {
                    // Generate clean sine with envelope
                    double phase = 2.0 * juce::double_Pi * clickFrequency * clickSampleIndex / clickSampleRate;
                    float clickVal = static_cast<float>(std::sin(phase));
                    
                    double t = static_cast<double>(clickSampleIndex) / clickSampleRate;
                    float envelope = static_cast<float>(std::exp(-t / 0.008)); // Snappy 8ms decay
                    
                    clickSampleIndex++;
                    if (clickSampleIndex >= clickLengthSamples)
                    {
                        clickActive = false;
                        clickSampleIndex = 0;
                    }

                    float finalSample = clickVal * envelope * clickVolume;
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        buffer.addSample(ch, i, finalSample);
                    }
                }
            }
        }
        else
        {
            // Not playing, clear clicks
            clickActive = false;
            clickSampleIndex = 0;
            lastPPQ = -1.0;
        }
    }
    else
    {
        // Internal clock mode or fallback
        currentTempo.store(internalBpm);
        hostIsPlaying.store(internalIsPlaying);

        if (internalIsPlaying)
        {
            double samplesPerBeat = (60.0 / internalBpm) * currentSampleRate;

            for (int i = 0; i < numSamples; ++i)
            {
                internalSampleCounter += 1.0;
                if (internalSampleCounter >= samplesPerBeat)
                {
                    internalSampleCounter -= samplesPerBeat;
                    internalBeatNumber = (internalBeatNumber % 4) + 1;

                    clickActive = true;
                    clickSampleIndex = 0;
                    clickFrequency = (internalBeatNumber == 1) ? 1000.0 : 600.0;

                    lastBeatNumber.store(internalBeatNumber);
                    beatTriggered.store(true);
                }

                if (clickActive && !clickMute)
                {
                    double phase = 2.0 * juce::double_Pi * clickFrequency * clickSampleIndex / clickSampleRate;
                    float clickVal = static_cast<float>(std::sin(phase));
                    
                    double t = static_cast<double>(clickSampleIndex) / clickSampleRate;
                    float envelope = static_cast<float>(std::exp(-t / 0.008));
                    
                    clickSampleIndex++;
                    if (clickSampleIndex >= clickLengthSamples)
                    {
                        clickActive = false;
                        clickSampleIndex = 0;
                    }

                    float finalSample = clickVal * envelope * clickVolume;
                    for (int ch = 0; ch < numChannels; ++ch)
                    {
                        buffer.addSample(ch, i, finalSample);
                    }
                }
            }
        }
        else
        {
            // Internal clock paused
            clickActive = false;
            clickSampleIndex = 0;
            internalSampleCounter = 0.0;
            internalBeatNumber = 0;
        }
    }
}

void TunerBPMPluginAudioProcessor::processPitchDetection(const float* inputData, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float currentSample = inputData[i];
        
        // Accumulate in downsampled buffer by averaging consecutive pairs
        if (i % 2 == 0)
        {
            float avg = currentSample;
            if (i + 1 < numSamples)
            {
                avg = (currentSample + inputData[i + 1]) * 0.5f;
            }
            
            downsampledBuffer[downsampleWritePos] = avg;
            downsampleWritePos = (downsampleWritePos + 1) % 2048;
            
            // Run Autocorrelation pitch estimator every 512 samples (~23 ms)
            if (downsampleWritePos % 512 == 0)
            {
                runAutocorrelation();
            }
        }
    }
}

void TunerBPMPluginAudioProcessor::runAutocorrelation()
{
    float x[2048];
    int startPos = (downsampleWritePos - 2048 + 2048) % 2048;
    for (int i = 0; i < 2048; ++i)
    {
        x[i] = downsampledBuffer[(startPos + i) % 2048];
    }
    
    double fs = currentSampleRate * 0.5;
    
    // Search frequency range 40 Hz to 1000 Hz
    int maxLag = static_cast<int>(fs / 40.0);
    int minLag = static_cast<int>(fs / 1000.0);
    
    maxLag = std::min(maxLag, 1024);
    minLag = std::max(minLag, 2);
    
    double maxCorr = -1e10;
    int bestLag = -1;
    
    const int N = 1024;
    std::vector<double> r(maxLag + 1, 0.0);
    
    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        double sum = 0.0;
        for (int n = 0; n < N; ++n)
        {
            sum += static_cast<double>(x[n]) * static_cast<double>(x[n + tau]);
        }
        r[tau] = sum;
    }
    
    // Find peaks
    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        if (r[tau] > r[tau - 1] && r[tau] > r[tau + 1])
        {
            if (r[tau] > maxCorr)
            {
                maxCorr = r[tau];
                bestLag = tau;
            }
        }
    }
    
    if (bestLag >= minLag && bestLag <= maxLag && maxCorr > 0.0)
    {
        double energy = 0.0;
        for (int n = 0; n < N; ++n) {
            energy += static_cast<double>(x[n]) * static_cast<double>(x[n]);
        }
        
        if (energy < 0.001) // noise gate más estricto
        {
            detectedFrequency.store(0.0f);
            centsDeviation.store(0.0f);
            detectedNoteIndex.store(-1);
            return;
        }
        
        // Comprobar la claridad del pitch (relación entre pico de correlación y energía)
        double clarity = maxCorr / energy;
        if (clarity < 0.6)
        {
            detectedFrequency.store(0.0f);
            centsDeviation.store(0.0f);
            detectedNoteIndex.store(-1);
            return;
        }
        
        // Parabolic Interpolation
        double alpha = r[bestLag - 1];
        double beta = r[bestLag];
        double gamma = r[bestLag + 1];
        
        double denom = alpha - 2.0 * beta + gamma;
        double p = 0.0;
        if (std::abs(denom) > 1e-9)
        {
            p = 0.5 * (alpha - gamma) / denom;
        }
        
        double exactLag = static_cast<double>(bestLag) + p;
        double freq = fs / exactLag;
        
        if (freq >= 30.0 && freq <= 2000.0)
        {
            detectedFrequency.store(static_cast<float>(freq));
            
            double d = 12.0 * std::log2(freq / 440.0) + 69.0;
            int note = static_cast<int>(std::round(d));
            float cents = static_cast<float>(100.0 * (d - note));
            
            centsDeviation.store(cents);
            detectedNoteIndex.store(note);
            
            // Si la claridad es alta, registramos la nota
            if (clarity > 0.85f)
            {
                int chroma = (note % 12 + 12) % 12; 
                chromaAccumulator[chroma] += 0.2f;
                if (chromaAccumulator[chroma] > 10.0f) {
                    chromaAccumulator[chroma] = 10.0f;
                }
            }
            
            // Decaimiento extremadamente lento (memoria de ~30-60 segundos)
            for (int i = 0; i < 12; ++i) {
                chromaAccumulator[i] *= 0.999f;
            }

            // Run scale detection periodically (every 10 detections)
            static int scaleDetections = 0;
            scaleDetections++;
            if (scaleDetections >= 10)
            {
                scaleDetections = 0;
                runScaleDetection();
            }
        }
    }
    else
    {
        detectedFrequency.store(0.0f);
        centsDeviation.store(0.0f);
        detectedNoteIndex.store(-1);
    }
}

juce::String TunerBPMPluginAudioProcessor::getDetectedNoteName() const
{
    int note = detectedNoteIndex.load();
    if (note < 0 || note > 127) {
        return "---";
    }
    
    int octave = (note / 12) - 1;
    int noteInOctave = note % 12;
    
    return noteNames[noteInOctave] + juce::String(octave);
}

juce::String TunerBPMPluginAudioProcessor::getDetectedScaleName() const
{
    int keyIndex = detectedKeyIndex.load();
    if (keyIndex < 0)
        return "Detecting...";

    int root = keyIndex % 12;
    bool isMinor = (keyIndex >= 12);

    const juce::String rootNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return rootNames[root] + (isMinor ? " Minor" : " Major");
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

void TunerBPMPluginAudioProcessor::resetScale()
{
    std::fill(chromaAccumulator.begin(), chromaAccumulator.end(), 0.0f);
    detectedKeyIndex.store(-1);
    scaleConfidence.store(0.0f);
}

void TunerBPMPluginAudioProcessor::runScaleDetection()
{
    // Find total energy
    float sumChroma = 0.0f;
    float maxChroma = 0.0f;
    for (int i = 0; i < 12; ++i) 
    {
        sumChroma += chromaAccumulator[i];
        if (chromaAccumulator[i] > maxChroma) maxChroma = chromaAccumulator[i];
    }
    
    if (sumChroma < 15.0f || maxChroma < 2.0f) // Not enough notes collected yet
    {
        detectedKeyIndex.store(-1);
        scaleConfidence.store(0.0f);
        return;
    }
    
    // Standard Krumhansl-Kessler profiles
    const float kkMajor[12] = {6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f};
    const float kkMinor[12] = {6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f};
    
    int bestKey = -1;
    float bestCorr = -1.0f;
    
    // Check 12 Major keys (0..11) and 12 Minor keys (12..23)
    for (int key = 0; key < 24; ++key)
    {
        bool isMinor = (key >= 12);
        int root = key % 12;
        
        const float* profile = isMinor ? kkMinor : kkMajor;
        
        float corr = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            // Usar cromas normalizados por el pico máximo (0.0 a 1.0)
            float normVal = chromaAccumulator[(root + i) % 12] / maxChroma;
            corr += normVal * profile[i];
        }
        
        if (corr > bestCorr)
        {
            bestCorr = corr;
            bestKey = key;
        }
    }
    
    detectedKeyIndex.store(bestKey);
    
    // Con croma normalizado, un 'perfect match' ronda los 18-20 puntos, y el ruido aleatorio ronda los 10-12.
    // Mapeamos para que la barra se llene progresivamente hasta el 100%.
    float conf = (bestCorr - 10.0f) / 8.0f;
    scaleConfidence.store(std::max(0.0f, std::min(1.0f, conf)));
}

void TunerBPMPluginAudioProcessor::processAudioBpm(const float* inputData, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        float absX = std::abs(inputData[i]);
        
        // Seguidor de envolvente asimétrico (ataque rápido, caída lenta)
        if (absX > envelopeFollower) {
            envelopeFollower = 0.2f * envelopeFollower + 0.8f * absX; // Fast attack
        } else {
            envelopeFollower = 0.9995f * envelopeFollower + 0.0005f * absX; // Slow release
        }
        
        // Downsample envelope difference (transient detection) to 100Hz
        onsetSampleCounter++;
        if (onsetSampleCounter >= onsetDownsampleRate)
        {
            onsetSampleCounter = 0;
            
            float onsetVal = envelopeFollower - prevEnvelope;
            if (onsetVal < 0.0f) onsetVal = 0.0f;
            
            // Soft compress transients
            onsetVal = std::log1pf(onsetVal * 25.0f);
            
            onsetHistory[onsetHistoryWritePos] = onsetVal;
            onsetHistoryWritePos = (onsetHistoryWritePos + 1) % 1024;
            
            prevEnvelope = envelopeFollower;
            
            // Run tempo autocorrelation once per second (every 100 samples)
            static int tempoDetections = 0;
            tempoDetections++;
            if (tempoDetections >= 100)
            {
                tempoDetections = 0;
                runTempoAutocorrelation();
            }
        }
    }
}

void TunerBPMPluginAudioProcessor::runTempoAutocorrelation()
{
    float H[1024];
    int startPos = (onsetHistoryWritePos - 1024 + 1024) % 1024;
    for (int i = 0; i < 1024; ++i)
    {
        H[i] = onsetHistory[(startPos + i) % 1024];
    }
    
    // Restringir el rango de búsqueda a 75 BPM - 150 BPM
    // A 100 Hz: 150 BPM = 40 samples (100 * 60 / 150)
    // A 100 Hz: 75 BPM = 80 samples (100 * 60 / 75)
    int minLag = 40;
    int maxLag = 80;
    
    double maxCorr = -1e10;
    int bestLag = -1;
    
    int N = 1024 - maxLag;
    std::vector<double> r(maxLag + 1, 0.0);
    
    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        double sum = 0.0;
        for (int n = 0; n < N; ++n)
        {
            sum += static_cast<double>(H[n]) * static_cast<double>(H[n + tau]);
        }
        
        r[tau] = sum;
    }
    
    for (int tau = minLag; tau <= maxLag; ++tau)
    {
        if (r[tau] > r[tau - 1] && r[tau] > r[tau + 1])
        {
            if (r[tau] > maxCorr)
            {
                maxCorr = r[tau];
                bestLag = tau;
            }
        }
    }
    
    if (bestLag >= minLag && bestLag <= maxLag && maxCorr > 0.0)
    {
        double energy = 0.0;
        for (int n = 0; n < 1024; ++n) energy += H[n] * H[n];
        
        if (energy > 0.02) // beat threshold ajustado para ventana más larga
        {
            double bpm = 60.0 * 100.0 / bestLag;
            detectedAudioBpm.store(static_cast<float>(bpm));
        }
        else
        {
            detectedAudioBpm.store(0.0f);
        }
    }
    else
    {
        detectedAudioBpm.store(0.0f);
    }
}

// Global factory function required by JUCE
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TunerBPMPluginAudioProcessor();
}
