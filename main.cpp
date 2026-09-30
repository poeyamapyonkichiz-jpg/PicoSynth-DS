#include <nds.h>
#include <stdio.h>

// ============================================================
// PicoSynth DS v0.2
// Nintendo DS PSG synthesizer
// ============================================================

static const int SCREEN_W = 256;
static const int SCREEN_H = 192;

// ------------------------------------------------------------
// Keyboard layout
// ------------------------------------------------------------

static const int HEADER_H       = 32;
static const int KEYBOARD_TOP   = 34;
static const int WHITE_KEY_W    = 32;
static const int WHITE_KEY_H    = SCREEN_H - KEYBOARD_TOP;
static const int BLACK_KEY_W    = 19;
static const int BLACK_KEY_H    = 88;

static const int WHITE_COUNT = 8;
static const int BLACK_COUNT = 5;

static const int whiteSemitone[WHITE_COUNT] = {
    0, 2, 4, 5, 7, 9, 11, 12
};

static const int blackSemitone[BLACK_COUNT] = {
    1, 3, 6, 8, 10
};

// 黒鍵が置かれる白鍵境界
static const int blackGap[BLACK_COUNT] = {
    0, 1, 3, 4, 5
};

static const char* noteNames[13] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B", "C"
};

// C4 ～ C5
static const int baseFreq[13] = {
    262, 277, 294, 311, 330, 349,
    370, 392, 415, 440, 466, 494, 523
};

// ------------------------------------------------------------
// Synth state
// ------------------------------------------------------------

static const DutyCycle dutyValues[] = {
    DutyCycle_12,
    DutyCycle_25,
    DutyCycle_37,
    DutyCycle_50,
    DutyCycle_62,
    DutyCycle_75,
    DutyCycle_87
};

static const char* dutyNames[] = {
    "12.5%",
    "25.0%",
    "37.5%",
    "50.0%",
    "62.5%",
    "75.0%",
    "87.5%"
};

static const int DUTY_COUNT = 7;

static int currentOctave = 4;
static int currentDuty   = 3;    // 50%
static int currentVolume = 100;

static int currentVoice = -1;
static int currentNote  = -1;

static bool playing = false;

// ------------------------------------------------------------
// Bottom screen bitmap
// ------------------------------------------------------------

static int subBg;
static u16* subGfx;

// ------------------------------------------------------------
// Color helper
// ------------------------------------------------------------

static inline u16 col(int r, int g, int b)
{
    return RGB15(r, g, b) | BIT(15);
}

// ------------------------------------------------------------
// Drawing primitives
// ------------------------------------------------------------

static void fillRect(int x, int y, int w, int h, u16 color)
{
    if (x < 0) {
        w += x;
        x = 0;
    }

    if (y < 0) {
        h += y;
        y = 0;
    }

    if (x + w > SCREEN_W)
        w = SCREEN_W - x;

    if (y + h > SCREEN_H)
        h = SCREEN_H - y;

    if (w <= 0 || h <= 0)
        return;

    for (int yy = y; yy < y + h; yy++) {
        for (int xx = x; xx < x + w; xx++) {
            subGfx[yy * 256 + xx] = color;
        }
    }
}

static void outlineRect(int x, int y, int w, int h, u16 color)
{
    fillRect(x, y, w, 1, color);
    fillRect(x, y + h - 1, w, 1, color);
    fillRect(x, y, 1, h, color);
    fillRect(x + w - 1, y, 1, h, color);
}

// ------------------------------------------------------------
// Tiny 5x7 font
// 下画面ラベル専用
// ------------------------------------------------------------

static const u8* glyph(char c)
{
    static const u8 A[7] = {
        0x0E,0x11,0x11,0x1F,0x11,0x11,0x11
    };

    static const u8 B[7] = {
        0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E
    };

    static const u8 C[7] = {
        0x0F,0x10,0x10,0x10,0x10,0x10,0x0F
    };

    static const u8 D[7] = {
        0x1E,0x11,0x11,0x11,0x11,0x11,0x1E
    };

    static const u8 E[7] = {
        0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F
    };

    static const u8 F[7] = {
        0x1F,0x10,0x10,0x1E,0x10,0x10,0x10
    };

    static const u8 G[7] = {
        0x0F,0x10,0x10,0x17,0x11,0x11,0x0F
    };

    static const u8 O[7] = {
        0x0E,0x11,0x11,0x11,0x11,0x11,0x0E
    };

    static const u8 T[7] = {
        0x1F,0x04,0x04,0x04,0x04,0x04,0x04
    };

    static const u8 U[7] = {
        0x11,0x11,0x11,0x11,0x11,0x11,0x0E
    };

    static const u8 Y[7] = {
        0x11,0x11,0x0A,0x04,0x04,0x04,0x04
    };

    static const u8 PLUS[7] = {
        0x00,0x04,0x04,0x1F,0x04,0x04,0x00
    };

    static const u8 MINUS[7] = {
        0x00,0x00,0x00,0x1F,0x00,0x00,0x00
    };

    switch (c) {
        case 'A': return A;
        case 'B': return B;
        case 'C': return C;
        case 'D': return D;
        case 'E': return E;
        case 'F': return F;
        case 'G': return G;
        case 'O': return O;
        case 'T': return T;
        case 'U': return U;
        case 'Y': return Y;
        case '+': return PLUS;
        case '-': return MINUS;
    }

    return nullptr;
}

static void drawChar5x7(
    int x,
    int y,
    char c,
    u16 color,
    int scale = 1
)
{
    const u8* g = glyph(c);

    if (!g)
        return;

    for (int row = 0; row < 7; row++) {

        for (int bit = 0; bit < 5; bit++) {

            if (g[row] & (1 << (4 - bit))) {

                fillRect(
                    x + bit * scale,
                    y + row * scale,
                    scale,
                    scale,
                    color
                );
            }
        }
    }
}

static void drawText5x7(
    int x,
    int y,
    const char* text,
    u16 color,
    int scale = 1
)
{
    while (*text) {

        drawChar5x7(x, y, *text, color, scale);

        x += 6 * scale;

        text++;
    }
}

// ------------------------------------------------------------
// Frequency
// ------------------------------------------------------------

static int getFrequency(int note)
{
    int f = baseFreq[note];

    if (currentOctave > 4) {

        for (int i = 0; i < currentOctave - 4; i++)
            f *= 2;
    }
    else if (currentOctave < 4) {

        for (int i = 0; i < 4 - currentOctave; i++)
            f /= 2;
    }

    return f;
}

static int displayOctave(int note)
{
    // 一番右のCは次のオクターブ
    if (note == 12)
        return currentOctave + 1;

    return currentOctave;
}

// ------------------------------------------------------------
// Sound
// ------------------------------------------------------------

static void stopNote()
{
    if (currentVoice >= 0) {

        soundKill(currentVoice);

        currentVoice = -1;
    }

    currentNote = -1;
    playing = false;
}

static void playNote(int note)
{
    if (currentVoice >= 0)
        soundKill(currentVoice);

    currentVoice = soundPlayPSG(
        dutyValues[currentDuty],
        getFrequency(note),
        currentVolume,
        64
    );

    currentNote = note;

    playing = true;
}

// ------------------------------------------------------------
// Top display
// ------------------------------------------------------------

static void printVolumeBar()
{
    int bars = (currentVolume * 16) / 127;

    iprintf("[");

    for (int i = 0; i < 16; i++) {

        if (i < bars)
            iprintf("#");
        else
            iprintf("-");
    }

    iprintf("]");
}

static void drawTopScreen()
{
    iprintf("\x1b[2J");
    iprintf("\x1b[0;0H");

    iprintf("+------------------------------+\n");
    iprintf("|        PICO SYNTH DS         |\n");
    iprintf("+------------------------------+\n");

    iprintf("\n");

    iprintf(" ENGINE    Nintendo DS PSG\n");
    iprintf(" WAVE      SQUARE / PULSE\n");

    iprintf("\n");

    if (playing && currentNote >= 0) {

        iprintf(" NOTE      %-2s%d\n",
            noteNames[currentNote],
            displayOctave(currentNote)
        );

        iprintf(" FREQUENCY %4d Hz\n",
            getFrequency(currentNote)
        );

        iprintf(" CHANNEL   %d\n",
            currentVoice
        );
    }
    else {

        iprintf(" NOTE      ---\n");
        iprintf(" FREQUENCY ---- Hz\n");
        iprintf(" CHANNEL   --\n");
    }

    iprintf("\n");

    iprintf(" OCTAVE    %d\n", currentOctave);

    iprintf(" DUTY      %s\n",
        dutyNames[currentDuty]
    );

    iprintf(" VOLUME    %3d ", currentVolume);

    printVolumeBar();

    iprintf("\n\n");

    iprintf(" TOUCH     Play keyboard\n");
    iprintf(" L / R     Octave\n");
    iprintf(" LEFT/RIGHT Duty\n");
    iprintf(" UP/DOWN    Volume\n");
}

// ------------------------------------------------------------
// Lower screen header
// ------------------------------------------------------------

static void drawHeader()
{
    u16 bg        = col(4, 5, 7);
    u16 button    = col(9, 11, 14);
    u16 line      = col(17, 20, 24);
    u16 text      = col(28, 29, 30);
    u16 highlight = col(8, 22, 26);

    fillRect(0, 0, 256, HEADER_H, bg);

    // OCT -
    fillRect(3, 3, 64, 26, button);
    outlineRect(3, 3, 64, 26, line);

    drawText5x7(
        15, 10,
        "OCT-",
        text,
        1
    );

    // DUTY
    fillRect(72, 3, 112, 26, highlight);
    outlineRect(72, 3, 112, 26, line);

    drawText5x7(
        112, 10,
        "DUTY",
        text,
        1
    );

    // OCT +
    fillRect(189, 3, 64, 26, button);
    outlineRect(189, 3, 64, 26, line);

    drawText5x7(
        201, 10,
        "OCT+",
        text,
        1
    );
}

// ------------------------------------------------------------
// Keyboard rendering
// ------------------------------------------------------------

static void drawKeyboard()
{
    u16 background = col(5, 6, 8);

    u16 whiteKey   = col(29, 29, 28);
    u16 whiteShade = col(24, 24, 23);

    u16 blackKey   = col(3, 3, 4);
    u16 blackTop   = col(7, 7, 9);

    u16 border     = col(7, 7, 8);

    u16 activeWhite = col(13, 27, 29);
    u16 activeBlack = col(22, 10, 5);

    u16 keyText = col(7, 7, 8);

    fillRect(
        0,
        HEADER_H,
        SCREEN_W,
        SCREEN_H - HEADER_H,
        background
    );

    // --------------------------------------------------------
    // White keys
    // --------------------------------------------------------

    for (int i = 0; i < WHITE_COUNT; i++) {

        int x = i * WHITE_KEY_W;

        int note = whiteSemitone[i];

        bool active =
            playing &&
            currentNote == note;

        // key shadow / lower shading
        fillRect(
            x,
            KEYBOARD_TOP,
            WHITE_KEY_W,
            WHITE_KEY_H,
            active ? activeWhite : whiteKey
        );

        fillRect(
            x + 2,
            SCREEN_H - 11,
            WHITE_KEY_W - 4,
            8,
            whiteShade
        );

        outlineRect(
            x,
            KEYBOARD_TOP,
            WHITE_KEY_W,
            WHITE_KEY_H,
            border
        );
    }

    // --------------------------------------------------------
    // White-key note labels
    // --------------------------------------------------------

    static const char whiteNames[] = {
        'C','D','E','F','G','A','B','C'
    };

    for (int i = 0; i < WHITE_COUNT; i++) {

        int x =
            i * WHITE_KEY_W +
            (WHITE_KEY_W / 2) -
            3;

        drawChar5x7(
            x,
            SCREEN_H - 27,
            whiteNames[i],
            keyText,
            1
        );
    }

    // --------------------------------------------------------
    // Black-key shadows
    // --------------------------------------------------------

    for (int i = 0; i < BLACK_COUNT; i++) {

        int bx =
            (blackGap[i] + 1) *
            WHITE_KEY_W -
            BLACK_KEY_W / 2;

        fillRect(
            bx + 2,
            KEYBOARD_TOP + 3,
            BLACK_KEY_W,
            BLACK_KEY_H,
            col(2,2,2)
        );
    }

    // --------------------------------------------------------
    // Black keys
    // --------------------------------------------------------

    for (int i = 0; i < BLACK_COUNT; i++) {

        int bx =
            (blackGap[i] + 1) *
            WHITE_KEY_W -
            BLACK_KEY_W / 2;

        int note =
            blackSemitone[i];

        bool active =
            playing &&
            currentNote == note;

        fillRect(
            bx,
            KEYBOARD_TOP,
            BLACK_KEY_W,
            BLACK_KEY_H,
            active ? activeBlack : blackKey
        );

        // highlight strip
        fillRect(
            bx + 2,
            KEYBOARD_TOP + 2,
            BLACK_KEY_W - 4,
            4,
            blackTop
        );

        outlineRect(
            bx,
            KEYBOARD_TOP,
            BLACK_KEY_W,
            BLACK_KEY_H,
            border
        );
    }
}

static void drawLowerScreen()
{
    drawHeader();
    drawKeyboard();
}

// ------------------------------------------------------------
// Touch keyboard detection
// ------------------------------------------------------------

static int touchToNote(int x, int y)
{
    if (y < KEYBOARD_TOP)
        return -1;

    // 黒鍵を最優先で判定
    if (y < KEYBOARD_TOP + BLACK_KEY_H) {

        for (int i = 0; i < BLACK_COUNT; i++) {

            int bx =
                (blackGap[i] + 1) *
                WHITE_KEY_W -
                BLACK_KEY_W / 2;

            if (
                x >= bx &&
                x < bx + BLACK_KEY_W
            ) {

                return blackSemitone[i];
            }
        }
    }

    // 白鍵
    int white =
        x / WHITE_KEY_W;

    if (white < 0)
        white = 0;

    if (white >= WHITE_COUNT)
        white = WHITE_COUNT - 1;

    return whiteSemitone[white];
}

// ------------------------------------------------------------
// Header touch buttons
// ------------------------------------------------------------

static bool headerTouch(int x, int y)
{
    if (y >= HEADER_H)
        return false;

    // OCT -
    if (x < 70) {

        if (currentOctave > 2)
            currentOctave--;

        return true;
    }

    // OCT +
    if (x > 186) {

        if (currentOctave < 7)
            currentOctave++;

        return true;
    }

    // DUTY cycle
    currentDuty++;

    if (currentDuty >= DUTY_COUNT)
        currentDuty = 0;

    return true;
}

// ============================================================
// MAIN
// ============================================================

int main(void)
{
  // --------------------------------------------------------
// Upper screen = text console
// --------------------------------------------------------

lcdMainOnTop();

videoSetMode(MODE_0_2D);
vramSetBankA(VRAM_A_MAIN_BG);

static PrintConsole topScreen;

consoleInit(
    &topScreen,
    3,
    BgType_Text4bpp,
    BgSize_T_256x256,
    31,
    0,
    true,   // MAIN display
    true
);

consoleSelect(&topScreen);

// --------------------------------------------------------
// Lower screen = 16-bit bitmap keyboard
// --------------------------------------------------------

videoSetModeSub(MODE_5_2D);
vramSetBankC(VRAM_C_SUB_BG);

subBg = bgInitSub(
    3,
    BgType_Bmp16,
    BgSize_B16_256x256,
    0,
    0
);

subGfx = (u16*)bgGetGfxPtr(subBg);
    // --------------------------------------------------------
    // Sound
    // --------------------------------------------------------

    soundEnable();

    // --------------------------------------------------------
    // First frame
    // --------------------------------------------------------

    drawTopScreen();

    drawLowerScreen();

    touchPosition touch;

    // --------------------------------------------------------
    // Main loop
    // --------------------------------------------------------

    while (pmMainLoop()) {

        swiWaitForVBlank();

        scanKeys();

        int down = keysDown();
        int held = keysHeld();
        int up   = keysUp();

        bool parameterChanged = false;

        // ----------------------------------------------------
        // Physical controls
        // ----------------------------------------------------

        if (down & KEY_L) {

            if (currentOctave > 2)
                currentOctave--;

            parameterChanged = true;
        }

        if (down & KEY_R) {

            if (currentOctave < 7)
                currentOctave++;

            parameterChanged = true;
        }

        if (down & KEY_LEFT) {

            currentDuty--;

            if (currentDuty < 0)
                currentDuty = DUTY_COUNT - 1;

            parameterChanged = true;
        }

        if (down & KEY_RIGHT) {

            currentDuty++;

            if (currentDuty >= DUTY_COUNT)
                currentDuty = 0;

            parameterChanged = true;
        }

        if (down & KEY_UP) {

            currentVolume += 8;

            if (currentVolume > 127)
                currentVolume = 127;

            parameterChanged = true;
        }

        if (down & KEY_DOWN) {

            currentVolume -= 8;

            if (currentVolume < 0)
                currentVolume = 0;

            parameterChanged = true;
        }

        // ----------------------------------------------------
        // First touch
        // ----------------------------------------------------

        if (down & KEY_TOUCH) {

            touchRead(&touch);

            if (touch.py < HEADER_H) {

                stopNote();

                if (headerTouch(
                    touch.px,
                    touch.py
                )) {

                    parameterChanged = true;
                }
            }
        }

        // ----------------------------------------------------
        // Keyboard touch
        // ----------------------------------------------------

        if (held & KEY_TOUCH) {

            touchRead(&touch);

            if (touch.py >= KEYBOARD_TOP) {

                int note =
                    touchToNote(
                        touch.px,
                        touch.py
                    );

                if (
                    note >= 0 &&
                    (
                        !playing ||
                        note != currentNote
                    )
                ) {

                    playNote(note);

                    drawTopScreen();
                    drawLowerScreen();
                }
            }
        }

        // ----------------------------------------------------
        // Touch release
        // ----------------------------------------------------

        if (up & KEY_TOUCH) {

            if (playing) {

                stopNote();

                drawTopScreen();
                drawLowerScreen();
            }
        }

        // ----------------------------------------------------
        // Parameter changes
        // ----------------------------------------------------

        if (parameterChanged) {

            if (
                playing &&
                currentNote >= 0
            ) {

                int note =
                    currentNote;

                playNote(note);
            }

            drawTopScreen();
            drawLowerScreen();
        }
    }

    return 0;
}
