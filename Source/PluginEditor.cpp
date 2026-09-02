#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

// ==============================================================================
// Premium Dark Aesthetics
// ==============================================================================
namespace Aesthetic {
    const juce::Colour bg           { 0xFF0B0D14 };
    const juce::Colour panel        { 0xFF141824 };
    const juce::Colour panelLighter { 0xFF1C2233 };
    const juce::Colour outline      { 0xFF2A344A };
    
    const juce::Colour textMain     { 0xFFFFFFFF };
    const juce::Colour textDim      { 0xFF8C9AB5 };
    
    const juce::Colour cyan         { 0xFF00F0FF };
    const juce::Colour magenta      { 0xFFFF007F };
    const juce::Colour lime         { 0xFF39FF14 };
    const juce::Colour amber        { 0xFFFFB800 };
    const juce::Colour violet       { 0xFF8A2BE2 };
}

// ==============================================================================
// Custom LookAndFeel
// ==============================================================================
class SupremeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    SupremeLookAndFeel()
    {
        setColour(juce::Slider::textBoxTextColourId, Aesthetic::textMain);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::ComboBox::backgroundColourId, Aesthetic::panelLighter);
        setColour(juce::ComboBox::textColourId, Aesthetic::textMain);
        setColour(juce::ComboBox::outlineColourId, Aesthetic::outline);
        setColour(juce::ComboBox::arrowColourId, Aesthetic::cyan);
        setColour(juce::PopupMenu::backgroundColourId, Aesthetic::panel);
        setColour(juce::PopupMenu::textColourId, Aesthetic::textMain);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, Aesthetic::cyan.withAlpha(0.2f));
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                          const float rotaryStartAngle, const float rotaryEndAngle, juce::Slider& slider) override
    {
        auto radius = (float) juce::jmin(width / 2, height / 2) - 4.0f;
        auto centreX = (float) x + (float) width  * 0.5f;
        auto centreY = (float) y + (float) height * 0.5f;
        auto rx = centreX - radius;
        auto ry = centreY - radius;
        auto rw = radius * 2.0f;
        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

        // Fondo del dial
        g.setColour(Aesthetic::panelLighter);
        g.fillEllipse(rx, ry, rw, rw);
        
        g.setColour(Aesthetic::outline);
        g.drawEllipse(rx, ry, rw, rw, 1.5f);

        // Arco de llenado
        juce::Path filledArc;
        filledArc.addCentredArc(centreX, centreY, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(Aesthetic::cyan);
        g.strokePath(filledArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        
        // Indicador (Needle)
        juce::Path p;
        auto pointerLength = radius * 0.8f;
        auto pointerThickness = 3.0f;
        p.addRectangle(-pointerThickness * 0.5f, -radius + 2.0f, pointerThickness, pointerLength);
        p.applyTransform(juce::AffineTransform::rotation(angle).translated(centreX, centreY));
        
        g.setColour(Aesthetic::textMain);
        g.fillPath(p);
    }
    
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced(1.0f);
        
        juce::Colour baseCol = button.findColour(juce::TextButton::buttonColourId);
        if (shouldDrawButtonAsDown) baseCol = baseCol.brighter(0.2f);
        else if (shouldDrawButtonAsHighlighted) baseCol = baseCol.brighter(0.1f);
        
        g.setColour(baseCol);
        g.fillRoundedRectangle(bounds, 6.0f);
        
        g.setColour(baseCol.brighter(0.3f));
        g.drawRoundedRectangle(bounds, 6.0f, 1.0f);
    }
};

static SupremeLookAndFeel supremeLAF;

// ==============================================================================
// Utility Functions
// ==============================================================================
static void drawPanel(juce::Graphics& g, juce::Rectangle<float> bounds, const juce::String& title, juce::Colour accent)
{
    g.setColour(Aesthetic::panel);
    g.fillRoundedRectangle(bounds, 12.0f);
    
    // Outer glow/border
    g.setColour(Aesthetic::outline);
    g.drawRoundedRectangle(bounds, 12.0f, 1.5f);
    
    // Header background
    juce::Rectangle<float> header = bounds.withHeight(34.0f);
    juce::Path headerPath;
    headerPath.addRoundedRectangle(header.getX(), header.getY(), header.getWidth(), header.getHeight(), 12.0f, 12.0f, true, true, false, false);
    
    juce::ColourGradient grad(accent.withAlpha(0.15f), header.getX(), header.getY(),
                              juce::Colours::transparentBlack, header.getX(), header.getBottom(), false);
    g.setGradientFill(grad);
    g.fillPath(headerPath);
    
    // Title
    g.setColour(accent);
    g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(title.toUpperCase(), header.reduced(14.0f, 0.0f), juce::Justification::centredLeft);
    
    g.setColour(Aesthetic::outline);
    g.drawHorizontalLine((int)header.getBottom(), bounds.getX(), bounds.getRight());
}

static void drawLed(juce::Graphics& g, float cx, float cy, float r, juce::Colour col, bool lit)
{
    g.setColour(lit ? col.withAlpha(0.25f) : juce::Colours::transparentBlack);
    g.fillEllipse(cx - r*3.0f, cy - r*3.0f, r*6.0f, r*6.0f);
    
    g.setColour(lit ? col : Aesthetic::panelLighter);
    g.fillEllipse(cx - r, cy - r, r*2.0f, r*2.0f);
    
    g.setColour(lit ? col.brighter() : Aesthetic::outline);
    g.drawEllipse(cx - r, cy - r, r*2.0f, r*2.0f, 1.5f);
}


// ==============================================================================
// TunerBPMPluginAudioProcessorEditor
// ==============================================================================
TunerBPMPluginAudioProcessorEditor::TunerBPMPluginAudioProcessorEditor(TunerBPMPluginAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    setSize(900, 560);
    std::fill(std::begin(oscilloscopeData), std::end(oscilloscopeData), 0.0f);

    // ── Components Setup ──────────────────────────────────────
    syncModeComboBox.addItem("DAW Sync", 1);
    syncModeComboBox.addItem("Internal", 2);
    syncModeComboBox.setLookAndFeel(&supremeLAF);
    addAndMakeVisible(syncModeComboBox);
    syncModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(audioProcessor.apvts, "syncMode", syncModeComboBox);

    internalPlayButton.setButtonText("PLAY");
    internalPlayButton.setLookAndFeel(&supremeLAF);
    internalPlayButton.setColour(juce::TextButton::buttonColourId, Aesthetic::panelLighter);
    internalPlayButton.setColour(juce::TextButton::textColourOffId, Aesthetic::lime);
    addAndMakeVisible(internalPlayButton);
    internalPlayAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(audioProcessor.apvts, "internalPlay", internalPlayButton);

    tapTempoButton.setButtonText("TAP");
    tapTempoButton.setLookAndFeel(&supremeLAF);
    tapTempoButton.setColour(juce::TextButton::buttonColourId, Aesthetic::panelLighter);
    tapTempoButton.setColour(juce::TextButton::textColourOffId, Aesthetic::cyan);
    tapTempoButton.onClick = [this] { handleTapTempo(); };
    addAndMakeVisible(tapTempoButton);

    resetScaleButton.setButtonText("RESET");
    resetScaleButton.setLookAndFeel(&supremeLAF);
    resetScaleButton.setColour(juce::TextButton::buttonColourId, Aesthetic::panelLighter);
    resetScaleButton.setColour(juce::TextButton::textColourOffId, Aesthetic::magenta);
    resetScaleButton.onClick = [this] { audioProcessor.resetScale(); };
    addAndMakeVisible(resetScaleButton);

    tempoSlider.setRange(40.0, 240.0, 0.5);
    tempoSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    tempoSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    tempoSlider.setLookAndFeel(&supremeLAF);
    addAndMakeVisible(tempoSlider);
    tempoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "internalTempo", tempoSlider);

    volumeSlider.setRange(0.0, 1.0, 0.01);
    volumeSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    volumeSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 20);
    volumeSlider.setLookAndFeel(&supremeLAF);
    addAndMakeVisible(volumeSlider);
    volumeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(audioProcessor.apvts, "volume", volumeSlider);

    tempoLabel.setText("TEMPO", juce::dontSendNotification);
    tempoLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    tempoLabel.setColour(juce::Label::textColourId, Aesthetic::textDim);
    tempoLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(tempoLabel);

    volumeLabel.setText("CLICK VOL", juce::dontSendNotification);
    volumeLabel.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    volumeLabel.setColour(juce::Label::textColourId, Aesthetic::textDim);
    volumeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(volumeLabel);

    startTimerHz(60);
}

TunerBPMPluginAudioProcessorEditor::~TunerBPMPluginAudioProcessorEditor()
{
    tempoSlider.setLookAndFeel(nullptr);
    volumeSlider.setLookAndFeel(nullptr);
    syncModeComboBox.setLookAndFeel(nullptr);
    internalPlayButton.setLookAndFeel(nullptr);
    tapTempoButton.setLookAndFeel(nullptr);
    resetScaleButton.setLookAndFeel(nullptr);
}

// ==============================================================================
void TunerBPMPluginAudioProcessorEditor::paint(juce::Graphics& g)
{
    const int W = getWidth();
    const int H = getHeight();

    // ══ BACKGROUND ═══════════════════════════════════════════════════════════
    g.fillAll(Aesthetic::bg);
    
    // Background glow/mesh
    juce::ColourGradient bgGrad(Aesthetic::cyan.withAlpha(0.03f), 0, 0, juce::Colours::transparentBlack, (float)W, (float)H, false);
    g.setGradientFill(bgGrad);
    g.fillAll();

    // ══ HEADER ═══════════════════════════════════════════════════════════════
    const float headerH = 60.0f;
    g.setColour(Aesthetic::panel);
    g.fillRect(0.0f, 0.0f, (float)W, headerH);
    g.setColour(Aesthetic::outline);
    g.drawHorizontalLine((int)headerH, 0.0f, (float)W);
    
    g.setColour(Aesthetic::magenta);
    g.fillRect(0.0f, 0.0f, 4.0f, headerH);

    g.setColour(Aesthetic::textMain);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("SUPREME TUNER BPM", 24, 0, 300, (int)headerH, juce::Justification::centredLeft);

    g.setColour(Aesthetic::textDim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("v1.1", W - 40, 0, 30, (int)headerH, juce::Justification::centredRight);

    // ══ LAYOUT ═══════════════════════════════════════════════════════════════
    float pad = 12.0f;
    float contentY = headerH + pad;
    float contentH = H - contentY - pad;
    float leftW = 460.0f;
    float rightW = W - leftW - pad * 3.0f;

    // ─────────────────────────────────────────────────────────────────────────
    // TUNER PANEL (Left, Top)
    // ─────────────────────────────────────────────────────────────────────────
    float tunerH = contentH * 0.65f;
    juce::Rectangle<float> tunerCard(pad, contentY, leftW, tunerH);
    drawPanel(g, tunerCard, "Pitch Tuner", Aesthetic::lime);

    float tcx = tunerCard.getCentreX();
    float tcy = tunerCard.getY() + 175.0f;
    float R = 130.0f;

    // Dial Track
    float startA = juce::degreesToRadians(-65.0f);
    float endA   = juce::degreesToRadians(65.0f);
    
    juce::Path arcTrack;
    arcTrack.addCentredArc(tcx, tcy, R, R, 0.0f, startA, endA, true);
    g.setColour(Aesthetic::panelLighter);
    g.strokePath(arcTrack, juce::PathStrokeType(14.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    bool hasPitch = (smoothedFreq > 1.0f);
    bool inTune   = hasPitch && std::abs(smoothedCents) <= 3.0f;
    juce::Colour needleCol = hasPitch ? (inTune ? Aesthetic::lime : Aesthetic::amber) : Aesthetic::textDim;

    // Glow Track
    if (hasPitch)
    {
        float centsClamped = std::max(-50.0f, std::min(50.0f, smoothedCents));
        float ratio = (centsClamped + 50.0f) / 100.0f;
        float fillEnd = startA + ratio * (endA - startA);
        float midA = (startA + endA) * 0.5f;
        
        juce::Path filledTrack;
        filledTrack.addCentredArc(tcx, tcy, R, R, 0.0f, std::min(midA, fillEnd), std::max(midA, fillEnd), true);
        g.setColour(needleCol.withAlpha(0.6f));
        g.strokePath(filledTrack, juce::PathStrokeType(14.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Ticks
    for (int c = -50; c <= 50; c += 10)
    {
        float ratio = (c + 50.0f) / 100.0f;
        float angle = startA + ratio * (endA - startA);
        float innerR = (c % 50 == 0) ? R - 8.0f : R - 4.0f;
        float outerR = R + 8.0f;
        g.setColour(c == 0 ? Aesthetic::lime : Aesthetic::outline.brighter());
        g.drawLine(tcx + innerR * std::sin(angle), tcy - innerR * std::cos(angle),
                   tcx + outerR * std::sin(angle), tcy - outerR * std::cos(angle),
                   c == 0 ? 3.0f : 1.5f);
    }

    // Needle
    float needleAngle = 0.0f;
    if (hasPitch)
    {
        float cc = std::max(-50.0f, std::min(50.0f, smoothedCents));
        needleAngle = (cc / 50.0f) * juce::degreesToRadians(65.0f);
    }
    float nx = tcx + (R - 20.0f) * std::sin(needleAngle);
    float ny = tcy - (R - 20.0f) * std::cos(needleAngle);

    if (hasPitch)
    {
        g.setColour(needleCol.withAlpha(0.2f));
        g.drawLine(tcx, tcy, nx, ny, 8.0f); // Outer glow
    }
    g.setColour(needleCol);
    g.drawLine(tcx, tcy, nx, ny, 2.5f);
    
    g.setColour(Aesthetic::panel);
    g.fillEllipse(tcx - 8.0f, tcy - 8.0f, 16.0f, 16.0f);
    g.setColour(needleCol);
    g.drawEllipse(tcx - 8.0f, tcy - 8.0f, 16.0f, 16.0f, 2.0f);

    // Note Display
    g.setColour(needleCol);
    g.setFont(juce::FontOptions(64.0f, juce::Font::bold));
    g.drawText(activeNoteName.isEmpty() ? "---" : activeNoteName,
               (int)(tcx - 100.0f), (int)(tcy - 120.0f), 200, 70, juce::Justification::centred);

    g.setColour(Aesthetic::textDim);
    g.setFont(juce::FontOptions(14.0f));
    juce::String freqStr = hasPitch ? juce::String(smoothedFreq, 2) + " Hz" : "—  Hz";
    g.drawText(freqStr, (int)(tcx - 100.0f), (int)(tcy - 40.0f), 200, 20, juce::Justification::centred);

    // ─────────────────────────────────────────────────────────────────────────
    // KEY / SCALE CARD (Left, Bottom)
    // ─────────────────────────────────────────────────────────────────────────
    float keyCardY = tunerCard.getBottom() + pad;
    float keyCardH = contentH - tunerH - pad;
    juce::Rectangle<float> keyCard(pad, keyCardY, leftW, keyCardH);
    drawPanel(g, keyCard, "Scale / Key Detector", Aesthetic::magenta);

    float kx = keyCard.getX() + 20.0f;
    float ky = keyCard.getY() + 45.0f;
    bool hasScale = (currentScaleName != "Detecting...");

    g.setColour(hasScale ? Aesthetic::magenta : Aesthetic::textDim);
    g.setFont(juce::FontOptions(34.0f, juce::Font::bold));
    g.drawText(currentScaleName, (int)kx, (int)ky, 300, 40, juce::Justification::centredLeft);

    float barY = ky + 50.0f;
    float barW = leftW - 120.0f;
    g.setColour(Aesthetic::panelLighter);
    g.fillRoundedRectangle(kx, barY, barW, 8.0f, 4.0f);
    if (hasScale)
    {
        float conf = std::max(0.0f, std::min(1.0f, currentScaleConfidence));
        juce::ColourGradient cg(Aesthetic::magenta, kx, barY, Aesthetic::cyan, kx + barW, barY, false);
        g.setGradientFill(cg);
        g.fillRoundedRectangle(kx, barY, barW * conf, 8.0f, 4.0f);
        
        g.setColour(Aesthetic::magenta);
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(juce::String((int)(currentScaleConfidence * 100.0f)) + "%",
                   (int)(kx + barW + 10.0f), (int)barY - 4, 50, 16, juce::Justification::centredLeft);
    }
    
    g.setColour(Aesthetic::textDim);
    g.setFont(juce::FontOptions(10.0f));
    g.drawText("CONFIDENCE", (int)kx, (int)barY - 14, 100, 12, juce::Justification::left);

    // ─────────────────────────────────────────────────────────────────────────
    // METRONOME & BPM CARD (Right, Top)
    // ─────────────────────────────────────────────────────────────────────────
    float rightX = tunerCard.getRight() + pad;
    float bpmH = contentH * 0.5f;
    juce::Rectangle<float> beatCard(rightX, contentY, rightW, bpmH);
    drawPanel(g, beatCard, "Metronome & Auto BPM", Aesthetic::cyan);

    float bcx = beatCard.getCentreX();
    float bcy = beatCard.getY() + 100.0f;

    // LEDs
    float ledSpacing = 40.0f;
    float ledsStartX = bcx - ledSpacing * 1.5f;
    bool playing = audioProcessor.isHostPlaying();
    for (int b = 1; b <= 4; ++b)
    {
        float lx = ledsStartX + (b - 1) * ledSpacing;
        bool lit = playing && (b == currentBeatNum);
        juce::Colour ledCol = (b == 1) ? Aesthetic::lime : Aesthetic::cyan;
        drawLed(g, lx, beatCard.getY() + 55.0f, 10.0f, ledCol, lit);

        if (lit && beatFlashLevel > 0.0f)
        {
            g.setColour(ledCol.withAlpha(beatFlashLevel * 0.5f));
            g.drawEllipse(lx - 16.0f, beatCard.getY() + 55.0f - 16.0f, 32.0f, 32.0f, 2.5f);
        }
    }

    g.setColour(Aesthetic::cyan);
    g.setFont(juce::FontOptions(48.0f, juce::Font::bold));
    g.drawText(juce::String(audioProcessor.getTempo(), 1),
               (int)(bcx - 100.0f), (int)(bcy), 200, 50, juce::Justification::centred);

    g.setColour(Aesthetic::textDim);
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("CURRENT BPM", (int)(bcx - 100.0f), (int)(bcy + 52.0f), 200, 14, juce::Justification::centred);

    // Auto BPM
    float autoY = bcy + 85.0f;
    g.setColour(Aesthetic::panelLighter);
    g.fillRoundedRectangle(bcx - 90.0f, autoY, 180.0f, 40.0f, 6.0f);
    
    g.setColour(Aesthetic::textDim);
    g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
    g.drawText("AUTO DETECT", (int)(bcx - 80.0f), (int)autoY + 12, 80, 16, juce::Justification::left);
    
    g.setColour(currentAudioBpm > 0.0f ? Aesthetic::cyan : Aesthetic::textDim);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    juce::String abpmStr = currentAudioBpm > 0.0f ? juce::String(juce::roundToInt(currentAudioBpm)) : "---";
    g.drawText(abpmStr, (int)(bcx), (int)autoY + 10, 80, 20, juce::Justification::right);

    // ─────────────────────────────────────────────────────────────────────────
    // OSCILLOSCOPE (Right, Bottom)
    // ─────────────────────────────────────────────────────────────────────────
    float oscY = beatCard.getBottom() + pad;
    float oscH = contentH - bpmH - pad;
    juce::Rectangle<float> oscCard(rightX, oscY, rightW, oscH);
    drawPanel(g, oscCard, "Waveform", Aesthetic::violet);

    {
        audioProcessor.getOscilloscopeBuffer(oscilloscopeData);
        float ox = oscCard.getX() + 10.0f;
        float oy = oscCard.getY() + 40.0f;
        float ow = oscCard.getWidth() - 20.0f;
        float oh = oscCard.getHeight() - 50.0f;
        float ocY = oy + oh * 0.5f;

        g.setColour(Aesthetic::panelLighter);
        g.fillRoundedRectangle(ox, oy, ow, oh, 6.0f);

        g.setColour(Aesthetic::outline);
        g.drawHorizontalLine((int)ocY, ox, ox + ow);

        juce::Path wavePath;
        const int sz = TunerBPMPluginAudioProcessor::oscilloscopeSize;
        for (int i = 0; i < sz; ++i)
        {
            float x = ox + (float)i / (float)sz * ow;
            float s = std::max(-1.0f, std::min(1.0f, oscilloscopeData[i]));
            float y = ocY - s * (oh * 0.45f);
            if (i == 0) wavePath.startNewSubPath(x, y);
            else        wavePath.lineTo(x, y);
        }
        g.setColour(Aesthetic::violet.withAlpha(0.2f));
        g.strokePath(wavePath, juce::PathStrokeType(4.0f));
        g.setColour(Aesthetic::violet.withAlpha(0.9f));
        g.strokePath(wavePath, juce::PathStrokeType(1.5f));
    }
}

// ==============================================================================
void TunerBPMPluginAudioProcessorEditor::resized()
{
    const int W = getWidth();
    const int H = getHeight();
    float pad = 12.0f;
    float contentY = 60.0f + pad;
    float contentH = H - contentY - pad;
    
    // Scale card reset button
    float leftW = 460.0f;
    float tunerH = contentH * 0.65f;
    float keyCardY = contentY + tunerH + pad;
    resetScaleButton.setBounds((int)(pad + leftW - 75.0f), (int)(keyCardY + 11.0f), 60, 20);

    // Right column controls (Inside Metronome Card)
    float rightX = pad + leftW + pad;
    float rightW = W - rightX - pad;
    
    float controlsY = contentY + 235.0f;
    float knobSize = 65.0f;
    
    tempoSlider.setBounds((int)(rightX + 20.0f), (int)controlsY, (int)knobSize, (int)knobSize);
    tempoLabel.setBounds((int)(rightX + 20.0f), (int)(controlsY - 16.0f), (int)knobSize, 14);

    volumeSlider.setBounds((int)(rightX + 100.0f), (int)controlsY, (int)knobSize, (int)knobSize);
    volumeLabel.setBounds((int)(rightX + 100.0f), (int)(controlsY - 16.0f), (int)knobSize, 14);

    float btnX = rightX + 190.0f;
    float btnW = rightW - 210.0f;
    internalPlayButton.setBounds((int)btnX, (int)controlsY, (int)btnW, 22);
    tapTempoButton.setBounds((int)btnX, (int)(controlsY + 28.0f), (int)btnW, 22);
    syncModeComboBox.setBounds((int)btnX, (int)(controlsY + 56.0f), (int)btnW, 22);
}

// ==============================================================================
void TunerBPMPluginAudioProcessorEditor::timerCallback()
{
    float targetFreq  = audioProcessor.getDetectedFrequency();
    float targetCents = audioProcessor.getCentsDeviation();

    if (targetFreq > 1.0f)
    {
        smoothedFreq  = smoothedFreq  * 0.70f + targetFreq  * 0.30f; // Faster response
        smoothedCents = smoothedCents * 0.65f + targetCents * 0.35f;
        activeNoteName = audioProcessor.getDetectedNoteName();
    }
    else
    {
        smoothedFreq  *= 0.85f;
        smoothedCents *= 0.85f;
        if (smoothedFreq < 0.5f)
        {
            smoothedFreq  = 0.0f;
            activeNoteName = "---";
        }
    }

    currentScaleName      = audioProcessor.getDetectedScaleName();
    currentScaleConfidence = audioProcessor.getScaleConfidence();
    currentAudioBpm        = audioProcessor.getDetectedAudioBpm();

    int beatNum = 1;
    if (audioProcessor.getAndClearBeatTriggered(beatNum))
    {
        beatFlashLevel = 1.0f;
        currentBeatNum = beatNum;
    }
    else
    {
        beatFlashLevel *= 0.82f;
        if (beatFlashLevel < 0.01f) beatFlashLevel = 0.0f;
    }

    int mode = (int)(*audioProcessor.apvts.getRawParameterValue("syncMode"));
    bool inInternal = (mode == 1);
    tempoSlider.setEnabled(inInternal);
    internalPlayButton.setEnabled(inInternal);
    tapTempoButton.setEnabled(inInternal);

    if (inInternal)
    {
        bool pl = (bool)(*audioProcessor.apvts.getRawParameterValue("internalPlay"));
        internalPlayButton.setButtonText(pl ? "PAUSE" : "PLAY");
        internalPlayButton.setColour(juce::TextButton::textColourOffId, pl ? Aesthetic::amber : Aesthetic::lime);
    }
    else
    {
        internalPlayButton.setButtonText("PLAY");
        internalPlayButton.setColour(juce::TextButton::textColourOffId, Aesthetic::textDim);
    }

    repaint();
}

void TunerBPMPluginAudioProcessorEditor::handleTapTempo()
{
    auto now = juce::Time::getMillisecondCounter();
    if (!tapTimes.empty() && (now - tapTimes.back() > 2000))
        tapTimes.clear();

    tapTimes.push_back(now);
    if (tapTimes.size() > 6) tapTimes.erase(tapTimes.begin());

    if (tapTimes.size() >= 2)
    {
        double total = 0.0;
        for (size_t i = 1; i < tapTimes.size(); ++i)
            total += (double)(tapTimes[i] - tapTimes[i - 1]);
        double avg = total / (double)(tapTimes.size() - 1);
        if (avg > 0.0)
        {
            double bpm = std::max(40.0, std::min(240.0, 60000.0 / avg));
            tempoSlider.setValue(bpm, juce::sendNotification);
        }
    }
}
