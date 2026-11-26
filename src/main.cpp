#include <cstdio>
#include <cmath>
#include "inkfox_core.hpp"

using namespace inkfox;

// =========================================================
// 🎨 GeForce NOW ऑफिशियल कलर पैलेट (Official Color Palette)
// =========================================================
// NVIDIA Green (#76B900 -> RGB: 118, 185, 0 -> Normalized: 0.462, 0.725, 0.0)
const float GFN_GREEN_R = 0.462f; 
const float GFN_GREEN_G = 0.725f; 
const float GFN_GREEN_B = 0.0f;

// White Background
const float BG_WHITE_R = 1.0f;
const float BG_WHITE_G = 1.0f;
const float BG_WHITE_B = 1.0f;

// Dark Text (Black/Dark Grey)
const float TEXT_DARK_R = 0.1f;
const float TEXT_DARK_G = 0.1f;
const float TEXT_DARK_B = 0.1f;

// =========================================================
// 🎮 XInput स्ट्रक्चर (XSDK के लिए कस्टम)
// =========================================================
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

// ... (XInput बटन कोड्स और XInputGetState स्टब पहले जैसे ही रहेंगे) ...
#define XINPUT_GAMEPAD_DPAD_LEFT       0x0004
#define XINPUT_GAMEPAD_DPAD_RIGHT      0x0008
#define XINPUT_GAMEPAD_A              0x1000
#define XINPUT_GAMEPAD_B              0x2000
#define XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE  7849

unsigned long XInputGetState(unsigned long dwUserIndex, XINPUT_STATE* pState) {
    pState->Gamepad.wButtons = 0; 
    pState->Gamepad.sThumbLX = 0;
    return 0; 
}

// =========================================================
// 🖥️ UI ड्रॉइंग फंक्शन्स (यहाँ हम कलर और लेआउट सेट करेंगे)
// =========================================================
// नोट: यहाँ DrawRect और DrawText को आपके XSDK के हिसाब से बदलना होगा
void DrawRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    // आपका XSDK GPU ड्रॉ कॉल यहाँ आएगा
}

void DrawText(const char* text, float x, float y, float r, float g, float b) {
    // आपका XSDK टेक्स्ट रेंडरिंग कोड यहाँ आएगा
}

void DrawImage(int textureID, float x, float y, float w, float h) {
    // आपका XSDK टेक्सचर ड्रॉ कॉल यहाँ आएगा
}

int main() {
    // 1. स्क्रीन क्लियर करें (सफेद बैकग्राउंड)
    // xsdk_gpu_clear(BG_WHITE_R, BG_WHITE_G, BG_WHITE_B, 1.0f); 

    // 2. टॉप हेडर (Top Bar) - सफेद बैकग्राउंड, बाएं GFN लोगो, दाएं यूजर प्रोफाइल
    DrawRect(0, 0, 1920, 100, BG_WHITE_R, BG_WHITE_G, BG_WHITE_B, 1.0f); 
    
    // लोगो ड्रॉ करें (अगर आपके पास GFN लोगो टेक्सचर है)
    // DrawImage(gfnLogoTexture, 30, 20, 200, 60); 

    // 3. मेन कंटेंट एरिया
    // सर्च बार (Search Bar)
    DrawRect(400, 20, 1000, 50, 0.9f, 0.9f, 0.9f, 1.0f); // हल्का ग्रे
    DrawText("Search games...", 420, 35, TEXT_DARK_R, TEXT_DARK_G, TEXT_DARK_B);

    // गेम कार्ड्स (Game Cards) - बड़े कार्ड्स, राउंडेड कॉर्नर
    float startX = 200;
    float startY = 200;
    float cardW = 300;
    float cardH = 400;
    float gap = 40;

    for (int i = 0; i < 4; i++) {
        float x = startX + i * (cardW + gap);
        
        // कार्ड का बैकग्राउंड (सफेद या हल्का ग्रे)
        DrawRect(x, startY, cardW, cardH, 0.95f, 0.95f, 0.95f, 1.0f);
        
        // गेम का टाइटल (कार्ड के नीचे)
        DrawText("Cyberpunk 2077", x + 10, startY + cardH + 10, TEXT_DARK_R, TEXT_DARK_G, TEXT_DARK_B);
        
        // "Play" बटन (हरा रंग - NVIDIA Green)
        DrawRect(x + 50, startY + cardH + 50, 200, 50, GFN_GREEN_R, GFN_GREEN_G, GFN_GREEN_B, 1.0f);
        DrawText("PLAY", x + 120, startY + cardH + 65, 1.0f, 1.0f, 1.0f); // व्हाइट टेक्स्ट
    }

    // 4. गेम लूप (Game Loop)
    float dt = 1.f / 60;
    XINPUT_STATE state;
    
    while (true) { // इसे अपने गेम लूप से रिप्लेस करें
        XInputGetState(0, &state);
        
        // D-Pad Left/Right से गेम सेलेक्ट करें
        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) {
            // पिछला गेम सेलेक्ट करें
        }
        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) {
            // अगला गेम सेलेक्ट करें
        }
        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) {
            // गेम लॉन्च करें (फिलहाल सिर्फ एक नोटिस दिखाएं)
        }
        if (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) {
            break; // ऐप से बाहर निकलें
        }

        // यहाँ अपना रेंडरिंग लूप चलाएं
        // g.tick(dt); 
    }

    return 0;
}
