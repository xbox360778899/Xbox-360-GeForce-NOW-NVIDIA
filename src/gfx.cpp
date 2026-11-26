#include "gfx.h"

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
constexpr uint32_t kMaxVertices = 2048;
constexpr uint32_t kVertexFloats = 7;
constexpr uint32_t kVertexBytes = kVertexFloats * sizeof(float);
constexpr uint32_t kBackground = 0xFF080B0E;
constexpr uint32_t kPanel = 0xFF10161B;
constexpr uint32_t kGreen = 0xFF76B900;
constexpr uint32_t kWhite = 0xFFF3F6F8;
constexpr uint32_t kMuted = 0xFFA8B2BA;
constexpr uint32_t kDim = 0xFF66727C;
constexpr size_t kMaxImageBytes = 1024 * 1024;

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
    {"Cyberpunk 2077", "ACTION RPG", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1091500/48a93d6445a67520ea49833c93dae8f2e68fc540/header_alt_assets_9.jpg?t=1794474878", {}, false},
    {"Apex Legends", "BATTLE ROYALE", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/1172470/4219c147f1e094297c726d537518babba7f3ae2d/header.jpg?t=1794958367", {}, false},
    {"The Witcher 3: Wild Hunt", "OPEN-WORLD RPG", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/292030/4b5b1ba5061b8a992cc69ac58b4245434edb4296/header.jpg?t=1793900434", {}, false},
    {"Dota 2", "MOBA", "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/570/header.jpg?t=1791407503", {}, false},
};
constexpr uint32_t kGameCount = sizeof(games) / sizeof(games[0]);

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
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Xbox360-GFN-UI/0.2 (homebrew artwork preview)");
  const CURLcode result = curl_easy_perform(curl);
  long response = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response);
  curl_easy_cleanup(curl);
  if (result != CURLE_OK || response < 200 || response >= 300 || !data.size) {
    xsdk_log_write("art: %s download failed (curl=%d HTTP=%ld overflow=%d)", game.title,
                   static_cast<int>(result), response, data.overflow ? 1 : 0);
    free(data.data);
    return false;
  }

  int width = 0, height = 0, channels = 0;
  stbi_uc* pixels = stbi_load_from_memory(data.data, static_cast<int>(data.size), &width, &height, &channels, 4);
  free(data.data);
  if (!pixels || width <= 0 || height <= 0 || width > 1024 || height > 1024) {
    xsdk_log_write("art: %s JPEG decode failed", game.title);
    if (pixels) stbi_image_free(pixels);
    return false;
  }
  NTSTATUS status = xsdk_texture_create(&game.texture, XSDK_FORMAT_R8G8B8A8,
                                        static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1, false);
  if (NT_SUCCESS(status)) {
    xsdk_texture_upload(&game.texture, 0, pixels, static_cast<uint32_t>(width) * 4);
    game.loaded = true;
    xsdk_log_write("art: loaded %s (%dx%d)", game.title, width, height);
  } else {
    xsdk_log_write("art: texture allocation failed for %s: 0x%08X", game.title, static_cast<unsigned>(status));
  }
  stbi_image_free(pixels);
  return game.loaded;
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

void buildScene(uint32_t selected) {
  vertexCount = 0;
  rect(0, 0, static_cast<float>(screenWidth), static_cast<float>(screenHeight), kBackground);
  rect(0, 0, static_cast<float>(screenWidth), 76, 0xFF0D1115);
  rect(0, 74, static_cast<float>(screenWidth), 2, 0xFF222A30);
  rect(38, 29, 18, 18, kGreen);
  rect(43, 34, 8, 8, 0xFF0D1115);
  rect(43, 54, 8, 3, kGreen);
  rect(304, 55, 50, 3, kGreen);
  rect(702, 16, 300, 42, 0xFF171E24);
  rect(68, 274, 172, 38, kGreen);

  // Featured banner image sits beside the dark copy area, as in the streaming app's home screen.
  rect(38, 98, 1204, 242, 0xFF11171C);
  rect(38, 98, 570, 242, 0xFF10161B);
  rect(38, 98, 3, 242, kGreen);
  rect(608, 98, 634, 242, 0x65080B0E);
  rect(38, 352, 1204, 1, 0xFF222A30);

  // Favourites and library both use genuine downloadable Steam header artwork.
  constexpr float cardX[] = {38, 344, 650, 956};
  constexpr float cardW = 286;
  for (uint32_t i = 0; i < kGameCount; ++i) {
    rect(cardX[i], 403, cardW, 117, kPanel);
    if (i == selected) outlinedRect(cardX[i] - 2, 401, cardW + 4, 121, 2, kGreen);
    rect(cardX[i], 522, cardW, 2, i == selected ? kGreen : 0xFF202930);
    rect(cardX[i], 572, cardW, 80, 0xFF11171C);
  }
}

void uploadScene() {
  const uint32_t words = vertexCount * kVertexFloats;
  memcpy(packedVertices, cpuVertices, words * sizeof(uint32_t));
  for (uint32_t i = 0; i < words; ++i) gpuVertices[i] = packedVertices[i];
  xsdk_gpu_invalidate(xsdk_gpu_address(gpuVertices), words * sizeof(uint32_t));
}

void drawText(uint32_t selected, uint32_t loadedCount, bool notice, bool loading = false) {
  xsdk_text_draw(72, 27, 1, kWhite, "GEFORCE NOW");
  xsdk_text_draw(304, 28, 1, kGreen, "GAMES");
  xsdk_text_draw(397, 28, 1, kMuted, "MY LIBRARY");
  xsdk_text_draw(540, 28, 1, kMuted, "SETTINGS");
  xsdk_text_draw(725, 26, 1, kDim, "SEARCH GAMES");
  xsdk_text_draw(1050, 27, 1, loadedCount ? kGreen : (loading ? kMuted : kDim),
                 loadedCount ? "ARTWORK ONLINE" : (loading ? "LOADING ART" : "ARTWORK OFFLINE"));

  xsdk_text_draw(68, 122, 1, kGreen, "FEATURED GAME");
  xsdk_text_draw(68, 158, 2, kWhite, "CYBERPUNK 2077");
  xsdk_text_draw(68, 206, 1, kMuted, "NIGHT CITY NEVER SLEEPS.");
  xsdk_text_draw(68, 233, 1, kMuted, "REAL GAME ARTWORK  /  ONLINE PREVIEW");
  xsdk_text_draw(84, 286, 1, 0xFF091007, "A  SELECT GAME TILE");
  xsdk_text_draw(68, 321, 1, loadedCount ? kGreen : (loading ? kMuted : kDim),
                 loadedCount ? "ARTWORK LOADED FROM INTERNET" : (loading ? "FETCHING GAME COVER IMAGES..." : "CONNECT CONSOLE TO INTERNET FOR ART"));

  xsdk_text_draw(38, 367, 1, kWhite, "MY FAVOURITES");
  xsdk_text_draw(1090, 367, 1, kDim, "VIEW ALL  >");
  xsdk_text_draw(38, 539, 1, kWhite, "MY LIBRARY");
  xsdk_text_draw(1090, 539, 1, kDim, "RECENTLY PLAYED");
  constexpr int32_t cardX[] = {38, 344, 650, 956};
  for (uint32_t i = 0; i < kGameCount; ++i) {
    xsdk_text_draw(cardX[i] + 4, 526, 1, i == selected ? kGreen : kMuted, games[i].title);
  }
  if (notice) {
    xsdk_text_draw(38, 668, 1, kGreen, "UNOFFICIAL UI  -  NO NVIDIA SIGN-IN OR GAME STREAMING");
  } else {
    xsdk_text_draw(38, 668, 1, kDim, "D-PAD LEFT / RIGHT  BROWSE       A  SELECT TILE       B  EXIT");
  }
}
}  // namespace

int gfx_run(void) {
  xsdk_log_write("GeForce NOW-inspired Xbox UI: starting");
  const CURLcode curlInit = curl_global_init(CURL_GLOBAL_DEFAULT);
  uint32_t loadedCount = 0;
  NTSTATUS status = xsdk_gpu_open();
  if (!NT_SUCCESS(status)) {
    xsdk_log_write("gfx: xsdk_gpu_open failed: 0x%08X", static_cast<unsigned>(status));
    curl_global_cleanup();
    return 1;
  }
  status = xsdk_shader_load(&vertexShader, ui_vert.container, ui_vert.container_bytes);
  if (NT_SUCCESS(status)) status = xsdk_shader_load(&pixelShader, ui_frag.container, ui_frag.container_bytes);
  const XsdkVertexElement layout[] = {
      {XSDK_USAGE_POSITION, 0, XSDK_VERTEX_FLOAT2, 0, 0, {0, 0, 0, 0}, false},
      {XSDK_USAGE_COLOR, 0, XSDK_VERTEX_FLOAT3, 0, 8, {0, 0, 0, 0}, false},
      {XSDK_USAGE_TEXCOORD, 0, XSDK_VERTEX_FLOAT2, 0, 20, {0, 0, 0, 0}, false},
  };
  const uint32_t stride = kVertexBytes;
  if (NT_SUCCESS(status)) status = xsdk_shader_set_vertex_layout(&vertexShader, layout, 3, &stride);
  if (!NT_SUCCESS(status)) {
    xsdk_log_write("gfx: shader setup failed: 0x%08X", static_cast<unsigned>(status));
    xsdk_gpu_close(); curl_global_cleanup(); return 1;
  }
  status = xsdk_text_open();
  if (!NT_SUCCESS(status)) {
    xsdk_log_write("gfx: xsdk_text_open failed: 0x%08X", static_cast<unsigned>(status));
    xsdk_gpu_close(); curl_global_cleanup(); return 1;
  }
  gpuVertices = xsdk_gpu_alloc(kMaxVertices * kVertexBytes);
  if (!gpuVertices) {
    xsdk_log_write("gfx: vertex allocation failed");
    xsdk_text_close(); xsdk_gpu_close(); curl_global_cleanup(); return 1;
  }
  status = xsdk_present_open(XSDK_DISPLAY_720P, kBuffers, 1, &screenWidth, &screenHeight);
  if (!NT_SUCCESS(status)) {
    xsdk_log_write("gfx: xsdk_present_open failed: 0x%08X", static_cast<unsigned>(status));
    xsdk_gpu_free(gpuVertices); xsdk_text_close(); xsdk_gpu_close(); curl_global_cleanup(); return 1;
  }

  // Paint a loading state first so network setup never leaves a black screen.
  buildScene(0);
  uploadScene();
  uint32_t loadingAddress = 0;
  status = xsdk_present_begin(&loadingAddress, kBeginTimeoutMs);
  if (NT_SUCCESS(status)) {
    xsdk_gpu_set_target(screenWidth, screenHeight);
    xsdk_gpu_set_shaders(&vertexShader, &pixelShader);
    xsdk_built_shader_set_literals(&ui_vert);
    xsdk_built_shader_set_literals(&ui_frag);
    xsdk_gpu_set_vertices(0, xsdk_gpu_address(gpuVertices), vertexCount * kVertexBytes);
    xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, vertexCount);
    drawText(0, 0, false, true);
    xsdk_gpu_resolve(loadingAddress, screenWidth, screenHeight, kBackground);
    xsdk_present_end();
    status = xsdk_gpu_wait(xsdk_gpu_fence(), kFenceTimeoutMs);
  }
  if (!NT_SUCCESS(status)) {
    xsdk_log_write("gfx: could not present loading screen: 0x%08X", static_cast<unsigned>(status));
  }
  if (curlInit == CURLE_OK) {
    for (uint32_t i = 0; i < kGameCount; ++i) if (loadGameArt(games[i])) ++loadedCount;
  } else {
    xsdk_log_write("art: libcurl initialization failed: %d", static_cast<int>(curlInit));
  }

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

  uint32_t selected = 0;
  bool notice = false;
  uint16_t previousButtons = 0;
  buildScene(selected);
  uploadScene();
  xsdk_log_write("gfx: display %ux%u; artwork %u/%u; D-pad browse, A select, B exit", screenWidth,
                 screenHeight, loadedCount, kGameCount);
  for (;;) {
    INPUT_STATE input{};
    if (XamInputGetState(0, 0, &input) != ERROR_SUCCESS) input = {};
    const uint16_t buttons = input.gamepad.buttons;
    const uint16_t pressed = buttons & static_cast<uint16_t>(~previousButtons);
    previousButtons = buttons;
    if (pressed & INPUT_GAMEPAD_B) break;
    if (pressed & (INPUT_GAMEPAD_DPAD_LEFT | INPUT_GAMEPAD_DPAD_RIGHT)) {
      status = xsdk_gpu_wait(xsdk_gpu_fence(), kFenceTimeoutMs);
      if (!NT_SUCCESS(status)) { xsdk_log_write("gfx: fence wait failed: 0x%08X", static_cast<unsigned>(status)); break; }
      selected = (pressed & INPUT_GAMEPAD_DPAD_LEFT) ? (selected + kGameCount - 1) % kGameCount : (selected + 1) % kGameCount;
      buildScene(selected);
      uploadScene();
      notice = false;
    }
    if (pressed & INPUT_GAMEPAD_A) {
      notice = true;
      xsdk_log_write("UI tile selected: %s (account login and streaming are not implemented)", games[selected].title);
    }

    uint32_t address = 0;
    status = xsdk_present_begin(&address, kBeginTimeoutMs);
    if (!NT_SUCCESS(status)) { xsdk_log_write("gfx: xsdk_present_begin failed: 0x%08X", static_cast<unsigned>(status)); break; }
    xsdk_gpu_set_target(screenWidth, screenHeight);
    xsdk_gpu_set_shaders(&vertexShader, &pixelShader);
    xsdk_built_shader_set_literals(&ui_vert);
    xsdk_built_shader_set_literals(&ui_frag);
    xsdk_gpu_set_vertices(0, xsdk_gpu_address(gpuVertices), vertexCount * kVertexBytes);
    xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, vertexCount);
    // The artwork uses a compact, immutable second vertex buffer.
    {
      static Vertex coverCpu[kGameCount * 18];
      static uint32_t coverPacked[kGameCount * 18 * kVertexFloats];
      static bool coverUploaded = false;
      if (!gpuCovers) gpuCovers = xsdk_gpu_alloc(sizeof(coverPacked));
      uint32_t coverCount = 0;
      auto appendQuad = [&](uint32_t idx, float x, float y, float w, float h) {
        if (!games[idx].loaded) return;
        const float l = 2.0f * x / screenWidth - 1.0f, r = 2.0f * (x + w) / screenWidth - 1.0f;
        const float t = 1.0f - 2.0f * y / screenHeight, b = 1.0f - 2.0f * (y + h) / screenHeight;
        const Vertex q[] = {{l,t,1,1,1,0,0},{r,t,1,1,1,1,0},{r,b,1,1,1,1,1},{l,t,1,1,1,0,0},{r,b,1,1,1,1,1},{l,b,1,1,1,0,1}};
        for (unsigned n=0;n<6;++n) coverCpu[coverCount++]=q[n];
      };
      appendQuad(0, 608, 98, 634, 242);
      constexpr float cardX[] = {38, 344, 650, 956};
      for (uint32_t i=0;i<kGameCount;++i) { appendQuad(i,cardX[i],403,286,117); appendQuad(i,cardX[i],572,286,80); }
      if (!coverUploaded) memcpy(coverPacked, coverCpu, coverCount * kVertexBytes);
      if (coverCount && gpuCovers) {
        if (!coverUploaded) {
          for (uint32_t n=0;n<coverCount*kVertexFloats;++n) gpuCovers[n]=coverPacked[n];
          xsdk_gpu_invalidate(xsdk_gpu_address(gpuCovers),coverCount*kVertexBytes);
          coverUploaded = true;
        }
        xsdk_gpu_set_vertices(0,xsdk_gpu_address(gpuCovers),coverCount*kVertexBytes);
        if (games[0].loaded) {
          xsdk_gpu_set_texture(0,&games[0].texture,&samplerState);
          xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST,6);
        }
        for (uint32_t i=0;i<kGameCount;++i) {
          if (!games[i].loaded) continue;
          // The hero uses six vertices, then each preceding loaded game contributes two card quads.
          uint32_t gameOffset = games[0].loaded ? 6 : 0;
          for (uint32_t j=0;j<i;++j) if (games[j].loaded) gameOffset += 12;
          xsdk_gpu_set_vertices(0,xsdk_gpu_address(gpuCovers)+gameOffset*kVertexBytes,(coverCount-gameOffset)*kVertexBytes);
          xsdk_gpu_set_texture(0,&games[i].texture,&samplerState);
          xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST,12);
        }
      }
    }
    drawText(selected, loadedCount, notice);
    xsdk_gpu_resolve(address, screenWidth, screenHeight, kBackground);
    xsdk_present_end();
  }

  status = xsdk_gpu_wait(xsdk_gpu_fence(), kFenceTimeoutMs);
  if (!NT_SUCCESS(status)) xsdk_log_write("gfx: final fence wait failed: 0x%08X", static_cast<unsigned>(status));
  xsdk_text_close();
  xsdk_present_close();
  xsdk_gpu_free(gpuVertices);
  gpuVertices = nullptr;
  if (gpuCovers) { xsdk_gpu_free(gpuCovers); gpuCovers = nullptr; }
  for (uint32_t i = 0; i < kGameCount; ++i) if (games[i].loaded) xsdk_texture_free(&games[i].texture);
  xsdk_shader_free(&pixelShader);
  xsdk_shader_free(&vertexShader);
  status = xsdk_gpu_close();
  curl_global_cleanup();
  if (!NT_SUCCESS(status)) xsdk_log_write("gfx: xsdk_gpu_close failed: 0x%08X", static_cast<unsigned>(status));
  xsdk_log_write("GeForce NOW-inspired Xbox UI: closed");
  return 0;
}
