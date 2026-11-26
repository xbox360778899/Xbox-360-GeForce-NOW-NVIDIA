#include "gfx.h"

#include <cstdint>
#include <cstring>

#include "geforce_now_ui_shaders.h"
#include "xsdk/gpu.h"
#include "xsdk/kernel.h"
#include "xsdk/log.h"
#include "xsdk/present.h"
#include "xsdk/shader.h"
#include "xsdk/text.h"

namespace {
constexpr uint32_t kBuffers = 2;
constexpr uint32_t kBeginTimeoutMs = 5000;
constexpr uint32_t kFenceTimeoutMs = 1000;
constexpr uint32_t kMaxVertices = 2048;
constexpr uint32_t kVertexFloats = 5;
constexpr uint32_t kVertexBytes = kVertexFloats * sizeof(float);
constexpr uint32_t kBackground = 0xFF080B0E;
constexpr uint32_t kGreen = 0xFF76B900;
constexpr uint32_t kWhite = 0xFFF3F6F8;
constexpr uint32_t kMuted = 0xFFA8B2BA;
constexpr uint32_t kDim = 0xFF66727C;

struct Vertex { float x, y, r, g, b; };
static_assert(sizeof(Vertex) == kVertexBytes);
Vertex cpuVertices[kMaxVertices]{};
uint32_t packedVertices[kMaxVertices * kVertexFloats]{};
uint32_t vertexCount = 0;
volatile uint32_t* gpuVertices = nullptr;
XsdkShader vertexShader{};
XsdkShader pixelShader{};
uint32_t screenWidth = 1280;
uint32_t screenHeight = 720;

struct GameCard { const char* mark; const char* title; const char* genre; uint32_t base; uint32_t accent; };
constexpr GameCard kCards[] = {
    {"ND", "NEON DRIFT", "RACING  /  4K READY", 0xFF102027, 0xFF76B900},
    {"SF", "STARFALL", "ACTION RPG  /  ADVENTURE", 0xFF17182A, 0xFF8D6BFF},
    {"IF", "IRON FRONT", "TACTICAL  /  MULTIPLAYER", 0xFF241916, 0xFFFF884D},
    {"WS", "WILD SIGNAL", "EXPLORE  /  SURVIVE", 0xFF10231D, 0xFF38D6A1},
};
constexpr uint32_t kCardCount = sizeof(kCards) / sizeof(kCards[0]);

float channel(uint32_t color, unsigned shift) { return static_cast<float>((color >> shift) & 0xFF) / 255.0f; }

void vertex(float x, float y, uint32_t color) {
  if (vertexCount >= kMaxVertices) return;
  const float clipX = 2.0f * x / static_cast<float>(screenWidth) - 1.0f;
  const float clipY = 1.0f - 2.0f * y / static_cast<float>(screenHeight);
  cpuVertices[vertexCount++] = {clipX, clipY, channel(color, 16), channel(color, 8), channel(color, 0)};
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

  rect(0, 0, static_cast<float>(screenWidth), 72, 0xFF0D1115);
  rect(0, 70, static_cast<float>(screenWidth), 2, 0xFF1B242B);
  rect(40, 26, 15, 15, kGreen);
  rect(45, 31, 5, 5, 0xFF0D1115);
  rect(40, 46, 15, 2, kGreen);
  rect(250, 53, 38, 3, kGreen);

  // Geometric hero art keeps the first screen self-contained and asset-free.
  rect(40, 96, 1200, 238, 0xFF10171D);
  rect(40, 96, 6, 238, kGreen);
  rect(700, 96, 540, 238, 0xFF111B20);
  triangle(700, 96, 1000, 96, 850, 334, 0xFF14252B);
  triangle(930, 96, 1240, 96, 1090, 334, 0xFF17282B);
  triangle(1090, 96, 1240, 96, 1240, 334, 0xFF1A3028);
  rect(1005, 110, 8, 208, 0xFF213832);
  rect(1032, 110, 2, 208, 0xFF213832);
  rect(1084, 110, 2, 208, 0xFF213832);
  rect(1136, 110, 2, 208, 0xFF213832);
  rect(1188, 110, 2, 208, 0xFF213832);
  triangle(770, 244, 875, 139, 980, 244, 0xFF1A3030);
  triangle(875, 139, 980, 244, 875, 298, 0xFF203F30);
  triangle(770, 244, 875, 298, 875, 139, 0xFF152624);
  rect(856, 228, 38, 8, kGreen);
  rect(874, 210, 8, 46, kGreen);
  rect(74, 267, 230, 36, kGreen);

  constexpr float cardY = 408, cardW = 282, cardH = 166, gap = 18;
  for (uint32_t i = 0; i < kCardCount; ++i) {
    const float x = 40 + i * (cardW + gap);
    const GameCard& card = kCards[i];
    rect(x, cardY, cardW, cardH, card.base);
    triangle(x + 126, cardY, x + cardW, cardY, x + cardW, cardY + cardH, card.accent & 0xFF5F5F5F);
    triangle(x + 164, cardY, x + 235, cardY, x + 198, cardY + 70, card.accent);
    rect(x + 14, cardY + 16, 54, 4, card.accent);
    rect(x + 14, cardY + 79, cardW - 28, 1, 0xFF364149);
    rect(x + 14, cardY + cardH - 10, cardW - 28, 2, 0xFF263039);
    if (i == selected) {
      outlinedRect(x - 2, cardY - 2, cardW + 4, cardH + 4, 3, kGreen);
      rect(x + cardW - 29, cardY + 15, 13, 13, kGreen);
    }
  }
}

void uploadScene() {
  const uint32_t words = vertexCount * kVertexFloats;
  std::memcpy(packedVertices, cpuVertices, words * sizeof(uint32_t));
  for (uint32_t i = 0; i < words; ++i) gpuVertices[i] = packedVertices[i];
  xsdk_gpu_invalidate(xsdk_gpu_address(gpuVertices), words * sizeof(uint32_t));
}

void drawText(uint32_t selected, bool notice) {
  xsdk_text_draw(40, 25, 1, kWhite, "GEFORCE NOW");
  xsdk_text_draw(270, 25, 1, kGreen, "HOME");
  xsdk_text_draw(320, 25, 1, kMuted, "MY LIBRARY");
  xsdk_text_draw(440, 25, 1, kMuted, "SETTINGS");
  xsdk_text_draw(1010, 25, 1, kGreen, "UI PREVIEW  /  OFFLINE");

  xsdk_text_draw(74, 119, 1, kGreen, "CLOUD GAMING  /  XBOX 360 UI CONCEPT");
  xsdk_text_draw(74, 158, 2, kWhite, "YOUR GAMES. YOUR WAY.");
  xsdk_text_draw(74, 211, 1, kMuted, "A GEFORCE NOW INSPIRED HOME SCREEN");
  xsdk_text_draw(74, 234, 1, kMuted, "FIRST UI STEP - STREAMING COMES LATER");
  xsdk_text_draw(74, 276, 1, 0xFF071007, "BROWSE YOUR LIBRARY");
  xsdk_text_draw(1000, 125, 1, kGreen, "LOW LATENCY");
  xsdk_text_draw(1000, 153, 1, kWhite, "READY TO PLAY");
  xsdk_text_draw(1000, 181, 1, kMuted, "CONCEPT BUILD");
  xsdk_text_draw(1000, 272, 1, kMuted, "UNOFFICIAL UI CONCEPT");

  xsdk_text_draw(40, 366, 2, kWhite, "PICK UP AND PLAY");
  xsdk_text_draw(984, 371, 1, kMuted, "FEATURED  /  04 GAMES");
  constexpr int32_t cardX[] = {40, 340, 640, 940};
  for (uint32_t i = 0; i < kCardCount; ++i) {
    const GameCard& card = kCards[i];
    xsdk_text_draw(cardX[i] + 16, 434, 2, card.accent, card.mark);
    xsdk_text_draw(cardX[i] + 16, 505, 1, kWhite, card.title);
    xsdk_text_draw(cardX[i] + 16, 536, 1, kMuted, card.genre);
  }

  if (notice) {
    xsdk_text_draw(40, 609, 1, kGreen, "UI PREVIEW ONLY - CLOUD STREAMING IS NOT CONNECTED YET");
  } else {
    xsdk_text_draw(40, 609, 1, kMuted, "SELECTED:");
    xsdk_text_draw(140, 609, 1, kWhite, kCards[selected].title);
    xsdk_text_draw(300, 609, 1, kDim, "PRESS A TO OPEN THIS GAME TILE");
  }
  xsdk_text_draw(40, 670, 1, kWhite, "DPAD  BROWSE");
  xsdk_text_draw(240, 670, 1, kWhite, "A  SELECT");
  xsdk_text_draw(390, 670, 1, kWhite, "B  EXIT");
  xsdk_text_draw(930, 670, 1, kDim, "UNOFFICIAL COMMUNITY PROJECT");
}
}  // namespace

int gfx_run(void) {
  xsdk_log_write("GeForce NOW UI preview: starting Xbox 360 home screen");
  NTSTATUS status = xsdk_gpu_open();
  if (!NT_SUCCESS(status)) {
    xsdk_log_write("gfx: xsdk_gpu_open failed: 0x%08X", static_cast<unsigned>(status));
    return 1;
  }

  status = xsdk_shader_load(&vertexShader, ui_vert.container, ui_vert.container_bytes);
  if (NT_SUCCESS(status)) status = xsdk_shader_load(&pixelShader, ui_frag.container, ui_frag.container_bytes);
  const XsdkVertexElement layout[] = {
      {XSDK_USAGE_POSITION, 0, XSDK_VERTEX_FLOAT2, 0, 0, {0, 0, 0, 0}, false},
      {XSDK_USAGE_COLOR, 0, XSDK_VERTEX_FLOAT3, 0, 8, {0, 0, 0, 0}, false},
  };
  const uint32_t stride = kVertexBytes;
  if (NT_SUCCESS(status)) status = xsdk_shader_set_vertex_layout(&vertexShader, layout, 2, &stride);
  if (!NT_SUCCESS(status)) {
    xsdk_log_write("gfx: shader setup failed: 0x%08X", static_cast<unsigned>(status));
    return 1;
  }
  status = xsdk_text_open();
  if (!NT_SUCCESS(status)) {
    xsdk_log_write("gfx: xsdk_text_open failed: 0x%08X", static_cast<unsigned>(status));
    return 1;
  }

  gpuVertices = xsdk_gpu_alloc(kMaxVertices * kVertexBytes);
  if (!gpuVertices) {
    xsdk_log_write("gfx: vertex allocation failed");
    return 1;
  }
  status = xsdk_present_open(XSDK_DISPLAY_720P, kBuffers, 1, &screenWidth, &screenHeight);
  if (!NT_SUCCESS(status)) {
    xsdk_log_write("gfx: xsdk_present_open failed: 0x%08X", static_cast<unsigned>(status));
    return 1;
  }
  xsdk_log_write("gfx: display %ux%u; use D-pad, A select, B exit", screenWidth, screenHeight);

  uint32_t selected = 0;
  bool notice = false;
  uint16_t previousButtons = 0;
  buildScene(selected);
  uploadScene();
  for (;;) {
    INPUT_STATE input{};
    if (XamInputGetState(0, 0, &input) != ERROR_SUCCESS) input = {};
    const uint16_t buttons = input.gamepad.buttons;
    const uint16_t pressed = buttons & static_cast<uint16_t>(~previousButtons);
    previousButtons = buttons;
    if (pressed & INPUT_GAMEPAD_B) break;

    if (pressed & (INPUT_GAMEPAD_DPAD_LEFT | INPUT_GAMEPAD_DPAD_RIGHT)) {
      const uint32_t next = (pressed & INPUT_GAMEPAD_DPAD_LEFT)
          ? (selected + kCardCount - 1) % kCardCount : (selected + 1) % kCardCount;
      status = xsdk_gpu_wait(xsdk_gpu_fence(), kFenceTimeoutMs);
      if (!NT_SUCCESS(status)) {
        xsdk_log_write("gfx: fence wait failed: 0x%08X", static_cast<unsigned>(status));
        break;
      }
      selected = next;
      buildScene(selected);
      uploadScene();
      notice = false;
    }
    if (pressed & INPUT_GAMEPAD_A) {
      notice = true;
      xsdk_log_write("UI preview selected: %s (streaming not implemented)", kCards[selected].title);
    }

    uint32_t address = 0;
    status = xsdk_present_begin(&address, kBeginTimeoutMs);
    if (!NT_SUCCESS(status)) {
      xsdk_log_write("gfx: xsdk_present_begin failed: 0x%08X", static_cast<unsigned>(status));
      break;
    }
    xsdk_gpu_set_target(screenWidth, screenHeight);
    xsdk_gpu_set_shaders(&vertexShader, &pixelShader);
    xsdk_built_shader_set_literals(&ui_vert);
    xsdk_built_shader_set_literals(&ui_frag);
    xsdk_gpu_set_vertices(0, xsdk_gpu_address(gpuVertices), vertexCount * kVertexBytes);
    xsdk_gpu_draw(XSDK_PRIMITIVE_TRIANGLE_LIST, vertexCount);
    drawText(selected, notice);
    xsdk_gpu_resolve(address, screenWidth, screenHeight, kBackground);
    xsdk_present_end();
  }

  status = xsdk_gpu_wait(xsdk_gpu_fence(), kFenceTimeoutMs);
  if (!NT_SUCCESS(status)) xsdk_log_write("gfx: final fence wait failed: 0x%08X", static_cast<unsigned>(status));
  xsdk_text_close();
  xsdk_present_close();
  xsdk_gpu_free(gpuVertices);
  gpuVertices = nullptr;
  xsdk_shader_free(&pixelShader);
  xsdk_shader_free(&vertexShader);
  status = xsdk_gpu_close();
  if (!NT_SUCCESS(status)) xsdk_log_write("gfx: xsdk_gpu_close failed: 0x%08X", static_cast<unsigned>(status));
  xsdk_log_write("GeForce NOW UI preview: closed");
  return 0;
}
