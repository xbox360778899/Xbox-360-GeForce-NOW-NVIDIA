#include <iostream>
#include <vector>
#include <string>
#include <cmath>

// XInput Custom Structures (XSDK ke liye)
struct XINPUT_GAMEPAD {
    unsigned short wButtons;
    unsigned char  bLeftTrigger;
    unsigned char  bRightTrigger;
    short          sThumbLX;
    short          sThumbLY;
    short          sThumbRX;
    short          sThumbRY;
};

struct XINPUT_STATE {
    unsigned long  dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
};

#define XINPUT_GAMEPAD_DPAD_LEFT       0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT      0x0008
#define XINPUT_GAMEPAD_A               0x1000
#define XINPUT_GAMEPAD_B               0x2000
#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE  7849

// Dummy XInput Function (Jab tak XSDK ka asli function nahi lagta)
unsigned long XInputGetState(unsigned long dwUserIndex, XINPUT_STATE* pState) {
    pState->Gamepad.wButtons = 0; 
    pState->Gamepad.sThumbLX = 0;
    return 0; 
}

// =========================================================
// 🎨 NVIDIA GeForce NOW Official Colors
// =========================================================
const float GFN_GREEN_R = 0.462f; 
const float GFN_GREEN_G = 0.725f; 
const float GFN_GREEN_B = 0.0f;

const float BG_WHITE_R = 1.0f;
const float BG_WHITE_G = 1.0f;
const float BG_WHITE_B = 1.0f;

const float TEXT_DARK_R = 0.1f;
const float TEXT_DARK_G = 0.1f;
const float TEXT_DARK_B = 0.1f;

// =========================================================
// 🖥️ Draw Functions (Inhe aapke XSDK ke saath link karna hoga)
// =========================================================
void DrawRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    // XSDK GPU Draw Call yahan aayega
}

void DrawText(const char* text, float x, float y, float r, float g, float b) {
    // XSDK Text Rendering yahan aayega
}

// =========================================================
// 🎮 Game Library Data
// =========================================================
struct Game {
    std::string title;
    int textureID; 
    bool isLoaded;
};

std::vector<Game> gameLibrary = {
    {"Cyberpunk 2077", -1, false},
    {"Apex Legends", -1, false},
    {"The Witcher 3", -1, false},
    {"Dota 2", -1, false},
    {"Fortnite", -1, false},
    {"Genshin Impact", -1, false},
    {"Valorant", -1, false},
    {"Minecraft", -1, false},
    {"Elden Ring", -1, false},
    {"GTA V", -1, false}
};

int main() {
    int currentSelection = 0;
    float cardW = 300;
    float cardH = 400;
    float gap = 40;
    float startY = 200;
    float startX = 200; 
    int visibleGames = 3; 

    while (true) {
        // 1. Input Padhein
        XINPUT_STATE state;
        XInputGetState(0, &state);

        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) {
            if (currentSelection < gameLibrary.size() - 1) currentSelection++;
        }
        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) {
            if (currentSelection > 0) currentSelection--;
        }
        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) {
            break; // Exit
        }

        // 2. Clear Screen (White Background)
        // xsdk_gpu_clear(BG_WHITE_R, BG_WHITE_G, BG_WHITE_B, 1.0f); 

        // 3. Scroll Logic
        int startIndex = 0;
        if (currentSelection >= visibleGames) {
            startIndex = currentSelection - (visibleGames - 1);
        }

        // 4. Draw UI
        for (int i = 0; i < visibleGames; i++) {
            int index = startIndex + i;
            if (index >= gameLibrary.size()) break;

            float x = startX + i * (cardW + gap);
            Game& g = gameLibrary[index];

            // Selected Game Border (Green)
            if (index == currentSelection) {
                DrawRect(x - 5, startY - 5, cardW + 10, cardH + 10, GFN_GREEN_R, GFN_GREEN_G, GFN_GREEN_B, 1.0f);
            }

            // Game Card Background
            DrawRect(x, startY, cardW, cardH, 0.95f, 0.95f, 0.95f, 1.0f);
            
            // Game Title
            DrawText(g.title.c_str(), x + 10, startY + cardH - 40, TEXT_DARK_R, TEXT_DARK_G, TEXT_DARK_B);
            
            // Play Button
            if (index == currentSelection) {
                DrawRect(x + 50, startY + cardH + 20, 200, 50, GFN_GREEN_R, GFN_GREEN_G, GFN_GREEN_B, 1.0f);
                DrawText("PLAY", x + 120, startY + cardH + 35, 1.0f, 1.0f, 1.0f);
            }
        }

        // (Yahan aapka Frame Present / VBlank wait hoga)
    }

    return 0;
}
