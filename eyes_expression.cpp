// connection configuration as can use any of pin as i just used 16/17 as 
//     vcc ----- 3.3v
//     gnd ----- gnd
//     sda ------ 16 (RX0)
//     scl ------ 17(TX0)


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
// GLOBALS
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
// SETUP
// =====================================================

void setup()
{
    Wire.begin(SDA_PIN, SCL_PIN);

    u8g2.setBusClock(800000);

    u8g2.begin();

    randomSeed(analogRead(0));
}

// =====================================================
// CURVED BLINK
// =====================================================

void drawBlinkCurve(int cx, int cy, int w)
{
    u8g2.setDrawColor(1);

    for (int x = -w / 2; x <= w / 2; x++)
    {
        float t = (float)x / (w / 2.0);

        // curved eyelid
        int y = cy + (t * t * 3);

        // slight tilt
        y -= x * 0.04;

        u8g2.drawDisc(cx + x, y, 1);
    }
}

// =====================================================
// EYE
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
    // =================================================
    // ZOOM EFFECT
    // =================================================

    int eyeW = (compress ? 38 : 42) * zoomEffect;

    int eyeH;

    if (squint)
        eyeH = 18 * zoomEffect;
    else
        eyeH = 28 * zoomEffect;

    // =================================================
    // BLINK MODE
    // =================================================

    if (blink)
    {
        drawBlinkCurve(cx, cy, eyeW - 6);
        return;
    }

    // =================================================
    // WHITE EYE SHAPE
    // =================================================

    u8g2.setDrawColor(1);

    for (int y = -eyeH / 2; y <= eyeH / 2; y++)
    {
        float t = (float)y / (eyeH / 2.0);

        // safer curve avoids edge gaps
        float curve = 1.0 - (t * t);

        if (curve < 0)
            curve = 0;

        // softer squint
        if (squint)
            curve *= 1.05;

        int lineW = curve * eyeW;

        // overlap helps remove gaps
        lineW += 2;

        int startX = cx - lineW / 2;

        // double-line fill fixes OLED skips
        u8g2.drawHLine(startX, cy + y, lineW);

        if (y % 2 == 0)
            u8g2.drawHLine(startX, cy + y + 1, lineW);
    }

    // =================================================
    // TOP SHADOW
    // =================================================

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

    // =================================================
    // IRIS POSITION
    // =================================================

    float maxMoveX = 7;

    float nx = lookX / maxMoveX;

    nx = nx * abs(nx);

    int irisX = cx + nx * maxMoveX;

    int irisY = cy;

    // =================================================
    // IRIS
    // =================================================

    u8g2.setDrawColor(0);

    u8g2.drawDisc(
        irisX,
        irisY,
        9 * zoomEffect
    );

    u8g2.drawDisc(
        irisX,
        irisY,
        4 * zoomEffect
    );

    // =================================================
    // HIGHLIGHT
    // =================================================

    u8g2.setDrawColor(1);

    u8g2.drawDisc(
        irisX - 3,
        irisY - 3,
        2
    );

    u8g2.drawLine(
        irisX + 2,
        irisY,
        irisX + 5,
        irisY
    );

    u8g2.drawLine(
        irisX + 4,
        irisY - 1,
        irisX + 4,
        irisY + 1
    );
}

// =====================================================
// MOUTH
// =====================================================

void drawMouth(int mode)
{
    int mx = 64;
    int my = 52;

    u8g2.setDrawColor(1);

    switch (mode)
    {
        // smile
        case 0:

            u8g2.drawLine(mx - 8, my, mx - 3, my + 3);
            u8g2.drawLine(mx - 3, my + 3, mx + 3, my + 3);
            u8g2.drawLine(mx + 3, my + 3, mx + 8, my);

            break;

        // talk open
        case 1:

            u8g2.drawRBox(mx - 4, my - 1, 8, 7, 2);

            break;

        // talk wide
        case 2:

            u8g2.drawRBox(mx - 8, my, 16, 5, 2);

            break;

        // cute tiny
        case 3:

            u8g2.drawDisc(mx - 2, my + 1, 1);
            u8g2.drawDisc(mx + 2, my + 1, 1);

            break;
    }
}

// =====================================================
// FACE
// =====================================================

void drawFace(float lookX)
{
    bool leftCompress = lookX > 2;
    bool rightCompress = lookX < -2;

    drawEye(
        38,
        28,
        lookX,
        squintMode,
        blinkBoth || blinkLeftOnly,
        leftCompress,
        speaking
    );

    drawEye(
        90,
        28,
        lookX,
        squintMode,
        blinkBoth,
        rightCompress,
        speaking
    );

    // mouth
    if (speaking)
    {
        if (talkingFrame % 6 < 3)
            drawMouth(1);
        else
            drawMouth(2);
    }
    else if (squintMode)
    {
        drawMouth(3);
    }
    else
    {
        drawMouth(0);
    }
}

// =====================================================
// BLINK
// =====================================================

void updateBlink()
{
    if (!blinkBoth && millis() - lastBlink > 5000)
    {
        blinkBoth = true;
        lastBlink = millis();
    }

    if (blinkBoth && millis() - lastBlink > 130)
    {
        blinkBoth = false;
    }

    // left wink
    if (!blinkLeftOnly && random(0, 900) == 1)
    {
        blinkLeftOnly = true;
    }

    if (blinkLeftOnly && random(0, 10) > 7)
    {
        blinkLeftOnly = false;
    }
}

// =====================================================
// EYE MOVEMENT
// =====================================================

void updateEyeMovement()
{
    if (millis() - lastMove > random(1800, 3200))
    {
        int dir = random(0, 3);

        if (dir == 0)
            targetEyeX = -5;

        if (dir == 1)
            targetEyeX = 5;

        if (dir == 2)
            targetEyeX = 0;

        lastMove = millis();
    }

    eyePosX += (targetEyeX - eyePosX) * 0.08;
}

// =====================================================
// EXPRESSIONS
// =====================================================

void updateExpressions()
{
    speaking = ((millis() / 4000) % 2 == 1);

    squintMode = ((millis() / 7000) % 2 == 1);

    talkingFrame++;

    // future zoom pulse effect
    zoomEffect = 1.0 + sin(millis() * 0.002) * 0.03;
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
    updateBlink();

    updateEyeMovement();

    updateExpressions();

    u8g2.clearBuffer();

    drawFace(eyePosX);

    u8g2.sendBuffer();

    delay(20);
}
