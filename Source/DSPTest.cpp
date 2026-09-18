#include <iostream>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "========================================" << std::endl;
    std::cout << "SUPREME TUNER BPM V.2 - TUNEBAT TEST SUITE" << std::endl;
    std::cout << "========================================" << std::endl;

    TunerBPMPluginAudioProcessor processor;
    double sampleRate = 44100.0;
    int blockSize = 512;
    processor.prepareToPlay(sampleRate, blockSize);

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    bool allPassed = true;

    auto testKey = [&](const std::string& testName, double f1, double f2, double f3,
                       const std::string& expectedScale, const std::string& expectedCamelot)
    {
        std::cout << "\n[" << testName << "] Frequencies: " << f1 << ", " << f2 << ", " << f3 << " Hz..." << std::endl;
        processor.resetScale();
        processor.unlockBpm();

        double p1 = 0.0, p2 = 0.0, p3 = 0.0;
        int totalBlocks = static_cast<int>((4.0 * sampleRate) / blockSize);
        for (int b = 0; b < totalBlocks; ++b)
        {
            float* channelData = buffer.getWritePointer(0);
            for (int i = 0; i < blockSize; ++i)
            {
                float s = 0.33f * (std::sin(p1) + std::sin(p2) + std::sin(p3));
                p1 += 2.0 * juce::double_Pi * f1 / sampleRate;
                p2 += 2.0 * juce::double_Pi * f2 / sampleRate;
                p3 += 2.0 * juce::double_Pi * f3 / sampleRate;
                channelData[i] = s;
            }
            buffer.copyFrom(1, 0, buffer, 0, 0, blockSize);
            processor.processBlock(buffer, midi);
        }

        std::string detectedScale = processor.getDetectedScaleName().toStdString();
        std::string camelot = processor.getCamelotCode().toStdString();
        std::string relative = processor.getRelativeKeyName().toStdString();

        std::cout << "Detected Scale: " << detectedScale << " (Expected: " << expectedScale << ")" << std::endl;
        std::cout << "Camelot Code:   " << camelot << " (Expected: " << expectedCamelot << ")" << std::endl;
        std::cout << "Relative Key:   " << relative << std::endl;

        bool passed = (detectedScale == expectedScale && camelot == expectedCamelot);
        std::cout << "Result: " << (passed ? "PASS" : "FAIL") << std::endl;
        if (!passed) allPassed = false;
    };

    auto testBpm = [&](const std::string& testName, double targetBpm)
    {
        std::cout << "\n[" << testName << "] Feeding " << targetBpm << " BPM Pattern..." << std::endl;
        processor.resetScale();
        processor.unlockBpm();

        double secondsPerBeat = 60.0 / targetBpm;
        double samplesPerBeat = secondsPerBeat * sampleRate;
        double sampleIndex = 0.0;

        int totalBlocks = static_cast<int>((5.0 * sampleRate) / blockSize);
        for (int b = 0; b < totalBlocks; ++b)
        {
            float* channelData = buffer.getWritePointer(0);
            for (int i = 0; i < blockSize; ++i)
            {
                double posInBeat = std::fmod(sampleIndex, samplesPerBeat);
                float s = 0.0f;
                // Kick thump pulse
                if (posInBeat < 0.035 * sampleRate)
                {
                    double t = posInBeat / sampleRate;
                    double freq = 130.0 * std::exp(-t * 35.0) + 50.0;
                    s = static_cast<float>(std::sin(2.0 * juce::double_Pi * freq * t) * (1.0 - t / 0.035));
                }
                channelData[i] = s;
                sampleIndex += 1.0;
            }
            buffer.copyFrom(1, 0, buffer, 0, 0, blockSize);
            processor.processBlock(buffer, midi);
        }

        float detectedBpm = processor.getDetectedAudioBpm();
        std::cout << "Detected BPM: " << detectedBpm << " (Expected: " << targetBpm << ")" << std::endl;
        bool passed = (std::abs(detectedBpm - static_cast<float>(targetBpm)) <= 1.0f);
        std::cout << "Result: " << (passed ? "PASS" : "FAIL") << std::endl;
        if (!passed) allPassed = false;
    };

    auto testGrooveBpm = [&](const std::string& testName, double targetBpm)
    {
        std::cout << "\n[" << testName << "] Feeding Full Groove (Kick+Snare+HiHats) at " << targetBpm << " BPM..." << std::endl;
        processor.resetScale();
        processor.unlockBpm();

        double secondsPerBeat = 60.0 / targetBpm;
        double samplesPerBeat = secondsPerBeat * sampleRate;
        double sampleIndex = 0.0;

        int totalBlocks = static_cast<int>((6.0 * sampleRate) / blockSize);
        for (int b = 0; b < totalBlocks; ++b)
        {
            float* channelData = buffer.getWritePointer(0);
            for (int i = 0; i < blockSize; ++i)
            {
                double beatPos = sampleIndex / samplesPerBeat;
                double beatInBar = std::fmod(beatPos, 4.0); // 4/4 bar
                double posInQuarter = std::fmod(sampleIndex, samplesPerBeat);
                double posInEighth = std::fmod(sampleIndex, samplesPerBeat * 0.5);

                float s = 0.0f;

                // Kick on beat 1 and 3
                if ((beatInBar < 0.25 || (beatInBar >= 2.0 && beatInBar < 2.25)) && posInQuarter < 0.04 * sampleRate)
                {
                    double t = posInQuarter / sampleRate;
                    double freq = 140.0 * std::exp(-t * 35.0) + 48.0;
                    s += static_cast<float>(0.8 * std::sin(2.0 * juce::double_Pi * freq * t) * (1.0 - t / 0.04));
                }

                // Snare on beat 2 and 4 (250-2500 Hz noise/tone)
                if (((beatInBar >= 1.0 && beatInBar < 1.25) || (beatInBar >= 3.0 && beatInBar < 3.25)) && posInQuarter < 0.06 * sampleRate)
                {
                    double t = posInQuarter / sampleRate;
                    float noise = (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f);
                    s += static_cast<float>(0.5 * (std::sin(2.0 * juce::double_Pi * 220.0 * t) + noise) * (1.0 - t / 0.06));
                }

                // Hi-Hat on every 8th note (> 3000 Hz)
                if (posInEighth < 0.015 * sampleRate)
                {
                    double t = posInEighth / sampleRate;
                    float hatNoise = (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f);
                    s += static_cast<float>(0.25 * hatNoise * (1.0 - t / 0.015));
                }

                channelData[i] = s;
                sampleIndex += 1.0;
            }
            buffer.copyFrom(1, 0, buffer, 0, 0, blockSize);
            processor.processBlock(buffer, midi);
        }

        float detectedBpm = processor.getDetectedAudioBpm();
        std::cout << "Detected BPM: " << detectedBpm << " (Expected: " << targetBpm << ")" << std::endl;
        bool passed = (std::abs(detectedBpm - static_cast<float>(targetBpm)) <= 1.2f);
        std::cout << "Result: " << (passed ? "PASS" : "FAIL") << std::endl;
        if (!passed) allPassed = false;
    };

    // Run tests
    testKey("TEST 1: C Major", 261.63, 329.63, 392.00, "C Major", "8B");
    testKey("TEST 2: D Minor", 293.66, 349.23, 440.00, "D Minor", "7A");
    testKey("TEST 3: F# Major", 369.99, 466.16, 554.37, "F# Major", "2B");

    testBpm("TEST 4: 128 BPM (House / EDM)", 128.0);
    testBpm("TEST 5: 140 BPM (Dubstep / Techno)", 140.0);
    testBpm("TEST 6: 90 BPM (Hip-Hop / Urban)", 90.0);
    testGrooveBpm("TEST 7: 120 BPM Full Groove (Kick+Snare+HiHats)", 120.0);
    testGrooveBpm("TEST 8: 174 BPM DnB Groove (Kick+Snare+HiHats)", 174.0);

    std::cout << "\n========================================" << std::endl;
    if (allPassed)
    {
        std::cout << "OVERALL: ALL 8 TESTS PASSED WITH 100% ACCURACY!" << std::endl;
        return 0;
    }
    else
    {
        std::cout << "OVERALL: SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
