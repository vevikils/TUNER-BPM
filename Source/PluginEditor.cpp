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
    const juce::Colour accentRose       { 0xFFF43F5E }; // Eject button coral/rose
}

// ==============================================================================
// Camelot Color Palette (Authentic Harmonic Mixing Wheel)
// ==============================================================================
static juce::Colour getCamelotColor(const juce::String& code)
{
    int num = code.retainCharacters("0123456789").getIntValue();
    switch (num)
    {
        case 1:  return juce::Colour(0xFF00D2B4); // 1A/1B: Aqua
        case 2:  return juce::Colour(0xFF00B4FF); // 2A/2B: Sky Blue
        case 3:  return juce::Colour(0xFF387BFF); // 3A/3B: Deep Blue
        case 4:  return juce::Colour(0xFF7B5CFF); // 4A/4B: Indigo
        case 5:  return juce::Colour(0xFFA855F7); // 5A/5B: Magenta
        case 6:  return juce::Colour(0xFFEC4899); // 6A/6B: Rose Pink
        case 7:  return juce::Colour(0xFFEF4444); // 7A/7B: Coral Red
        case 8:  return juce::Colour(0xFFF97316); // 8A/8B: Orange-Red
        case 9:  return juce::Colour(0xFFF59E0B); // 9A/9B: Amber
        case 10: return juce::Colour(0xFFEAB308); // 10A/10B: Gold
        case 11: return juce::Colour(0xFF84CC16); // 11A/11B: Lime
        case 12: return juce::Colour(0xFF10B981); // 12A/12B: Emerald
        default: return StudioStyle::accentViolet;
    }
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
        g.setColour(StudioStyle::cardBorder.withAlpha(shouldDrawButtonAsHighlighted ? 0.45f : 0.25f));
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
    juce::ColourGradient sheen(juce::Colours::white.withAlpha(0.12f), bounds.getCentreX(), bounds.getY(),
                               juce::Colours::transparentWhite, bounds.getRight() - 20.0f, bounds.getY(), true);
    g.setGradientFill(sheen);
    g.drawHorizontalLine(static_cast<int>(bounds.getY() + 1.0f), bounds.getX() + corner, bounds.getRight() - corner);

    // Category Tag (Micro Letter-Spaced)
    float padX = bounds.getX() + 18.0f;
    float padY = bounds.getY() + 14.0f;

    // Small status pip with subtle aura
    g.setColour(accentCol.withAlpha(0.35f));
    g.fillEllipse(padX - 2.0f, padY + 1.0f, 10.0f, 10.0f);
    g.setColour(accentCol);
    g.fillEllipse(padX, padY + 3.0f, 6.0f, 6.0f);

    g.setColour(StudioStyle::textMuted);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText(category.toUpperCase(),
               static_cast<int>(padX + 14.0f), static_cast<int>(padY), 200, 12,
               juce::Justification::centredLeft);
}

// ==============================================================================
// TunerBPMPluginAudioProcessorEditor Implementation
// ==============================================================================
TunerBPMPluginAudioProcessorEditor::TunerBPMPluginAudioProcessorEditor(TunerBPMPluginAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(780, 400);
    std::fill(std::begin(oscilloscopeData), std::end(oscilloscopeData), 0.0f);

    // Open Audio File Button (Tunebat Web feature)
    openFileButton.setButtonText("OPEN FILE");
    openFileButton.setLookAndFeel(&studioLAF);
    openFileButton.setColour(juce::TextButton::textColourOffId, StudioStyle::accentCyan);
    openFileButton.onClick = [this] { triggerOpenFileDialog(); };
    addAndMakeVisible(openFileButton);

    // Eject Loaded File Button (Returns to Live DAW Monitoring)
    ejectFileButton.setButtonText("EJECT / LIVE");
    ejectFileButton.setLookAndFeel(&studioLAF);
    ejectFileButton.setColour(juce::TextButton::textColourOffId, StudioStyle::accentRose);
    ejectFileButton.onClick = [this] {
        audioProcessor.clearLoadedAudioFile();
        repaint();
    };
    addChildComponent(ejectFileButton); // Initially hidden if no file loaded

    // Master Reset Button
    resetAllButton.setButtonText("RESET");
    resetAllButton.setLookAndFeel(&studioLAF);
    resetAllButton.setColour(juce::TextButton::textColourOffId, StudioStyle::textBody);
    resetAllButton.onClick = [this] {
        audioProcessor.clearLoadedAudioFile();
        audioProcessor.resetScale();
        audioProcessor.unlockBpm();
        repaint();
    };
    addAndMakeVisible(resetAllButton);

    // BPM Re-detect Button
    unlockBpmButton.setButtonText("RE-CALC");
    unlockBpmButton.setLookAndFeel(&studioLAF);
    unlockBpmButton.setColour(juce::TextButton::textColourOffId, StudioStyle::accentCyan);
    unlockBpmButton.onClick = [this] { audioProcessor.unlockBpm(); };
    addAndMakeVisible(unlockBpmButton);

    startTimerHz(60);
}

TunerBPMPluginAudioProcessorEditor::~TunerBPMPluginAudioProcessorEditor()
{
    openFileButton.setLookAndFeel(nullptr);
    ejectFileButton.setLookAndFeel(nullptr);
    resetAllButton.setLookAndFeel(nullptr);
    unlockBpmButton.setLookAndFeel(nullptr);
}

// ==============================================================================
// File Drag-and-Drop Implementation (Tunebat Analyzer Style)
// ==============================================================================
bool TunerBPMPluginAudioProcessorEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (auto& file : files)
    {
        juce::String ext = juce::File(file).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".mp3" || ext == ".flac" || ext == ".aif" || ext == ".aiff" || ext == ".ogg" || ext == ".m4a")
            return true;
    }
    return false;
}

void TunerBPMPluginAudioProcessorEditor::filesDropped(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(x, y);
    isFileHovering = false;
    for (const auto& filePath : files)
    {
        juce::File audioFile(filePath);
        if (audioFile.existsAsFile())
        {
            auto ext = audioFile.getFileExtension().toLowerCase();
            if (ext == ".wav" || ext == ".mp3" || ext == ".flac" ||
                ext == ".aif" || ext == ".aiff" || ext == ".ogg" || ext == ".m4a")
            {
                audioProcessor.loadAndAnalyzeAudioFile(audioFile);
                break;
            }
        }
    }
    repaint();
}

void TunerBPMPluginAudioProcessorEditor::fileDragEnter(const juce::StringArray& files, int x, int y)
{
    juce::ignoreUnused(files, x, y);
    isFileHovering = true;
    repaint();
}

void TunerBPMPluginAudioProcessorEditor::fileDragExit(const juce::StringArray& files)
{
    juce::ignoreUnused(files);
    isFileHovering = false;
    repaint();
}

void TunerBPMPluginAudioProcessorEditor::triggerOpenFileDialog()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Select an Audio File to Analyze (WAV, MP3, FLAC, AIFF, OGG, M4A)...",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg;*.m4a");

    auto chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(chooserFlags, [this](const juce::FileChooser& fc)
    {
        auto file = fc.getResult();
        if (file.existsAsFile())
            audioProcessor.loadAndAnalyzeAudioFile(file);
    });
}

// ==============================================================================
void TunerBPMPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    const int W = getWidth();
    const int H = getHeight();

    // ══ AMBIENT BACKGROUND (Deep Obsidian Studio Sheen) ══════════════════════
    juce::ColourGradient bgGrad(StudioStyle::bgSheen, 0.0f, 0.0f,
                                StudioStyle::bgDeep, 0.0f, static_cast<float>(H), false);
    g.setGradientFill(bgGrad);
    g.fillAll();

    // ══ HEADER (Masterpiece Pro Branding) ═════════════════════════════════════
    const float headerH = 46.0f;
    float hX = 20.0f;

    // Logo: SUPREME TUNER BPM
    g.setColour(StudioStyle::textHero);
    g.setFont(juce::FontOptions(16.5f, juce::Font::bold));
    g.drawText("SUPREME TUNER BPM", static_cast<int>(hX), 0, 190, static_cast<int>(headerH), juce::Justification::centredLeft);

    // Version Pill Badge: V.2
    float v2X = hX + 195.0f;
    float v2Y = (headerH - 22.0f) * 0.5f;
    juce::Rectangle<float> v2Badge(v2X, v2Y, 52.0f, 22.0f);
    g.setColour(StudioStyle::accentCyan.withAlpha(0.15f));
    g.fillRoundedRectangle(v2Badge, 6.0f);
    g.setColour(StudioStyle::accentCyan.withAlpha(0.60f));
    g.drawRoundedRectangle(v2Badge, 6.0f, 1.0f);

    g.setColour(StudioStyle::accentCyan);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText("V.2.1", v2Badge, juce::Justification::centred);

    // Vertical Hairline Divider
    float divX = v2X + 62.0f;
    g.setColour(StudioStyle::cardBorder);
    g.drawVerticalLine(static_cast<int>(divX), 13.0f, headerH - 13.0f);

    // Brand Subtitle
    float brandX = divX + 14.0f;
    g.setColour(StudioStyle::textMuted);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText("HARMONIC & TEMPO SUITE", static_cast<int>(brandX), 0, 160, static_cast<int>(headerH), juce::Justification::centredLeft);

    // ══ LOADED FILE BANNER RIBBON (Dedicated High-End Status Bar) ══════════════
    const float pad = 12.0f;
    const float bannerY = 48.0f;
    const float bannerH = 32.0f;
    const float bannerW = static_cast<float>(W) - pad * 2.0f;
    juce::Rectangle<float> bannerRect(pad, bannerY, bannerW, bannerH);

    bool hasFile = currentLoadedFileName.isNotEmpty();

    // Banner Glass Background
    g.setColour(StudioStyle::cardSurfaceTop.withAlpha(hasFile ? 0.65f : 0.35f));
    g.fillRoundedRectangle(bannerRect, 8.0f);
    g.setColour(hasFile ? StudioStyle::accentMint.withAlpha(0.35f) : StudioStyle::cardBorder);
    g.drawRoundedRectangle(bannerRect, 8.0f, 1.0f);

    if (hasFile)
    {
        // Music note icon
        float iconX = pad + 14.0f;
        g.setColour(StudioStyle::accentMint);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText(juce::String::charToString(0x266B), static_cast<int>(iconX), static_cast<int>(bannerY), 18, static_cast<int>(bannerH), juce::Justification::centredLeft);

        // "LOADED FILE:" Tag
        g.setColour(StudioStyle::accentMint);
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText("LOADED FILE:", static_cast<int>(iconX + 22.0f), static_cast<int>(bannerY), 85, static_cast<int>(bannerH), juce::Justification::centredLeft);

        // File Name
        g.setColour(StudioStyle::textHero);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(currentLoadedFileName, static_cast<int>(iconX + 110.0f), static_cast<int>(bannerY), static_cast<int>(bannerW - 240.0f), static_cast<int>(bannerH), juce::Justification::centredLeft);

        // Status tag (Offline Analyzed)
        float tagW = 120.0f;
        float tagX = bannerRect.getRight() - 110.0f - tagW;
        g.setColour(StudioStyle::accentMint.withAlpha(0.12f));
        juce::Rectangle<float> tagPill(tagX, bannerY + 6.0f, tagW, 20.0f);
        g.fillRoundedRectangle(tagPill, 10.0f);
        g.setColour(StudioStyle::accentMint.withAlpha(0.40f));
        g.drawRoundedRectangle(tagPill, 10.0f, 1.0f);
        g.setColour(StudioStyle::accentMint);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText("OFFLINE ANALYZED", tagPill, juce::Justification::centred);
    }
    else if (isAnalyzingFile)
    {
        // Pulsing loader banner
        g.setColour(StudioStyle::accentCyan);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText("ANALYZING AUDIO FILE: EXTRACTING HARMONICS & TEMPO...", bannerRect, juce::Justification::centred);
    }
    else
    {
        // Live DAW Monitoring Mode Hint
        float iconX = pad + 14.0f;
        g.setColour(StudioStyle::accentCyan);
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText(juce::String::charToString(0x25C6), static_cast<int>(iconX), static_cast<int>(bannerY), 16, static_cast<int>(bannerH), juce::Justification::centredLeft);

        g.setColour(StudioStyle::accentCyan);
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.drawText("LIVE DAW MONITORING", static_cast<int>(iconX + 18.0f), static_cast<int>(bannerY), 140, static_cast<int>(bannerH), juce::Justification::centredLeft);

        g.setColour(StudioStyle::cardBorder);
        g.drawVerticalLine(static_cast<int>(iconX + 164.0f), bannerY + 8.0f, bannerY + bannerH - 8.0f);

        g.setColour(StudioStyle::textMuted);
        g.setFont(juce::FontOptions(10.0f, juce::Font::plain));
        g.drawText("Listening to live track audio. Drag & Drop any audio file (WAV, MP3, FLAC, AIFF) to analyze offline.",
                   static_cast<int>(iconX + 176.0f), static_cast<int>(bannerY), static_cast<int>(bannerW - 200.0f), static_cast<int>(bannerH), juce::Justification::centredLeft);
    }

    // ══ HERO SECTION (Two Balanced Luxury Cards) ══════════════════════════════
    const float heroY = bannerY + bannerH + 8.0f;
    const float heroH = 224.0f;
    const float cardW = (static_cast<float>(W) - pad * 3.0f) * 0.5f;

    // ─────────────────────────────────────────────────────────────────────────
    // 1. LEFT CARD: MUSICAL SCALE & KEY (Tunebat HPCP Key Detector)
    // ─────────────────────────────────────────────────────────────────────────
    juce::Rectangle<float> scaleCard(pad, heroY, cardW, heroH);
    drawStudioCard(g, scaleCard, "Harmonic Key & Scale (Tunebat HPCP)",
                   isScaleLocked ? StudioStyle::accentMint : StudioStyle::accentViolet);

    float sx = scaleCard.getX() + 20.0f;
    float sy = scaleCard.getY() + 38.0f;
    bool hasScale = (currentScaleName != "Detecting...");

    // Big Hero Scale Title
    juce::Colour scaleColor = isScaleLocked ? StudioStyle::accentMint :
                              (hasScale ? StudioStyle::accentViolet : StudioStyle::textMuted);
    g.setColour(scaleColor);
    g.setFont(juce::FontOptions(36.0f, juce::Font::bold));
    g.drawText(currentScaleName, static_cast<int>(sx), static_cast<int>(sy), static_cast<int>(cardW - 40.0f), 44, juce::Justification::centredLeft);

    // Camelot & Relative Key Glass Capsule
    sy += 50.0f;
    float pillW = cardW - 40.0f;
    juce::Rectangle<float> infoPill(sx, sy, pillW, 34.0f);

    g.setColour(StudioStyle::cardSurfaceTop.withAlpha(0.70f));
    g.fillRoundedRectangle(infoPill, 17.0f);
    g.setColour(StudioStyle::cardBorder);
    g.drawRoundedRectangle(infoPill, 17.0f, 1.0f);

    if (hasScale)
    {
        // Dedicated Camelot Badge with Authentic Wheel Color
        float camW = 46.0f;
        juce::Rectangle<float> camBadge(sx + 5.0f, sy + 4.0f, camW, 26.0f);
        juce::Colour camCol = getCamelotColor(currentCamelot);
        g.setColour(camCol.withAlpha(0.18f));
        g.fillRoundedRectangle(camBadge, 13.0f);
        g.setColour(camCol.withAlpha(0.65f));
        g.drawRoundedRectangle(camBadge, 13.0f, 1.0f);

        g.setColour(camCol);
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText(currentCamelot, camBadge, juce::Justification::centred);

        // Relative Key Label
        g.setColour(StudioStyle::textHero);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        juce::String relText = "RELATIVE: " + currentRelativeKey.toUpperCase();
        g.drawText(relText, static_cast<int>(sx + camW + 16.0f), static_cast<int>(sy), static_cast<int>(pillW - camW - 20.0f), 34, juce::Justification::centredLeft);
    }
    else
    {
        g.setColour(StudioStyle::textMuted);
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText("LISTENING TO CHORD HARMONICS...", infoPill, juce::Justification::centred);
    }

    // Monotonic Analysis Progress Bar
    sy += 52.0f;
    g.setColour(StudioStyle::textMuted);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText("HARMONIC CONFIDENCE", static_cast<int>(sx), static_cast<int>(sy), 160, 12, juce::Justification::left);

    float progClamped = juce::jlimit(0.0f, 1.0f, currentScaleProgress);
    juce::String pctStr = isScaleLocked ? "LOCKED (100%)" : (juce::String(juce::roundToInt(progClamped * 100.0f)) + "%");
    g.setColour(isScaleLocked ? StudioStyle::accentMint : StudioStyle::accentViolet);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText(pctStr, static_cast<int>(sx), static_cast<int>(sy), static_cast<int>(pillW), 12, juce::Justification::right);

    float barY = sy + 18.0f;
    g.setColour(StudioStyle::cardBorder.withAlpha(0.35f));
    g.fillRoundedRectangle(sx, barY, pillW, 5.0f, 2.5f);

    if (progClamped > 0.001f)
    {
        juce::Colour barCol = isScaleLocked ? StudioStyle::accentMint : StudioStyle::accentViolet;
        g.setColour(barCol);
        g.fillRoundedRectangle(sx, barY, pillW * progClamped, 5.0f, 2.5f);

        if (!isScaleLocked && progClamped < 0.99f)
        {
            float tipX = sx + pillW * progClamped;
            g.setColour(barCol.withAlpha(0.7f));
            g.fillEllipse(tipX - 4.0f, barY - 1.5f, 8.0f, 8.0f);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // 2. RIGHT CARD: TEMPO & KICK REPETITION BPM (Tunebat Percival Engine)
    // ─────────────────────────────────────────────────────────────────────────
    float rx = scaleCard.getRight() + pad;
    juce::Rectangle<float> bpmCard(rx, heroY, cardW, heroH);
    drawStudioCard(g, bpmCard, "Acoustic Tempo (Tunebat Engine)",
                   isBpmLocked ? StudioStyle::accentMint : StudioStyle::accentCyan);

    // Discrete Beat LEDs (Top Right)
    float ledsX = bpmCard.getRight() - 92.0f;
    float ledsY = bpmCard.getY() + 14.0f;
    bool isPlaying = audioProcessor.isHostPlaying() || (currentAudioBpm > 0.0f);
    for (int b = 1; b <= 4; ++b)
    {
        float lx = ledsX + static_cast<float>(b - 1) * 18.0f;
        bool lit = isPlaying && (b == currentBeatNum);
        juce::Colour ledCol = (b == 1) ? StudioStyle::accentMint : StudioStyle::accentCyan;

        g.setColour(lit ? ledCol : StudioStyle::cardBorder);
        g.fillEllipse(lx, ledsY, 7.0f, 7.0f);
        if (lit && beatFlashLevel > 0.0f)
        {
            g.setColour(ledCol.withAlpha(beatFlashLevel * 0.45f));
            g.drawEllipse(lx - 3.0f, ledsY - 3.0f, 13.0f, 13.0f, 1.0f);
        }
    }

    float bx = bpmCard.getX() + 20.0f;
    float by = bpmCard.getY() + 38.0f;

    // Giant Fixed BPM Hero Numeral
    juce::String bpmStr = (currentAudioBpm > 0.0f) ? juce::String(juce::roundToInt(currentAudioBpm)) : "---";
    g.setColour(isBpmLocked ? StudioStyle::accentMint : (currentAudioBpm > 0.0f ? StudioStyle::accentCyan : StudioStyle::textMuted));
    g.setFont(juce::FontOptions(48.0f, juce::Font::bold));
    g.drawText(bpmStr, static_cast<int>(bx), static_cast<int>(by), 135, 48, juce::Justification::centredLeft);

    // BPM Unit and Lock Subscript
    float tagX = bx + 134.0f;
    g.setColour(StudioStyle::textHero);
    g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    g.drawText("BPM", static_cast<int>(tagX), static_cast<int>(by + 8.0f), 55, 16, juce::Justification::left);

    g.setColour(isBpmLocked ? StudioStyle::accentMint : StudioStyle::textMuted);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText(isBpmLocked ? "FIXED LOCK" : (currentAudioBpm > 0.0f ? "ANALYZING" : "SEARCHING"), static_cast<int>(tagX), static_cast<int>(by + 28.0f), 90, 14, juce::Justification::left);

    // Status Capsule Badge with Re-calc Button Inside
    by += 54.0f;
    float statusW = cardW - 40.0f;
    juce::Rectangle<float> statusRect(bx, by, statusW, 34.0f);

    juce::Colour statusCol = isBpmLocked ? StudioStyle::accentMint : StudioStyle::accentCyan;
    g.setColour(statusCol.withAlpha(0.10f));
    g.fillRoundedRectangle(statusRect, 17.0f);
    g.setColour(statusCol.withAlpha(0.30f));
    g.drawRoundedRectangle(statusRect, 17.0f, 1.0f);

    // Status Indicator Dot
    g.setColour(statusCol);
    g.fillEllipse(bx + 14.0f, by + 12.0f, 10.0f, 10.0f);
    if (isBpmLocked)
    {
        g.setColour(statusCol.withAlpha(0.40f));
        g.drawEllipse(bx + 11.5f, by + 9.5f, 15.0f, 15.0f, 1.0f);
    }

    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.setColour(StudioStyle::textHero);
    g.drawText(bpmStatus.toUpperCase(), static_cast<int>(bx + 32.0f), static_cast<int>(by), static_cast<int>(statusW - 115.0f), 34, juce::Justification::centredLeft);

    // DAW Host Reference Row
    by += 50.0f;
    g.setColour(StudioStyle::textMuted);
    g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
    g.drawText("DAW PROJECT SYNC", static_cast<int>(bx), static_cast<int>(by), 150, 12, juce::Justification::left);

    by += 16.0f;
    g.setColour(StudioStyle::textHero);
    g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    g.drawText(juce::String(audioProcessor.getTempo(), 1) + " BPM", static_cast<int>(bx), static_cast<int>(by), 150, 18, juce::Justification::left);

    // ══ BOTTOM RIBBON: ACOUSTIC GLASS DOCK (Tuner + Oscilloscope) ═════════════
    float dockY = heroY + heroH + 8.0f;
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
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText(hasNote ? activeNoteName : "---", static_cast<int>(nx), static_cast<int>(dockY), 60, static_cast<int>(dockH), juce::Justification::centredLeft);

    // Center-Left: Precision Cents Calibration Meter
    float gaugeX = nx + 68.0f;
    float gaugeW = 120.0f;
    float gaugeY = dockY + dockH * 0.5f - 3.0f;

    // Track
    g.setColour(StudioStyle::cardBorder.withAlpha(0.35f));
    g.fillRoundedRectangle(gaugeX, gaugeY, gaugeW, 6.0f, 3.0f);

    // Center Zero Pip
    g.setColour(StudioStyle::accentMint.withAlpha(0.70f));
    g.drawVerticalLine(static_cast<int>(gaugeX + gaugeW * 0.5f), gaugeY - 3.0f, gaugeY + 9.0f);

    // Moving Needle Indicator
    if (hasNote)
    {
        float centsClamped = juce::jlimit(-50.0f, 50.0f, smoothedCents);
        float indX = gaugeX + gaugeW * 0.5f + (centsClamped / 50.0f) * (gaugeW * 0.46f);
        g.setColour(noteCol);
        g.fillRoundedRectangle(indX - 2.5f, gaugeY - 3.0f, 5.0f, 12.0f, 2.5f);
    }

    // Right Section: Organic Luminous Oscilloscope Line
    float oscX = gaugeX + gaugeW + 36.0f;
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
    g.setColour(StudioStyle::accentCyan.withAlpha(0.22f));
    g.strokePath(wavePath, juce::PathStrokeType(3.5f));

    // Pass 2: Razor Crisp Luminous Trace
    g.setColour(StudioStyle::accentCyan.withAlpha(0.90f));
    g.strokePath(wavePath, juce::PathStrokeType(1.4f));

    // Micro Watermark
    g.setColour(StudioStyle::textMuted.withAlpha(0.40f));
    g.setFont(juce::FontOptions(8.5f, juce::Font::bold));
    g.drawText("SUPREME TUNER BPM V.2.1.1 • TUNEBAT ENGINE", static_cast<int>(oscX), static_cast<int>(dockY + dockH - 14.0f), static_cast<int>(oscW), 10, juce::Justification::bottomRight);

    // ══ DRAG & DROP HOVER OVERLAY (Tunebat Web Glass Dropzone) ════════════════
    if (isFileHovering)
    {
        auto area = getLocalBounds().toFloat().reduced(8.0f);
        g.setColour(juce::Colours::black.withAlpha(0.85f));
        g.fillRoundedRectangle(area, 14.0f);

        // Dashed glowing border
        g.setColour(StudioStyle::accentCyan);
        g.drawRoundedRectangle(area, 14.0f, 2.0f);

        g.setColour(StudioStyle::textHero);
        g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        g.drawText("DROP AUDIO FILE HERE TO ANALYZE", area.reduced(20.0f), juce::Justification::centred);

        g.setColour(StudioStyle::accentCyan);
        g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
        g.drawText("TUNEBAT ESSENTIA HPCP KEY & ACOUSTIC BPM ENGINE", area.removeFromBottom(area.getHeight() * 0.38f), juce::Justification::centredTop);
    }
}

// ==============================================================================
void TunerBPMPluginAudioProcessorEditor::resized()
{
    const int W = getWidth();
    const float pad = 12.0f;
    const float bannerY = 48.0f;
    const float heroY = bannerY + 32.0f + 8.0f;
    const float cardW = (static_cast<float>(W) - pad * 3.0f) * 0.5f;

    // Top Header Buttons
    resetAllButton.setBounds(W - 80, 11, 68, 24);
    openFileButton.setBounds(W - 176, 11, 88, 24);

    // Eject Button inside the Loaded File Banner (Right side of banner)
    ejectFileButton.setBounds(W - static_cast<int>(pad) - 96, static_cast<int>(bannerY + 4.0f), 90, 24);

    // Re-calc button inside BPM Card (Right-aligned in status pill row)
    float rx = pad + cardW + pad;
    float bx = rx + 20.0f;
    float statusW = cardW - 40.0f;
    float by = heroY + 92.0f;
    unlockBpmButton.setBounds(static_cast<int>(bx + statusW - 76.0f), static_cast<int>(by + 4.0f), 70, 26);
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

    // Audio File State
    currentLoadedFileName = audioProcessor.getLoadedAudioFileName();
    isAnalyzingFile       = audioProcessor.isAnalyzingFile();
    ejectFileButton.setVisible(currentLoadedFileName.isNotEmpty());

    // Beat Pulse Animation (Visual only)
    int beatNum = 1;
    if (audioProcessor.getAndClearBeatTriggered(beatNum))
    {
        beatFlashLevel = 1.0f;
        currentBeatNum = beatNum;
    }
    else
    {
        beatFlashLevel *= 0.88f;
        if (beatFlashLevel < 0.01f) beatFlashLevel = 0.0f;
    }

    repaint();
}
