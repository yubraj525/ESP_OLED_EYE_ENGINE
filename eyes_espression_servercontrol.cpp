#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <math.h>

#define SDA_PIN 16
#define SCL_PIN 17

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(
    U8G2_R0,
    U8X8_PIN_NONE,
    SCL_PIN,
    SDA_PIN
);

// =====================================================
// GLOBALS (UNCHANGED CORE)
// =====================================================

float eyePosX = 0;
float targetEyeX = 0;

float zoomEffect = 1.0;

bool blinkBoth = false;
bool blinkLeftOnly = false;

bool speaking = false;
bool squintMode = false;

unsigned long lastBlink = 0;
unsigned long lastMove = 0;

int talkingFrame = 0;

// =====================================================
// EMOTION CONTROL LAYER (NEW)
// =====================================================

enum Emotion {
    NEUTRAL,
    HAPPY,
    SAD,
    SPEAK
};

Emotion currentEmotion = NEUTRAL;

unsigned long emotionStartTime = 0;

// =====================================================
// SERIAL DEBUG
// =====================================================

void printEmotion(const char* name)
{
    Serial.print("EMOTION: ");
    Serial.println(name);
}

// =====================================================
// EMOTION FUNCTIONS (NEW API)
// =====================================================

void setEmotion_NEUTRAL()
{
    currentEmotion = NEUTRAL;
    emotionStartTime = millis();
    printEmotion("NEUTRAL");
}

void setEmotion_HAPPY()
{
    currentEmotion = HAPPY;
    emotionStartTime = millis();
    printEmotion("HAPPY");
}

void setEmotion_SAD()
{
    currentEmotion = SAD;
    emotionStartTime = millis();
    printEmotion("SAD");
}

void setEmotion_SPEAK()
{
    currentEmotion = SPEAK;
    emotionStartTime = millis();
    printEmotion("SPEAK");
}

// =====================================================
// AUTO RETURN TO NEUTRAL
// =====================================================

void updateEmotionTimeout()
{
    if (millis() - emotionStartTime > 5000)
    {
        setEmotion_NEUTRAL();
    }
}

// =====================================================
// CURVED BLINK (UNCHANGED)
// =====================================================

void drawBlinkCurve(int cx, int cy, int w)
{
    u8g2.setDrawColor(1);

    for (int x = -w / 2; x <= w / 2; x++)
    {
        float t = (float)x / (w / 2.0);
        int y = cy + (t * t * 3);
        y -= x * 0.04;
        u8g2.drawDisc(cx + x, y, 1);
    }
}

// =====================================================
// EYE (UNCHANGED)
// =====================================================

void drawEye(
    int cx,
    int cy,
    float lookX,
    bool squint,
    bool blink,
    bool compress,
    bool removeTopCurve
)
{
    int eyeW = (compress ? 38 : 42) * zoomEffect;

    int eyeH;

    if (squint)
        eyeH = 18 * zoomEffect;
    else
        eyeH = 28 * zoomEffect;

    if (blink)
    {
        drawBlinkCurve(cx, cy, eyeW - 6);
        return;
    }

    u8g2.setDrawColor(1);

    for (int y = -eyeH / 2; y <= eyeH / 2; y++)
    {
        float t = (float)y / (eyeH / 2.0);

        float curve = 1.0 - (t * t);
        if (curve < 0) curve = 0;

        if (squint)
            curve *= 1.05;

        int lineW = curve * eyeW + 2;
        int startX = cx - lineW / 2;

        u8g2.drawHLine(startX, cy + y, lineW);

        if (y % 2 == 0)
            u8g2.drawHLine(startX, cy + y + 1, lineW);
    }

    if (!removeTopCurve)
    {
        u8g2.setDrawColor(0);

        for (int i = 0; i < 2; i++)
        {
            u8g2.drawLine(
                cx - eyeW / 2 + 6,
                cy - eyeH / 2 + 3 + i,
                cx + eyeW / 2 - 6,
                cy - eyeH / 2 + 2 + i
            );
        }
    }

    float maxMoveX = 7;
    float nx = lookX / maxMoveX;
    nx = nx * abs(nx);

    int irisX = cx + nx * maxMoveX;
    int irisY = cy;

    u8g2.setDrawColor(0);

    u8g2.drawDisc(irisX, irisY, 9 * zoomEffect);
    u8g2.drawDisc(irisX, irisY, 4 * zoomEffect);

    u8g2.setDrawColor(1);

    u8g2.drawDisc(irisX - 3, irisY - 3, 2);
}

// =====================================================
// MOUTH (ONLY BEHAVIOR CHANGES)
// =====================================================

void drawMouth(int mode)
{
    int mx = 64;
    int my = 52;

    u8g2.setDrawColor(1);

    switch (mode)
    {
        case 0:
            u8g2.drawLine(mx - 8, my, mx - 3, my + 3);
            u8g2.drawLine(mx - 3, my + 3, mx + 3, my + 3);
            u8g2.drawLine(mx + 3, my + 3, mx + 8, my);
            break;

        case 1:
            u8g2.drawRBox(mx - 4, my - 1, 8, 7, 2);
            break;

        case 2:
            u8g2.drawRBox(mx - 8, my, 16, 5, 2);
            break;

        case 3:
            u8g2.drawDisc(mx - 2, my + 1, 1);
            u8g2.drawDisc(mx + 2, my + 1, 1);
            break;
    }
}

// =====================================================
// FACE (NOW CONTROLLED BY EMOTION)
// =====================================================

void drawFace(float lookX)
{
    bool blink = blinkBoth || blinkLeftOnly;

    bool leftCompress = lookX > 2;
    bool rightCompress = lookX < -2;

    drawEye(38, 28, lookX, squintMode, blink, leftCompress, speaking);
    drawEye(90, 28, lookX, squintMode, blink, rightCompress, speaking);

    // =================================================
    // EMOTION → MOUTH MAP
    // =================================================

    if (currentEmotion == HAPPY)
        drawMouth(0);

    else if (currentEmotion == SAD)
        drawMouth(0); // simple sad fallback (keep structure unchanged)

    else if (currentEmotion == SPEAK)
    {
        if (talkingFrame % 6 < 3)
            drawMouth(1);
        else
            drawMouth(2);
    }
    else
        drawMouth(0);
}

// =====================================================
// BLINK + MOVEMENT (UNCHANGED BEHAVIOR)
// =====================================================

void updateBlink()
{
    if (!blinkBoth && millis() - lastBlink > 5000)
    {
        blinkBoth = true;
        lastBlink = millis();
    }

    if (blinkBoth && millis() - lastBlink > 130)
        blinkBoth = false;
}

void updateEyeMovement()
{
    if (millis() - lastMove > random(1800, 3200))
    {
        targetEyeX = random(-5, 6);
        lastMove = millis();
    }

    eyePosX += (targetEyeX - eyePosX) * 0.08;
}

void updateExpressions()
{
    speaking = (currentEmotion == SPEAK);

    squintMode = (currentEmotion == SAD);

    talkingFrame++;

    zoomEffect = 1.0 + sin(millis() * 0.002) * 0.03;
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
    Serial.begin(115200);

    Wire.begin(SDA_PIN, SCL_PIN);
    u8g2.setBusClock(800000);
    u8g2.begin();

    randomSeed(analogRead(0));

    setEmotion_NEUTRAL();
}

// =====================================================
// LOOP (SERIAL DEMO + 5s AUTO RESET)
// =====================================================

void loop()
{
    updateEmotionTimeout();

    updateBlink();
    updateEyeMovement();
    updateExpressions();

    // =================================================
    // SERIAL DEMO CONTROL (REQUIRED BY YOU)
    // =================================================

    static int step = 0;

    if (millis() % 5000 < 20)
    {
        step++;

        if (step == 1) setEmotion_HAPPY();
        else if (step == 2) setEmotion_SAD();
        else if (step == 3) setEmotion_SPEAK();
        else
        {
            setEmotion_NEUTRAL();
            step = 0;
        }
    }

    // =================================================
    // RENDER
    // =================================================

    u8g2.clearBuffer();
    drawFace(eyePosX);
    u8g2.sendBuffer();

    delay(20);
}