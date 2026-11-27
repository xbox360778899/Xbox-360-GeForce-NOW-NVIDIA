#include "gfx.h"
#include "auth.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <curl/curl.h>
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "geforce_now_ui_shaders.h"
#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/present.h"
#include "xsdk/shader.h"
#include "xsdk/text.h"
#include "xsdk/texture.h"

namespace {
constexpr uint32_t kBuffers = 2;
constexpr uint32_t kBeginTimeoutMs = 5000;
constexpr uint32_t kFenceTimeoutMs = 1000;
constexpr uint32_t kMaxVertices = 4096;
constexpr uint32_t kVertexFloats = 7;
constexpr uint32_t kVertexBytes = kVertexFloats * sizeof(float);
constexpr size_t kMaxImageBytes = 1024 * 1024;

struct Theme {
    uint32_t background;
    uint32_t panel;
    uint32_t headerBg;
    uint32_t textMain;
    uint32_t textMuted;
    uint32_t border;
};
Theme lightTheme = { 0xFFF5F5F5, 0xFFFFFFFF, 0xFFFFFFFF, 0xFF000000, 0xFF555555, 0xFFCCCCCC };
Theme darkTheme  = { 0xFF121212, 0xFF1E1E1E, 0xFF181818, 0xFFFFFFFF, 0xFFAAAAAA, 0xFF333333 };

bool isDarkMode = false;
Theme currentTheme = lightTheme;
const uint32_t kGreen = 0xFF76B900;

enum class Tab { HOME, LIBRARY, SETTINGS };
Tab currentTab = Tab::HOME;
int settingsSelection = 0;
bool vibrationEnabled = true;

struct Vertex { float x, y, r, g, b, u, v; };
static_assert(sizeof(Vertex) == kVertexBytes);
Vertex cpuVertices[kMaxVertices]{};
uint32_t packedVertices[kMaxVertices * kVertexFloats]{};
uint32_t vertexCount = 0;
volatile uint32_t* gpuVertices = nullptr;
volatile uint32_t* gpuCovers = nullptr;
XsdkShader vertexShader{};
XsdkShader pixelShader{};
uint32_t screenWidth = 1280;
uint32_t screenHeight = 720;

struct Game { const char* title; const char* category; const char* url; XsdkTexture texture; bool loaded; };
Game games[] = {
    {"Cyberpunk 2077", "ACTION RPG", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1091500/header.jpg", {}, false},
    {"Apex Legends", "BATTLE ROYALE", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1172470/header.jpg", {}, false},
    {"The Witcher 3", "OPEN-WORLD RPG", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/292030/header.jpg", {}, false},
    {"Dota 2", "MOBA", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/570/header.jpg", {}, false},
    {"GTA V", "OPEN WORLD", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/271590/header.jpg", {}, false},
    {"Elden Ring", "ACTION RPG", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1245620/header.jpg", {}, false},
    {"Red Dead Redemption 2", "ACTION ADV", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1174180/header.jpg", {}, false},
    {"Counter-Strike 2", "FPS", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/730/header.jpg", {}, false},
    {"Fall Guys", "PARTY", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1097150/header.jpg", {}, false},
    {"Baldur's Gate 3", "RPG", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1086940/header.jpg", {}, false},
    {"Overwatch 2", "FPS", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/2357570/header.jpg", {}, false},
    {"Terraria", "SANDBOX", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/105600/header.jpg", {}, false},
    {"SMITE", "MOBA", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/386360/header.jpg", {}, false},
    {"PUBG", "BATTLE ROYALE", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/578080/header.jpg", {}, false},
    {"Destiny 2", "FPS", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1085660/header.jpg", {}, false},
    {"Rust", "SURVIVAL", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/252490/header.jpg", {}, false},
    {"Warframe", "ACTION RPG", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/230410/header.jpg", {}, false},
    {"Rainbow Six Siege", "TACTICAL FPS", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/359550/header.jpg", {}, false},
    {"Titanfall 2", "FPS", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1237970/header.jpg", {}, false},
    {"Rocket League", "SPORTS", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/252950/header.jpg", {}, false}
};
constexpr uint32_t kGameCount = sizeof(games) / sizeof(games[0]);
constexpr uint32_t kColumns = 4;
constexpr float cardW = 286.0f;
constexpr float cardH = 117.0f;
constexpr float gapX = 20.0f;
constexpr float rowH = 160.0f;

struct DownloadBuffer { unsigned char* data; size_t size; size_t capacity; bool overflow; };

size_t receiveBytes(char* ptr, size_t size, size_t count, void* context) {
  DownloadBuffer* out = static_cast<DownloadBuffer*>(context);
  const size_t bytes = size * count;
  if (!bytes || bytes > kMaxImageBytes - out->size) { out->overflow = true; return 0; }
  const size_t required = out->size + bytes;
  if (required > out->capacity) {
    size_t next = out->capacity ? out->capacity : 32768;
    while (next < required) next *= 2;
    if (next > kMaxImageBytes) next = kMaxImageBytes;
    unsigned char* resized = static_cast<unsigned char*>(realloc(out->data, next));
    if (!resized) return 0;
    out->data = resized;
    out->capacity = next;
  }
  memcpy(out->data + out->size, ptr, bytes);
  out->size += bytes;
  return bytes;
}

bool loadGameArt(Game& game) {
  DownloadBuffer data{};
  CURL* curl = curl_easy_init();
  if (!curl) return false;
  curl_easy_setopt(curl, CURLOPT_URL, game.url);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receiveBytes);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
  curl_easy_setopt(curl, CURLOPT_CAINFO, "game:\\cacert.pem");
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 12L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 22L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  const CURLcode result = curl_easy_perform(curl);
  long response = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
  curl_easy_cleanup(curl);
  if (result != CURLE_OK || response < 200 || response >= 300 || !data.size) {
    free(data.data);
    return false;
  }

  int width = 0, height = 0, channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(data.data, static_cast<int>(data.size), &width, &height, &channels, 4);
  free(data.data);
  if (!pixels || width <= 0 || height <= 0 || width > 1024 || height > 1024) {
    if (pixels) stbi_image_free(pixels);
    return false;
  }
  NTSTATUS status = xsdk_texture_create(&game.texture, XSDK_FORMAT_R8G8B8A8,
                                        static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1, false);
  if (NT_SUCCESS(status)) {
    xsdk_texture_upload(&game.texture, 0, pixels, static_cast<uint32_t>(width) * 4);
    game.loaded = true;
  }
  stbi_image_free(pixels);
  return game.loaded;
}

void manageMemory(float scrollY, uint32_t selectedGame) {
    for (uint32_t i = 0; i < kGameCount; ++i) {
        int r = i / kColumns;
        float y = 403 + r * rowH - scrollY;
        
        bool isNearScreen = (y + cardH > 100 && y < 800); 
        bool isHero = (i == selectedGame);
        
        if (isNearScreen || isHero) {
            if (!games[i].loaded) {
                loadGameArt(games[i]);
            }
        } else {
            if (games[i].loaded) {
                xsdk_texture_free(&games[i].texture);
                games[i].loaded = false;
                games[i].texture = {}; 
            }
        }
    }
}

float channel(uint32_t color, unsigned shift) { return static_cast<float>((color >> shift) & 0xFF) / 255.0f; }

void vertex(float x, float y, uint32_t color, float u = -1.0f, float v = -1.0f) {
  if (vertexCount >= kMaxVertices) return;
  const float clipX = 2.0f * x / static_cast<float>(screenWidth) - 1.0f;
  const float clipY = 1.0f - 2.0f * y / static_cast<float>(screenHeight);
  cpuVertices[vertexCount++] = {clipX, clipY, channel(color, 16), channel(color, 8), channel(color, 0), u, v};
}

void triangle(float x1, float y1, float x2, float y2, float x3, float y3, uint32_t color) {
  vertex(x1, y1, color); vertex(x2, y2, color); vertex(x3, y3, color);
}

void rect(float x, float y, float w, float h, uint32_t color) {
  if (w <= 0 || h <= 0) return;
  triangle(x, y, x + w, y, x + w, y + h, color);
  triangle(x, y, x + w, y + h, x, y + h, color);
}

void outlinedRect(float x, float y, float w, float h, float thickness, uint32_t color) {
  rect(x, y, w, thickness, color);
  rect(x, y + h - thickness, w, thickness, color);
  rect(x, y + thickness, thickness, h - 2 * thickness, color);
  rect(x + w - thickness, y + thickness, thickness, h - 2 * thickness, color);
}

void buildScene(uint32_t selectedGame, float gridScrollY) {
  vertexCount = 0;
  rect(0, 0, static_cast<float>(screenWidth), static_cast<float>(screenHeight), currentTheme.background);
  rect(0, 0, static_cast<float>(screenWidth), 76, currentTheme.headerBg);
  rect(0, 74, static_cast<float>(screenWidth), 2, currentTheme.border);
  
  rect(38, 29, 18, 18, kGreen);
  rect(43, 34, 8, 8, currentTheme.headerBg);
  rect(43, 54, 8, 3, kGreen);

  if (currentTab == Tab::HOME) {
    rect(38, 98, 1204, 242, currentTheme.panel);
    rect(38, 98, 570, 242, currentTheme.panel);
    rect(38, 98, 4, 242, kGreen);
    rect(38, 352, 1204, 1, currentTheme.border);

    for (uint32_t i = 0; i < kGameCount; ++i) {
      int r = i / kColumns;
      int c = i % kColumns;
      float x = 38 + c * (cardW + gapX);
      float y = 403 + r * rowH - gridScrollY;

      if (y + cardH < 400 || y > 620) continue;

      rect(x, y, cardW, cardH, currentTheme.panel);
      if (i == selectedGame) {
        outlinedRect(x - 2, y - 2, cardW + 4, cardH + 4, 3, kGreen);
      } else {
        outlinedRect(x, y, cardW, cardH, 1, currentTheme.border);
      }
    }
  } else if (currentTab == Tab::SETTINGS) {
    rect(200, 120, 880, 500, currentTheme.panel);
    outlinedRect(200, 120, 880, 500, 2, currentTheme.border);
    for(int i=0; i<3; i++) {
        if (i == settingsSelection) {
            rect(210, 150 + (i*80), 860, 60, 0x2276B900);
            rect(210, 150 + (i*80), 4, 60, kGreen);
        }
    }
  }
}

void uploadScene() {
  const uint32_t words = vertexCount * kVertexFloats;
  memcpy(packedVertices, cpuVertices, words * sizeof(uint32_t));
  for (uint32_t i = 0; i < words; ++i) gpuVertices[i] = packedVertices[i];
  xsdk_gpu_invalidate(xsdk_gpu_address(gpuVertices), words * sizeof(uint32_t));
}

void drawText(uint32_t selectedGame, float gridScrollY, bool notice) {
  xsdk_text_draw(72, 27, 1, currentTheme.textMain, "GEFORCE NOW");
  xsdk_text_draw(304, 28, 1, currentTab == Tab::HOME ? kGreen : currentTheme.textMuted, "GAMES");
  xsdk_text_draw(397, 28, 1, currentTab == Tab::LIBRARY ? kGreen : currentTheme.textMuted, "MY LIBRARY");
  xsdk_text_draw(540, 28, 1, currentTab == Tab::SETTINGS ? kGreen : currentTheme.textMuted, "SETTINGS");
  
  if (currentTab == Tab::HOME) {
    xsdk_text_draw(68, 122, 1, kGreen, "FEATURED GAME");
    xsdk_text_draw(68, 158, 2, currentTheme.textMain, games[selectedGame].title);
    xsdk_text_draw(68, 206, 1, currentTheme.textMuted, games[selectedGame].category);
    xsdk_text_draw(68, 233, 1, currentTheme.textMuted, "STREAMING READY");
    
    xsdk_text_draw(38, 367, 1, currentTheme.textMain, "ALL GAMES");
    xsdk_text_draw(1090, 367, 1, currentTheme.textMuted, "VIEW ALL  >");
    
    for (uint32_t i = 0; i < kGameCount; ++i) {
      int r = i / kColumns;
      int c = i % kColumns;
      float x = 38 + c * (cardW + gapX);
      float y = 403 + r * rowH - gridScrollY;

      if (y + cardH < 400 || y > 620) continue;

      xsdk_text_draw(x + 4, y + cardH + 6, 1, i == selectedGame ? kGreen : currentTheme.textMain, games[i].title);
      xsdk_text_draw(x + 4, y + cardH + 25, 1, currentTheme.textMuted, games[i].category);
    }

    if (notice) xsdk_text_draw(38, 668, 1, kGreen, "GAME STREAMING IS NOT IMPLEMENTED IN THIS DEMO");
    else xsdk_text_draw(38, 668, 1, currentTheme.textMuted, "LB / RB  CHANGE TAB     D-PAD  SCROLL     A  SELECT     B  EXIT");

  } else if (currentTab == Tab::LIBRARY) {
    xsdk_text_draw(38, 150, 2, currentTheme.textMain, "MY LIBRARY");
    xsdk_text_draw(38, 200, 1, currentTheme.textMuted, "YOU HAVE NO RECENTLY PLAYED GAMES.");
    xsdk_text_draw(38, 668, 1, currentTheme.textMuted, "LB / RB  CHANGE TAB       B  EXIT");

  } else if (currentTab == Tab::SETTINGS) {
    xsdk_text_draw(230, 130, 2, currentTheme.textMain, "SETTINGS");
    
    xsdk_text_draw(240, 170, 1, settingsSelection == 0 ? kGreen : currentTheme.textMain, "APP THEME");
    xsdk_text_draw(900, 170, 1, currentTheme.textMuted, isDarkMode ? "DARK MODE" : "LIGHT MODE");

    xsdk_text_draw(240, 250, 1, settingsSelection == 1 ? kGreen : currentTheme.textMain, "CONTROLLER VIBRATION");
    xsdk_text_draw(900, 250, 1, currentTheme.textMuted, vibrationEnabled ? "ON" : "OFF");

    xsdk_text_draw(240, 330, 1, settingsSelection == 2 ? kGreen : currentTheme.textMain, "STREAMING QUALITY");
    xsdk_text_draw(900, 330, 1, currentTheme.textMuted, "1080P 60FPS");

    xsdk_text_draw(230, 580, 1, currentTheme.textMuted, "D-PAD UP/DOWN  NAVIGATE       A  TOGGLE       B  BACK");
  }
}
}  // namespace

int gfx_run(void) {
  const CURLcode curlInit = curl_global_init(CURL_GLOBAL_DEFAULT);
  NTSTATUS status = xsdk_gpu_open();
  if (!NT_SUCCESS(status)) { curl_global_cleanup(); return 1; }
  
  status = xsdk_shader_load(&vertexShader, ui_vert.container, ui_vert.container_bytes);
  if (NT_SUCCESS(status)) status = xsdk_shader_load(&pixelShader, ui_frag.container, ui_frag.container_bytes);
  
  const XsdkVertexElement layout[] = {
      {XSDK_USAGE_POSITION, 0, XSDK_VERTEX_FLOAT2, 0, 0, {0, 0, 0, 0}, false},
      {XSDK_USAGE_COLOR, 0, XSDK_VERTEX_FLOAT3, 0, 8, {0, 0, 0, 0}, false},
      {XSDK_USAGE_TEXCOORD, 0, XSDK_VERTEX_FLOAT2, 0, 20, {0, 0, 0, 0}, false},
  };
  const uint32_t stride = kVertexBytes;
  if (NT_SUCCESS(status)) status = xsdk_shader_set_vertex_layout(&vertexShader, layout, 3, &stride);
  
  if (!NT_SUCCESS(status)) { xsdk_gpu_close(); curl_global_cleanup(); return 1; }
  status = xsdk_text_open();
  
  gpuVertices = xsdk_gpu_alloc(kMaxVertices * kVertexBytes);
  gpuCovers = xsdk_gpu_alloc(kMaxVertices * kVertexBytes);
  
  status = xsdk_present_open(XSDK_DISPLAY_720P, kBuffers, 1, &screenWidth, &screenHeight);

  XsdkSampler sampler{};
  sampler.mag_filter = XSDK_FILTER_LINEAR;
  sampler.min_filter = XSDK_FILTER_LINEAR;
  sampler.mip_filter = XSDK_FILTER_POINT;
  sampler.address_u = XSDK_ADDRESS_CLAMP;
  sampler.address_v = XSDK_ADDRESS_CLAMP;
  sampler.address_w = XSDK_ADDRESS_CLAMP;
  sampler.level_count = 1;
  XsdkSamplerState samplerState{};
  xsdk_texture_sampler_state(&samplerState, &sampler);

  uint32_t selectedGame = 0;
  float gridScrollY = 0.0f;
  bool notice = false;
  uint16_t previousButtons = 0;
  AuthTokens authTokens{};
  bool authenticated = false;
  bool needCoverUpdate = true; 

  manageMemory(gridScrollY, selectedGame);

  buildScene(selectedGame, gridScrollY);
  uploadScene();
  
  for (;;) {
    INPUT_STATE input{};
    if (XamInputGetState(0, 0, &input) != ERROR_SUCCESS) input = {};
    const uint16_t buttons = input.gamepad.buttons;
    const uint16_t pressed = buttons & static_cast<uint16_t>(~previousButtons);
    previousButtons = buttons;
    
    if (pressed & INPUT_GAMEPAD_START) {
      if (curlInit != CURLE_OK) {
        xsdk_log_write("auth: libcurl initialization failed; sign-in unavailable");
      } else if (auth_device_login(&authTokens)) {
        authenticated = true;
        xsdk_log_write("auth: sign-in token is ready in memory for the next networking step");
      } else {
        authenticated = false;
        xsdk_log_write("auth: sign-in failed: %s", auth_last_error());
      }
    }

    if (pressed & INPUT_GAMEPAD_B) {
        if (currentTab != Tab::HOME) {
            currentTab = Tab::HOME;
            needCoverUpdate = true;
        } else {
            break;
        }
    }

    if (pressed & INPUT_GAMEPAD_LEFT_SHOULDER) {
        if (currentTab == Tab::SETTINGS) currentTab = Tab::LIBRARY;
        else if (currentTab == Tab::LIBRARY) currentTab = Tab::HOME;
        needCoverUpdate = true;
    }
    if (pressed & INPUT_GAMEPAD_RIGHT_SHOULDER) {
        if (currentTab == Tab::HOME) currentTab = Tab::LIBRARY;
        else if (currentTab == Tab::LIBRARY) currentTab = Tab::SETTINGS;
        needCoverUpdate = true;
    }
    
    if (currentTab == Tab::HOME) {
        bool moved = false;
        if (pressed & INPUT_GAMEPAD_DPAD_RIGHT) { selectedGame = (selectedGame + 1) % kGameCount; moved = true; }
        if (pressed & INPUT_GAMEPAD_DPAD_LEFT) { selectedGame = (selectedGame == 0) ? kGameCount - 1 : selectedGame - 1; moved = true; }
        if (pressed & INPUT_GAMEPAD_DPAD_DOWN) {
            if (selectedGame + kColumns < kGameCount) { selectedGame += kColumns; moved = true; }
        }
        if (pressed & INPUT_GAMEPAD_DPAD_UP) {
            if (selectedGame >= kColumns) { selectedGame -= kColumns; moved = true; }
        }
        
        if (moved) {
            notice = false;
            
            int row = selectedGame / kColumns;
            float targetY = 403 + row * rowH;
            float gridTop = 400;
            float gridBottom = 620;

            if (targetY < gridTop + gridScrollY) gridScrollY = targetY - gridTop;
            if (targetY + cardH > gridBottom + gridScrollY) gridScrollY = targetY + cardH - gridBottom;
            
            float maxScroll = (kGameCount / kColumns) * rowH + cardH - (gridBottom - gridTop);
            if (gridScrollY < 0) gridScrollY = 0;
            if (gridScrollY > maxScroll) gridScrollY = maxScroll;
            
            manageMemory(gridScrollY, selectedGame);
            needCoverUpdate = true;
        }
        if (pressed & INPUT_GAMEPAD_A) notice = true;
    } 
    else if (currentTab == Tab::SETTINGS) {
        if (pressed & INPUT_GAMEPAD_DPAD_DOWN) settingsSelection = (settingsSelection + 1) % 3;
        if (pressed & INPUT_GAMEPAD_DPAD_UP) settingsSelection = (settingsSelection == 0) ? 2 : settingsSelection - 1;
        if (pressed & INPUT_GAMEPAD_A) {
            if (settingsSelection == 0) {
                isDarkMode = !isDarkMode;
                currentTheme = isDarkMode ? darkTheme : lightTheme;
            } else if (settingsSelection == 1) {
                vibrationEnabled = !vibrationEnabled;
            }
        }
    }

    buildScene(selectedGame, gridScrollY);
    uploadScene();

    uint32_t address = 0;
    status = xsdk_present_begin(&address, kBeginTimeoutMs);
    if (!NT_SUCCESS(status)) break;
    
    xsdk_gpu_set_target(screenWidth, screenHeight);
    xsdk_gpu_set_shaders(&vertexShader, &pixelShader);
    xsdk_built_shader_set_literals(&ui_vert);
    xsdk_built_shader_set_literals(&ui_frag);
    
    xsdk_gpu_set_vertices(0, xsdk_gpu_address(gpuVertices), vertexCount * kVertexBytes);
    xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, vertexCount);
    
    if (currentTab == Tab::HOME) {
      static Vertex coverCpu[64 * 6];
      static uint32_t coverPacked[64 * 6 * kVertexFloats];
      
      if (needCoverUpdate) {
        uint32_t coverCount = 0;
        auto appendQuad = [&](uint32_t idx, float x, float y, float w, float h) {
          if (!games[idx].loaded) return;
          const float l = 2.0f * x / screenWidth - 1.0f, r = 2.0f * (x + w) / screenWidth - 1.0f;
          const float t = 1.0f - 2.0f * y / screenHeight, b = 1.0f - 2.0f * (y + h) / screenHeight;
          const Vertex q[] = {{l,t,1,1,1,0,0},{r,t,1,1,1,1,0},{r,b,1,1,1,1,1},{l,t,1,1,1,0,0},{r,b,1,1,1,1,1},{l,b,1,1,1,0,1}};
          for (unsigned n=0;n<6;++n) coverCpu[coverCount++]=q[n];
        };
        
        appendQuad(selectedGame, 608, 98, 634, 242);
        
        for (uint32_t i = 0; i < kGameCount; ++i) {
          int r = i / kColumns;
          int c = i % kColumns;
          float x = 38 + c * (cardW + gapX);
          float y = 403 + r * rowH - gridScrollY;

          if (y + cardH < 400 || y > 620) continue;
          appendQuad(i, x, y, cardW, cardH);
        }
        
                if (coverCount > 0) {
          memcpy(coverPacked, coverCpu, coverCount * kVertexBytes);
          for (uint32_t n=0; n<coverCount*kVertexFloats; ++n) {
            gpuCovers[n] = coverPacked[n];
          }
          xsdk_gpu_invalidate(xsdk_gpu_address(gpuCovers), coverCount*kVertexBytes);
        }
        needCoverUpdate = false;
      }
      
      if (games[selectedGame].loaded) {
          xsdk_gpu_set_vertices(0, xsdk_gpu_address(gpuCovers), 6 * kVertexBytes);
          xsdk_gpu_set_texture(0, &games[selectedGame].texture, &samplerState);
          xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, 6);
      }
      
      uint32_t offset = 6;
      for (uint32_t i = 0; i < kGameCount; ++i) {
          int r = i / kColumns;
          int c = i % kColumns;
          float y = 403 + r * rowH - gridScrollY;

          if (y + cardH < 400 || y > 620) continue;

          if (games[i].loaded) {
              xsdk_gpu_set_vertices(0, xsdk_gpu_address(gpuCovers) + offset * kVertexBytes, 6 * kVertexBytes);
              xsdk_gpu_set_texture(0, &games[i].texture, &samplerState);
              xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, 6);
              offset += 6;
          }
      }
    }
    
    drawText(selectedGame, gridScrollY, notice);
    xsdk_gpu_resolve(address, screenWidth, screenHeight, currentTheme.background);
    xsdk_present_end();
    
    xsdk_gpu_wait(xsdk_gpu_fence(), kFenceTimeoutMs); 
  }

  xsdk_text_close();
  xsdk_present_close();
  xsdk_gpu_free(gpuVertices);
  if (gpuCovers) { xsdk_gpu_free(gpuCovers); gpuCovers = nullptr; }
  for (uint32_t i = 0; i < kGameCount; ++i) if (games[i].loaded) xsdk_texture_free(&games[i].texture);
  xsdk_shader_free(&pixelShader);
  xsdk_shader_free(&vertexShader);
  xsdk_gpu_close();
  if (authenticated) auth_clear_tokens(&authTokens);
  curl_global_cleanup();
  return 0;
}
