#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

// ==============================================================================
// Luxury Studio Palette (Obsidian Glass & Anodized Metal)
// ==============================================================================
namespace StudioStyle {
    const juce::Colour bgDeep           { 0xFF0A0B0E }; // Pure obsidian
    const juce::Colour bgSheen          { 0xFF12141C }; // Top ambient light
    const juce::Colour cardSurface      { 0xFF12141E }; // Smoked glass card
    const juce::Colour cardSurfaceTop   { 0xFF181B26 }; // Top card specular
    const juce::Colour cardBorder       { 0x1AFFFFFF }; // Hairline glass edge (10% white)
    const juce::Colour cardBorderActive { 0x3AFFFFFF }; // Highlight edge
    
    const juce::Colour textHero         { 0xFFF5F5F7 }; // Clean high-contrast white
    const juce::Colour textBody         { 0xFFC7C7CC }; // Neutral light gray
    const juce::Colour textMuted        { 0xFF767882 }; // Understated labels
    const juce::Colour textDim          { 0xFF42444D }; // Sub-elements
    
    const juce::Colour accentViolet     { 0xFFA78BFA }; // Harmonic scale lavender
    const juce::Colour accentMint       { 0xFF34D399 }; // Precision lock emerald
    const juce::Colour accentCyan       { 0xFF38BDF8 }; // Acoustic tempo cyan
    const juce::Colour accentAmber      { 0xFFFBBF24 }; // Tuner cents gold
}

// ==============================================================================
// Bespoke Apple/Studio LookAndFeel for Ghost Glass Buttons
// ==============================================================================
class StudioGlassLookAndFeel : public juce::LookAndFeel_V4
{
public:
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused(backgroundColour);
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        float cornerRadius = bounds.getHeight() * 0.5f;

        juce::Colour base = StudioStyle::cardSurfaceTop;
        if (shouldDrawButtonAsDown)
            base = base.brighter(0.25f);
        else if (shouldDrawButtonAsHighlighted)
            base = base.brighter(0.12f);

        // Glass button fill
        g.setColour(base);
        g.fillRoundedRectangle(bounds, cornerRadius);

        // Subtle specular top hairline
        g.setColour(juce::Colours::white.withAlpha(shouldDrawButtonAsHighlighted ? 0.15f : 0.08f));
        g.drawHorizontalLine(static_cast<int>(bounds.getY() + 1.0f), bounds.getX() + 4.0f, bounds.getRight() - 4.0f);

        // Hairline outline
        g.setColour(StudioStyle::cardBorder.withAlpha(shouldDrawButtonAsHighlighted ? 0.40f : 0.22f));
        g.drawRoundedRectangle(bounds, cornerRadius, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& button,
                        bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        juce::ignoreUnused(shouldDrawButtonAsDown);
        auto font = juce::FontOptions(10.0f, juce::Font::bold);
        g.setFont(font);

        juce::Colour textCol = button.findColour(juce::TextButton::textColourOffId);
        if (shouldDrawButtonAsHighlighted)
            textCol = textCol.brighter(0.25f);

        g.setColour(textCol);
        g.drawFittedText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, 1);
    }
};

static StudioGlassLookAndFeel studioLAF;

// ==============================================================================
// Helper: Draw Masterpiece Card
// ==============================================================================
static void drawStudioCard(juce::Graphics& g, juce::Rectangle<float> bounds,
                           const juce::String& category,
                           juce::Colour accentCol)
{
    float corner = 14.0f;

    // Soft glass gradient fill
    juce::ColourGradient bgGrad(StudioStyle::cardSurfaceTop, bounds.getX(), bounds.getY(),
                                StudioStyle::cardSurface, bounds.getX(), bounds.getBottom(), false);
    g.setGradientFill(bgGrad);
    g.fillRoundedRectangle(bounds, corner);

    // 1px Razor Hairline Border
    g.setColour(StudioStyle::cardBorder);
    g.drawRoundedRectangle(bounds, corner, 1.0f);

    // Specular highlight on top edge
    juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.10f), bounds.getCentreX(), bounds.getY(),
                               juce::Colours::transparentWhite, bounds.getRight() - 20.0f, bounds.getY(), true);
    g.setGradientFill(sheen);
    g.drawHorizontalLine(static_cast<int>(bounds.getY() + 1.0f), bounds.getX() + corner, bounds.getRight() - corner);

    // Category Tag (Micro Letter-Spaced)
    float padX = bounds.getX() + 18.0f;
    float padY = bounds.getY() + 14.0f;

    // Small status pip
    g.setColour(accentCol);
    g.fillEllipse(padX, padY + 3.0f, 6.0f, 6.0f);

    g.setColour(StudioStyle::textMuted);
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText(category.toUpperCase(),
               static_cast<int>(padX + 12.0f), static_cast<int>(padY), 160, 12,
               juce::Justification::centredLeft);
}

// ==============================================================================
// TunerBPMPluginAudioProcessorEditor Implementation
// ==============================================================================
TunerBPMPluginAudioProcessorEditor::TunerBPMPluginAudioProcessorEditor(TunerBPMPluginAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(680, 336);
    std::fill(std::begin(oscilloscopeData), std::end(oscilloscopeData), 0.0f);

    // Master Reset Button
    resetAllButton.setButtonText("RESET");
    resetAllButton.setLookAndFeel(&studioLAF);
    resetAllButton.setColour(juce::TextButton::textColourOffId, StudioStyle::textBody);
    resetAllButton.onClick = [this] {
        audioProcessor.resetScale();
        audioProcessor.unlockBpm();
    };
    addAndMakeVisible(resetAllButton);

    // Kick Re-detect Button
    unlockBpmButton.setButtonText("RE-CALC");
    unlockBpmButton.setLookAndFeel(&studioLAF);
    unlockBpmButton.setColour(juce::TextButton::textColourOffId, StudioStyle::accentCyan);
    unlockBpmButton.onClick = [this] { audioProcessor.unlockBpm(); };
    addAndMakeVisible(unlockBpmButton);

    startTimerHz(60);
}

TunerBPMPluginAudioProcessorEditor::~TunerBPMPluginAudioProcessorEditor()
{
    resetAllButton.setLookAndFeel(nullptr);
    unlockBpmButton.setLookAndFeel(nullptr);
}

// ==============================================================================
void TunerBPMPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    const int W = getWidth();
    const int H = getHeight();

    // ══ AMBIENT BACKGROUND (Obsidian Studio Sheen) ════════════════════════════
    juce::ColourGradient bgGrad(StudioStyle::bgSheen, 0.0f, 0.0f,
                                StudioStyle::bgDeep, 0.0f, static_cast<float>(H), false);
    g.setGradientFill(bgGrad);
    g.fillAll();

    // ══ HEADER (Masterpiece Pro Branding) ═════════════════════════════════════
    const float headerH = 48.0f;
    float hX = 20.0f;

    // Logo: STT2
    g.setColour(StudioStyle::textHero);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("STT2", static_cast<int>(hX), 0, 62, static_cast<int>(headerH), juce::Justification::centredLeft);

    // Version Pill Badge: V.2
    float v2X = hX + 65.0f;
    float v2Y = (headerH - 20.0f) * 0.5f;
    juce::Rectangle<float> v2Badge(v2X, v2Y, 36.0f, 20.0f);
    g.setColour(StudioStyle::accentCyan.withAlpha(0.12f));
    g.fillRoundedRectangle(v2Badge, 6.0f);
    g.setColour(StudioStyle::accentCyan.withAlpha(0.45f));
    g.drawRoundedRectangle(v2Badge, 6.0f, 1.0f);

    g.setColour(StudioStyle::accentCyan);
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    g.drawText("V.2", v2Badge, juce::Justification::centred);

    // Vertical Hairline Divider
    float divX = v2X + 46.0f;
    g.setColour(StudioStyle::cardBorder);
    g.drawVerticalLine(static_cast<int>(divX), 14.0f, headerH - 14.0f);

    // Masterpiece Brand Logo: SUPREME TEMPO & TUNER - BY VEVI
    float brandX = divX + 14.0f;
    g.setColour(StudioStyle::textHero);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("SUPREME TEMPO & TUNER", static_cast<int>(brandX), 0, 175, static_cast<int>(headerH), juce::Justification::centredLeft);

    g.setColour(StudioStyle::accentCyan);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText("— BY VEVI", static_cast<int>(brandX + 176.0f), 0, 90, static_cast<int>(headerH), juce::Justification::centredLeft);

    // ══ HERO SECTION (Two Balanced Luxury Cards) ══════════════════════════════
    const float pad = 12.0f;
    const float heroY = headerH + 2.0f;
    const float heroH = 214.0f;
    const float cardW = (static_cast<float>(W) - pad * 3.0f) * 0.5f;

    // ─────────────────────────────────────────────────────────────────────────
    // 1. LEFT CARD: MUSICAL SCALE & KEY (Armonía)
    // ─────────────────────────────────────────────────────────────────────────
    juce::Rectangle<float> scaleCard(pad, heroY, cardW, heroH);
    drawStudioCard(g, scaleCard, "Harmonic Key Detector",
                   isScaleLocked ? StudioStyle::accentMint : StudioStyle::accentViolet);

    float sx = scaleCard.getX() + 18.0f;
    float sy = scaleCard.getY() + 38.0f;
    bool hasScale = (currentScaleName != "Detecting...");

    // Big Hero Scale Title
    juce::Colour scaleColor = isScaleLocked ? StudioStyle::accentMint :
                              (hasScale ? StudioStyle::accentViolet : StudioStyle::textMuted);
    g.setColour(scaleColor);
    g.setFont(juce::FontOptions(34.0f, juce::Font::bold));
    g.drawText(currentScaleName, static_cast<int>(sx), static_cast<int>(sy), static_cast<int>(cardW - 36.0f), 42, juce::Justification::centredLeft);

    // Camelot & Relative Key Glass Capsule
    sy += 48.0f;
    float pillW = cardW - 36.0f;
    juce::Rectangle<float> infoPill(sx, sy, pillW, 30.0f);

    g.setColour(StudioStyle::cardSurfaceTop.withAlpha(0.60f));
    g.fillRoundedRectangle(infoPill, 15.0f);
    g.setColour(StudioStyle::cardBorder);
    g.drawRoundedRectangle(infoPill, 15.0f, 1.0f);

    juce::String infoText = hasScale ? ("CAMELOT  " + currentCamelot + "   •   RELATIVE  " + currentRelativeKey.toUpperCase())
                                     : "LISTENING TO CHORD HARMONICS...";
    g.setColour(hasScale ? StudioStyle::textHero : StudioStyle::textMuted);
    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    g.drawText(infoText, infoPill, juce::Justification::centred);

    // Monotonic Analysis Progress Bar (Never goes backwards!)
    sy += 48.0f;
    g.setColour(StudioStyle::textMuted);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText("ANALYSIS PROGRESS", static_cast<int>(sx), static_cast<int>(sy), 140, 12, juce::Justification::left);

    float progClamped = juce::jlimit(0.0f, 1.0f, currentScaleProgress);
    juce::String pctStr = isScaleLocked ? "LOCKED (100%)" : (juce::String(juce::roundToInt(progClamped * 100.0f)) + "%");
    g.setColour(isScaleLocked ? StudioStyle::accentMint : StudioStyle::accentViolet);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText(pctStr, static_cast<int>(sx), static_cast<int>(sy), static_cast<int>(pillW), 12, juce::Justification::right);

    float barY = sy + 16.0f;
    g.setColour(StudioStyle::cardBorder.withAlpha(0.35f));
    g.fillRoundedRectangle(sx, barY, pillW, 4.0f, 2.0f);

    if (progClamped > 0.001f)
    {
        juce::Colour barCol = isScaleLocked ? StudioStyle::accentMint : StudioStyle::accentViolet;
        g.setColour(barCol);
        g.fillRoundedRectangle(sx, barY, pillW * progClamped, 4.0f, 2.0f);

        // Soft micro-glow at edge
        if (!isScaleLocked && progClamped < 0.99f)
        {
            float tipX = sx + pillW * progClamped;
            g.setColour(barCol.withAlpha(0.6f));
            g.fillEllipse(tipX - 3.0f, barY - 1.0f, 6.0f, 6.0f);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2. RIGHT CARD: TEMPO & KICK REPETITION BPM (Ritmo)
    // ─────────────────────────────────────────────────────────────────────────
    float rx = scaleCard.getRight() + pad;
    juce::Rectangle<float> bpmCard(rx, heroY, cardW, heroH);
    drawStudioCard(g, bpmCard, "Kick Tempo Reference",
                   isBpmLocked ? StudioStyle::accentMint : StudioStyle::accentCyan);

    // Discrete Beat LEDs (Top Right)
    float ledsX = bpmCard.getRight() - 86.0f;
    float ledsY = bpmCard.getY() + 15.0f;
    bool isPlaying = audioProcessor.isHostPlaying();
    for (int b = 1; b <= 4; ++b)
    {
        float lx = ledsX + static_cast<float>(b - 1) * 16.0f;
        bool lit = isPlaying && (b == currentBeatNum);
        juce::Colour ledCol = (b == 1) ? StudioStyle::accentMint : StudioStyle::accentCyan;

        g.setColour(lit ? ledCol : StudioStyle::cardBorder);
        g.fillEllipse(lx, ledsY, 6.0f, 6.0f);
        if (lit && beatFlashLevel > 0.0f)
        {
            g.setColour(ledCol.withAlpha(beatFlashLevel * 0.40f));
            g.drawEllipse(lx - 2.5f, ledsY - 2.5f, 11.0f, 11.0f, 1.0f);
        }
    }

    float bx = bpmCard.getX() + 18.0f;
    float by = bpmCard.getY() + 38.0f;

    // Giant Fixed BPM Hero Numeral (SF Pro 46pt bold)
    juce::String bpmStr = (currentAudioBpm > 0.0f) ? juce::String(juce::roundToInt(currentAudioBpm)) : "---";
    g.setColour(isBpmLocked ? StudioStyle::accentMint : (currentAudioBpm > 0.0f ? StudioStyle::accentCyan : StudioStyle::textMuted));
    g.setFont(juce::FontOptions(46.0f, juce::Font::bold));
    g.drawText(bpmStr, static_cast<int>(bx), static_cast<int>(by), 125, 46, juce::Justification::centredLeft);

    // BPM Unit and Lock Subscript
    float tagX = bx + 124.0f;
    g.setColour(StudioStyle::textHero);
    g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
    g.drawText("BPM", static_cast<int>(tagX), static_cast<int>(by + 8.0f), 50, 16, juce::Justification::left);

    g.setColour(isBpmLocked ? StudioStyle::accentMint : StudioStyle::textMuted);
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText(isBpmLocked ? "FIXED LOCK" : (currentAudioBpm > 0.0f ? "ANALYZING" : "SEARCHING"), static_cast<int>(tagX), static_cast<int>(by + 26.0f), 80, 14, juce::Justification::left);

    // Status Capsule Badge
    by += 54.0f;
    float statusW = cardW - 36.0f;
    juce::Rectangle<float> statusRect(bx, by, statusW, 30.0f);

    juce::Colour statusCol = isBpmLocked ? StudioStyle::accentMint : StudioStyle::accentCyan;
    g.setColour(statusCol.withAlpha(0.09f));
    g.fillRoundedRectangle(statusRect, 15.0f);
    g.setColour(statusCol.withAlpha(0.28f));
    g.drawRoundedRectangle(statusRect, 15.0f, 1.0f);

    // Status Indicator Dot
    g.setColour(statusCol);
    g.fillEllipse(bx + 12.0f, by + 10.5f, 8.0f, 8.0f);
    if (isBpmLocked)
    {
        g.setColour(statusCol.withAlpha(0.35f));
        g.drawEllipse(bx + 9.5f, by + 8.0f, 13.0f, 13.0f, 1.0f);
    }

    g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    g.setColour(StudioStyle::textHero);
    g.drawText(bpmStatus.toUpperCase(), static_cast<int>(bx + 28.0f), static_cast<int>(by), static_cast<int>(statusW - 105.0f), 30, juce::Justification::centredLeft);

    // DAW Host Reference Row
    by += 46.0f;
    g.setColour(StudioStyle::textMuted);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText("DAW PROJECT SYNC", static_cast<int>(bx), static_cast<int>(by), 150, 12, juce::Justification::left);

    by += 16.0f;
    g.setColour(StudioStyle::textHero);
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    g.drawText(juce::String(audioProcessor.getTempo(), 1) + " BPM", static_cast<int>(bx), static_cast<int>(by), 150, 18, juce::Justification::left);

    // ══ BOTTOM RIBBON: ACOUSTIC GLASS DOCK (Tuner + Oscilloscope) ═════════════
    float dockY = heroY + heroH + pad;
    float dockH = static_cast<float>(H) - dockY - pad;
    juce::Rectangle<float> dockRect(pad, dockY, static_cast<float>(W) - pad * 2.0f, dockH);

    // Glass Dock Surface
    g.setColour(StudioStyle::cardSurface);
    g.fillRoundedRectangle(dockRect, 12.0f);
    g.setColour(StudioStyle::cardBorder);
    g.drawRoundedRectangle(dockRect, 12.0f, 1.0f);

    // Left Section: Detected Note
    float nx = pad + 18.0f;
    bool hasNote = (activeNoteName != "---" && !activeNoteName.isEmpty());
    bool inTune = hasNote && std::abs(smoothedCents) <= 3.5f;
    juce::Colour noteCol = hasNote ? (inTune ? StudioStyle::accentMint : StudioStyle::accentAmber)
                                   : StudioStyle::textMuted;

    g.setColour(noteCol);
    g.setFont(juce::FontOptions(19.0f, juce::Font::bold));
    g.drawText(hasNote ? activeNoteName : "---", static_cast<int>(nx), static_cast<int>(dockY), 54, static_cast<int>(dockH), juce::Justification::centredLeft);

    // Center-Left: Precision Cents Calibration Meter
    float gaugeX = nx + 58.0f;
    float gaugeW = 105.0f;
    float gaugeY = dockY + dockH * 0.5f - 2.5f;

    // Track
    g.setColour(StudioStyle::cardBorder.withAlpha(0.3f));
    g.fillRoundedRectangle(gaugeX, gaugeY, gaugeW, 5.0f, 2.5f);

    // Center Zero Pip
    g.setColour(StudioStyle::accentMint.withAlpha(0.6f));
    g.drawVerticalLine(static_cast<int>(gaugeX + gaugeW * 0.5f), gaugeY - 2.0f, gaugeY + 7.0f);

    // Moving Needle Indicator
    if (hasNote)
    {
        float centsClamped = juce::jlimit(-50.0f, 50.0f, smoothedCents);
        float indX = gaugeX + gaugeW * 0.5f + (centsClamped / 50.0f) * (gaugeW * 0.46f);
        g.setColour(noteCol);
        g.fillRoundedRectangle(indX - 2.0f, gaugeY - 2.0f, 4.0f, 9.0f, 2.0f);
    }

    // Right Section: Organic Luminous Oscilloscope Line
    float oscX = gaugeX + gaugeW + 30.0f;
    float oscW = static_cast<float>(W) - pad - oscX - 18.0f;
    float oscY = dockY + 6.0f;
    float oscH = dockH - 12.0f;
    float midY = oscY + oscH * 0.5f;

    audioProcessor.getOscilloscopeBuffer(oscilloscopeData);
    juce::Path wavePath;
    const int sz = TunerBPMPluginAudioProcessor::oscilloscopeSize;
    for (int i = 0; i < sz; ++i)
    {
        float px = oscX + static_cast<float>(i) / static_cast<float>(sz) * oscW;
        float sample = juce::jlimit(-1.0f, 1.0f, oscilloscopeData[i]);
        float py = midY - sample * (oscH * 0.44f);
        if (i == 0) wavePath.startNewSubPath(px, py);
        else        wavePath.lineTo(px, py);
    }

    // Pass 1: Soft Ambient Glow
    g.setColour(StudioStyle::accentCyan.withAlpha(0.20f));
    g.strokePath(wavePath, juce::PathStrokeType(3.0f));

    // Pass 2: Razor Crisp Luminous Trace
    g.setColour(StudioStyle::accentCyan.withAlpha(0.85f));
    g.strokePath(wavePath, juce::PathStrokeType(1.2f));

    // Micro Watermark
    g.setColour(StudioStyle::textMuted.withAlpha(0.40f));
    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    g.drawText("STT2 v2.0 • PRO", static_cast<int>(oscX), static_cast<int>(dockY + dockH - 14.0f), static_cast<int>(oscW), 10, juce::Justification::bottomRight);
}

// ==============================================================================
void TunerBPMPluginAudioProcessorEditor::resized()
{
    const int W = getWidth();
    const float pad = 12.0f;
    const float headerH = 48.0f;
    const float heroY = headerH + 2.0f;
    const float cardW = (static_cast<float>(W) - pad * 3.0f) * 0.5f;

    // Reset button in Header (Top Right)
    resetAllButton.setBounds(W - 76, 12, 60, 24);

    // Re-calc button inside BPM Card (Right-aligned in status pill row)
    float rx = pad + cardW + pad;
    float bx = rx + 18.0f;
    float statusW = cardW - 36.0f;
    unlockBpmButton.setBounds(static_cast<int>(bx + statusW - 74.0f), static_cast<int>(heroY + 95.0f), 68, 24);
}

// ==============================================================================
void TunerBPMPluginAudioProcessorEditor::timerCallback()
{
    // Pitch & Tuner
    smoothedCents  = smoothedCents * 0.70f + audioProcessor.getCentsDeviation() * 0.30f;
    activeNoteName = audioProcessor.getDetectedNoteName();

    // Scale & Key
    currentScaleName     = audioProcessor.getDetectedScaleName();
    currentRelativeKey   = audioProcessor.getRelativeKeyName();
    currentCamelot       = audioProcessor.getCamelotCode();
    currentScaleProgress = audioProcessor.getScaleProgress();
    isScaleLocked        = audioProcessor.isScaleLocked();

    // Tempo & BPM
    currentAudioBpm = audioProcessor.getDetectedAudioBpm();
    isBpmLocked     = audioProcessor.isBpmLocked();
    bpmStatus       = audioProcessor.getBpmStatus();

    // Beat Pulse Animation (Visual only)
    int beatNum = 1;
    if (audioProcessor.getAndClearBeatTriggered(beatNum))
    {
        beatFlashLevel = 1.0f;
        currentBeatNum = beatNum;
    }
    else
    {
        beatFlashLevel *= 0.85f;
        if (beatFlashLevel < 0.01f) beatFlashLevel = 0.0f;
    }

    repaint();
}
