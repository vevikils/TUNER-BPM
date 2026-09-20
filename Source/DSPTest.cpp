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

    // ==============================================================================
    // MULTI-FILE LOADING & FL STUDIO CONCURRENCY TEST SUITE
    // ==============================================================================
    auto generateTestWav = [&](const juce::File& file, double durationSec, double targetBpm, double fRoot)
    {
        if (file.existsAsFile())
            file.deleteFile();

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::AudioFormatWriter> writer(
            wavFormat.createWriterFor(new juce::FileOutputStream(file), 44100.0, 1, 16, {}, 0));

        if (writer == nullptr)
            return false;

        const int totalSamples = static_cast<int>(durationSec * 44100.0);
        const int writeBlock = 2048;
        juce::AudioBuffer<float> tempBuf(1, writeBlock);

        double secondsPerBeat = 60.0 / targetBpm;
        double samplesPerBeat = secondsPerBeat * 44100.0;
        double sampleIndex = 0.0;

        double p1 = 0.0, p2 = 0.0, p3 = 0.0;
        double fThird = fRoot * 1.25992;
        double fFifth = fRoot * 1.49831;

        int samplesWritten = 0;
        while (samplesWritten < totalSamples)
        {
            int toWrite = std::min(writeBlock, totalSamples - samplesWritten);
            float* writePtr = tempBuf.getWritePointer(0);

            for (int i = 0; i < toWrite; ++i)
            {
                float s = 0.25f * static_cast<float>(std::sin(p1) + std::sin(p2) + std::sin(p3));
                p1 += 2.0 * juce::double_Pi * fRoot / 44100.0;
                p2 += 2.0 * juce::double_Pi * fThird / 44100.0;
                p3 += 2.0 * juce::double_Pi * fFifth / 44100.0;

                double posInBeat = std::fmod(sampleIndex, samplesPerBeat);
                if (posInBeat < 0.04 * 44100.0)
                {
                    double t = posInBeat / 44100.0;
                    double kFreq = 130.0 * std::exp(-t * 35.0) + 50.0;
                    s += static_cast<float>(0.75 * std::sin(2.0 * juce::double_Pi * kFreq * t) * (1.0 - t / 0.04));
                }

                writePtr[i] = juce::jlimit(-1.0f, 1.0f, s);
                sampleIndex += 1.0;
            }

            writer->writeFromAudioSampleBuffer(tempBuf, 0, toWrite);
            samplesWritten += toWrite;
        }

        return true;
    };

    juce::File tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    juce::File file1 = tempDir.getChildFile("tuner_bpm_test_1.wav");
    juce::File file2 = tempDir.getChildFile("tuner_bpm_test_2.wav");
    juce::File file3 = tempDir.getChildFile("tuner_bpm_test_3.wav");

    generateTestWav(file1, 15.0, 128.0, 261.63); // C Major, 128 BPM
    generateTestWav(file2, 15.0, 140.0, 293.66); // D Minor, 140 BPM
    generateTestWav(file3, 15.0, 90.0, 440.0);   // A Major, 90 BPM

    // TEST 9: Sequential Multi-File Loading (File 1 -> File 2)
    std::cout << "\n[TEST 9: Multi-File Sequential Loading (File 1 -> File 2)]" << std::endl;
    processor.loadAndAnalyzeAudioFile(file1);
    int waitCounter = 0;
    while (processor.isAnalyzingFile() && waitCounter < 200)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        waitCounter++;
    }
    bool f1Loaded = processor.hasLoadedAudioFile();
    float bpm1 = processor.getDetectedAudioBpm();
    std::cout << "File 1 analyzed. hasLoadedAudioFile: " << (f1Loaded ? "YES" : "NO")
              << ", BPM: " << bpm1 << ", Scale: " << processor.getDetectedScaleName() << std::endl;

    std::cout << "Loading SECOND File (140 BPM, D) - Testing Seamless Replacement..." << std::endl;
    processor.loadAndAnalyzeAudioFile(file2);
    waitCounter = 0;
    while (processor.isAnalyzingFile() && waitCounter < 200)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        waitCounter++;
    }
    bool f2Loaded = processor.hasLoadedAudioFile();
    float bpm2 = processor.getDetectedAudioBpm();
    std::cout << "File 2 analyzed. hasLoadedAudioFile: " << (f2Loaded ? "YES" : "NO")
              << ", BPM: " << bpm2 << ", Scale: " << processor.getDetectedScaleName() << std::endl;

    bool passed9 = f1Loaded && f2Loaded && (std::abs(bpm2 - 140.0f) <= 2.0f);
    std::cout << "Result: " << (passed9 ? "PASS" : "FAIL") << std::endl;
    if (!passed9) allPassed = false;

    // TEST 10: Rapid Consecutive Multi-File Loading (Mid-Flight Cancellation)
    std::cout << "\n[TEST 10: Rapid Multi-File Cancellation (File 1 -> File 2 -> File 3 Rapid Load)]" << std::endl;
    processor.loadAndAnalyzeAudioFile(file1);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    processor.loadAndAnalyzeAudioFile(file2);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    processor.loadAndAnalyzeAudioFile(file3);

    waitCounter = 0;
    while (processor.isAnalyzingFile() && waitCounter < 200)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        waitCounter++;
    }

    float bpm3 = processor.getDetectedAudioBpm();
    std::cout << "Final File 3 BPM: " << bpm3 << " (Expected: 90.0)" << std::endl;
    bool passed10 = processor.hasLoadedAudioFile() && (std::abs(bpm3 - 90.0f) <= 2.0f);
    std::cout << "Result: " << (passed10 ? "PASS" : "FAIL") << std::endl;
    if (!passed10) allPassed = false;

    // TEST 11: Real-Time Audio Callback Concurrency (FL Studio Engine Simulation)
    std::cout << "\n[TEST 11: Concurrent Real-Time Audio Callback (FL Studio Engine Simulation)]" << std::endl;
    std::atomic<bool> audioThreadActive { true };
    std::atomic<uint64_t> audioBlocksProcessed { 0 };

    std::thread audioSimThread([&]()
    {
        juce::AudioBuffer<float> simBuf(2, blockSize);
        juce::MidiBuffer simMidi;
        double phase = 0.0;

        while (audioThreadActive.load(std::memory_order_relaxed))
        {
            simBuf.clear();
            float* left = simBuf.getWritePointer(0);
            float* right = simBuf.getWritePointer(1);
            for (int i = 0; i < blockSize; ++i)
            {
                float s = static_cast<float>(0.1 * std::sin(phase));
                phase += 2.0 * juce::double_Pi * 440.0 / sampleRate;
                left[i] = s;
                right[i] = s;
            }

            processor.processBlock(simBuf, simMidi);
            audioBlocksProcessed.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
    });

    std::cout << "DAW audio callback running concurrently. Loading File 1..." << std::endl;
    processor.loadAndAnalyzeAudioFile(file1);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::cout << "Loading File 2 while audio callback running..." << std::endl;
    processor.loadAndAnalyzeAudioFile(file2);
    waitCounter = 0;
    while (processor.isAnalyzingFile() && waitCounter < 200)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        waitCounter++;
    }

    std::cout << "Testing Eject / Clear while audio callback running..." << std::endl;
    processor.clearLoadedAudioFile();
    std::cout << "hasLoadedAudioFile after eject: " << (processor.hasLoadedAudioFile() ? "YES" : "NO") << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Reloading File 3 while audio callback running..." << std::endl;
    processor.loadAndAnalyzeAudioFile(file3);
    waitCounter = 0;
    while (processor.isAnalyzingFile() && waitCounter < 200)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        waitCounter++;
    }

    audioThreadActive.store(false);
    if (audioSimThread.joinable())
        audioSimThread.join();

    std::cout << "Audio blocks processed concurrently: " << audioBlocksProcessed.load() << std::endl;
    bool passed11 = (audioBlocksProcessed.load() > 50) && processor.hasLoadedAudioFile();
    std::cout << "Result: " << (passed11 ? "PASS" : "FAIL") << std::endl;
    if (!passed11) allPassed = false;

    // TEST 12: Corrupt & Unsupported File Resilience
    std::cout << "\n[TEST 12: Corrupt, 0-Byte & Unsupported Audio File Resilience]" << std::endl;
    juce::File emptyFile = tempDir.getChildFile("tuner_empty.wav");
    emptyFile.deleteFile();
    emptyFile.create();

    juce::File corruptFile = tempDir.getChildFile("tuner_corrupt.wav");
    corruptFile.deleteFile();
    corruptFile.appendData("RIFF....WAVEfmt ....not real wav audio data", 43);

    std::cout << "Loading 0-byte file (Must not crash)..." << std::endl;
    processor.loadAndAnalyzeAudioFile(emptyFile);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Loading corrupt file (Must not crash)..." << std::endl;
    processor.loadAndAnalyzeAudioFile(corruptFile);
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::cout << "Recovery test: Loading valid file immediately after corrupt files..." << std::endl;
    processor.loadAndAnalyzeAudioFile(file1);
    waitCounter = 0;
    while (processor.isAnalyzingFile() && waitCounter < 200)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        waitCounter++;
    }

    std::cout << "Recovered BPM: " << processor.getDetectedAudioBpm() << std::endl;
    bool passed12 = processor.hasLoadedAudioFile() && (std::abs(processor.getDetectedAudioBpm() - 128.0f) <= 2.0f);
    std::cout << "Result: " << (passed12 ? "PASS" : "FAIL") << std::endl;
    if (!passed12) allPassed = false;

    file1.deleteFile();
    file2.deleteFile();
    file3.deleteFile();
    emptyFile.deleteFile();
    corruptFile.deleteFile();

    std::cout << "\n========================================" << std::endl;
    if (allPassed)
    {
        std::cout << "OVERALL: ALL 12 TESTS (INCLUDING MULTI-FILE CONCURRENCY) PASSED WITH 100% ACCURACY!" << std::endl;
        return 0;
    }
    else
    {
        std::cout << "OVERALL: SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}