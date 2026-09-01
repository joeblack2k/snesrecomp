/* Synthetic regression for SNES OBJ range/fetch ordering.
 * No game ROM, generated data, or platform frontend is required. */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "snes/ppu.h"
#include "snes/snes.h"

Snes *g_snes;

uint16_t WsShadowTile(int layer, int screen_x, uint32_t wrapped_y,
                      uint16_t real_tile) {
    (void)layer;
    (void)screen_x;
    (void)wrapped_y;
    return real_tile;
}

uint16_t WsShadowTileDebug(int layer, int screen_x, int screen_y,
                           int pixel_span, uint32_t wrapped_y,
                           uint16_t h_scroll, uint16_t map_word_adr,
                           uint16_t real_tile) {
    (void)screen_y;
    (void)pixel_span;
    (void)h_scroll;
    (void)map_word_adr;
    return WsShadowTile(layer, screen_x, wrapped_y, real_tile);
}

bool WsShadowLayerActive(int layer) {
    (void)layer;
    return false;
}

uint32_t WsShadowWorldX(int layer) {
    (void)layer;
    return 0;
}

uint32_t WsShadowPresentWorldY(int layer, int screen_x) {
    (void)layer;
    (void)screen_x;
    return 0;
}

uint32_t WsShadowScrollY(int layer) {
    (void)layer;
    return 0;
}

void WsShadowOnVramWrite(uint16_t word_adr, uint16_t value) {
    (void)word_adr;
    (void)value;
}

static int check(bool condition, const char *message) {
    if (!condition) fprintf(stderr, "FAIL: %s\n", message);
    return condition ? 0 : 1;
}

static void no_op_line_enhancer(Ppu *ppu, uint y, bool sub, void *context) {
    (void)ppu;
    (void)y;
    (void)sub;
    (void)context;
}

static void setup_35_sliver_case(Ppu *ppu) {
    for (int slot = 0; slot < 128; slot++)
        ppu->oam[slot * 2] = 0xf000;
    memset(ppu->highOam, 0, sizeof ppu->highOam);
    ppu->obsel = 2 << 5;  /* size pair 8x8 / 64x64 */
    /* Fetch order is reversed. Slots 1..4 contribute 32 slivers and slots
     * 5..6 contribute two more, so native fetch drops slot 0 as sliver 35. */
    ppu->oam[0] = 0x0000;
    for (int slot = 1; slot <= 6; slot++) {
        ppu->oam[slot * 2] = 0x0040;
        if (slot <= 4) {
            int high_byte = slot >> 2;
            int size_bit = ((slot & 3) * 2) + 1;
            ppu->highOam[high_byte] |= (uint8_t)(1u << size_bit);
        }
    }
    for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
        ppu->vram[i] = 0xffff;
}

static void setup_33_sprite_case(Ppu *ppu) {
    for (int slot = 0; slot < 128; slot++)
        ppu->oam[slot * 2] = 0xf000;
    memset(ppu->highOam, 0, sizeof ppu->highOam);
    ppu->obsel = 0;  /* 8x8 / 16x16; all entries use the small size */
    for (int slot = 0; slot < 33; slot++)
        ppu->oam[slot * 2] = 0x0040;
    for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
        ppu->vram[i] = 0xffff;
}


/* Widescreen mirror axis: a 4bpp Mode 1 BG1 whose tile columns each carry a
 * distinct flat color, rendered 43 pixels wide on each side. The margins wrap
 * the 32-column map (the shadow stub above returns the real tile), so every
 * screen column has a known color before the axis reflection is applied. */
static void setup_mirror_axis_case(Ppu *ppu) {
    for (int slot = 0; slot < 128; slot++)
        ppu->oam[slot * 2] = 0xf000;
    memset(ppu->highOam, 0, sizeof ppu->highOam);
    memset(ppu->vram, 0, sizeof ppu->vram);
    ppu->bgmode = 1;
    ppu->screenEnabled[0] = 0x01;
    ppu->screenEnabled[1] = 0x00;
    ppu->bgXsc[0] = 0x00;      /* 32x32 tilemap at word 0 */
    ppu->bgTileAdr = 0x01;     /* BG1 characters at word 0x1000 */
    ppu->hScroll[0] = 0;
    ppu->vScroll[0] = 0;
    for (int tile = 1; tile < 16; tile++) {
        for (int row = 0; row < 8; row++) {
            const uint16_t lo = (uint16_t)(((tile & 1) ? 0x00ff : 0) |
                                           ((tile & 2) ? 0xff00 : 0));
            const uint16_t hi = (uint16_t)(((tile & 4) ? 0x00ff : 0) |
                                           ((tile & 8) ? 0xff00 : 0));
            ppu->vram[0x1000 + tile * 16 + row] = lo;
            ppu->vram[0x1000 + tile * 16 + 8 + row] = hi;
        }
    }
    for (int row = 0; row < 32; row++)
        for (int col = 0; col < 32; col++)
            ppu->vram[row * 32 + col] = (uint16_t)((col % 15) + 1);
}

static PpuZbufType bg_pixel(const Ppu *ppu, int x) {
    return ppu->bgBuffers[0].data[x + kPpuExtraLeftRight];
}

static int expected_color(int x) {
    return ((((x >> 3) & 31) % 15) + 1);
}

int main(void) {
    /* The shared scratch surface must also hold the live-margin cases below;
     * PPU priority buffers are wider than native even when the first tests
     * only inspect their OBJ plane. */
    enum { kTestExtra = 8, kPitch = (kPpuXPixels + kTestExtra * 2) * 4 };
    uint8_t pixels[kPitch];
    Ppu *ppu = ppu_init();
    int failures = 0;
    if (!ppu) return 2;
    memset(pixels, 0, sizeof pixels);
    ppu_reset(ppu);
    PpuBeginDrawing(ppu, pixels, kPitch, kPpuRenderFlags_NewRenderer);
    ppu->inidisp = 0x0f;

    /* Keep unused OAM entries off this line. Slot 0 is one 8x8 sprite at x=0;
     * slots 1..5 are 64x64 sprites at x=64. Reverse tile fetch reaches the
     * 34-sliver limit before slot 0, while a forward one-pass implementation
     * incorrectly renders it. */
    for (int slot = 0; slot < 128; slot++)
        ppu->oam[slot * 2] = 0xf000;
    ppu->obsel = 2 << 5;  /* size pair 8x8 / 64x64 */
    ppu->oam[0] = 0x0000;
    for (int slot = 1; slot <= 5; slot++) {
        ppu->oam[slot * 2] = 0x0040;
        int high_byte = slot >> 2;
        int size_bit = ((slot & 3) * 2) + 1;
        ppu->highOam[high_byte] |= (uint8_t)(1u << size_bit);
    }
    for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
        ppu->vram[i] = 0xffff;

    ppu_runLine(ppu, 0);
    ppu_runLine(ppu, 1);
    failures += check(ppu->timeOver, "34-sliver overflow is reported");
    failures += check((ppu->objBuffer.data[kPpuExtraLeftRight] & 0xff) == 0,
                      "reverse fetch drops low slot after sliver overflow");

    PpuBeginDrawing(ppu, pixels, kPitch,
                    kPpuRenderFlags_NewRenderer |
                    kPpuRenderFlags_NoSpriteLimits);
    ppu_runLine(ppu, 0);
    ppu_runLine(ppu, 1);
    failures += check((ppu->objBuffer.data[kPpuExtraLeftRight] & 0xff) != 0,
                      "disabling sprite limits renders the low slot");

    /* A live widened world gets only the capacity represented by its added
     * columns. The same flag with zero live columns (including a centered
     * fixed screen) must retain the exact native limits. */
    ppu_reset(ppu);
    PpuBeginDrawing(ppu, pixels, kPitch,
                    kPpuRenderFlags_NewRenderer |
                    kPpuRenderFlags_WidescreenSpriteBudget);
    PpuSetExtraSpaceCentered(ppu, 8);
    ppu->inidisp = 0x0f;
    setup_35_sliver_case(ppu);
    ppu_runLine(ppu, 0);
    ppu_runLine(ppu, 1);
    failures += check(ppu->timeOver,
                      "centered widescreen keeps native 34-sliver limit");
    failures += check((ppu->objBuffer.data[kPpuExtraLeftRight] & 0xff) == 0,
                      "centered widescreen still drops sliver 35");

    ppu_reset(ppu);
    PpuBeginDrawing(ppu, pixels, kPitch,
                    kPpuRenderFlags_NewRenderer |
                    kPpuRenderFlags_WidescreenSpriteBudget);
    PpuSetExtraSpace(ppu, 8);  /* 16 added pixels -> two added slivers */
    ppu->inidisp = 0x0f;
    setup_35_sliver_case(ppu);
    ppu_runLine(ppu, 0);
    ppu_runLine(ppu, 1);
    failures += check(!ppu->timeOver,
                      "live 16-pixel margins admit sliver 35");
    failures += check((ppu->objBuffer.data[kPpuExtraLeftRight] & 0xff) != 0,
                      "live widened world renders the formerly dropped slot");

    ppu_reset(ppu);
    PpuBeginDrawing(ppu, pixels, kPitch,
                    kPpuRenderFlags_NewRenderer |
                    kPpuRenderFlags_WidescreenSpriteBudget);
    PpuSetExtraSpaceCentered(ppu, 8);
    ppu->inidisp = 0x0f;
    setup_33_sprite_case(ppu);
    ppu_runLine(ppu, 0);
    ppu_runLine(ppu, 1);
    failures += check(ppu->rangeOver,
                      "centered widescreen keeps native 32-sprite limit");

    ppu_reset(ppu);
    PpuBeginDrawing(ppu, pixels, kPitch,
                    kPpuRenderFlags_NewRenderer |
                    kPpuRenderFlags_WidescreenSpriteBudget);
    PpuSetExtraSpace(ppu, 8);
    ppu->inidisp = 0x0f;
    setup_33_sprite_case(ppu);
    ppu_runLine(ppu, 0);
    ppu_runLine(ppu, 1);
    failures += check(!ppu->rangeOver,
                      "live 16-pixel margins admit sprite 33");

    /* Existing line-enhancer users rely on BG1 staying inside its authentic
     * destination viewport. New title-specific source insets must be opt-in
     * and must not relax that legacy fallback. */
    {
        enum { kExtra = 8, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLineEnhancer(ppu, no_op_line_enhancer, NULL);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 1;
        ppu->screenEnabled[0] = 1;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] == 0 &&
                              wide_pixels[kExtra - 1] == 0 &&
                              wide_pixels[kExtra + kPpuXPixels] == 0,
                          "legacy line enhancer keeps BG1 out of margins");
        failures += check(wide_pixels[kExtra] != 0 &&
                              wide_pixels[kExtra + kPpuXPixels - 1] != 0,
                          "legacy line enhancer retains native BG1");

        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLayerViewportInset(ppu, 0, 16, 16);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[kExtra] == 0 &&
                              wide_pixels[kExtra + 15] == 0 &&
                              wide_pixels[kExtra + 240] == 0,
                          "explicit BG1 viewport inset hides native padding");
        failures += check(wide_pixels[kExtra + 16] != 0 &&
                              wide_pixels[kExtra + 239] != 0,
                          "explicit BG1 viewport inset retains visible span");
    }

    /* The parity renderer is also SMK's widescreen Mode 7 path. Live margins
     * must sample real map coordinates, while centered extra space remains
     * an exact native-width presentation for menus. */
    {
        enum { kExtra = 8, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels, 0);
        PpuSetExtraSpace(ppu, kExtra);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 7;
        ppu->screenEnabled[0] = 1;
        ppu->m7matrix[0] = 0x0100;
        ppu->m7matrix[3] = 0x0100;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0x0101;
        ppu->cgram[1] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] != 0 &&
                              wide_pixels[kExtra - 1] != 0 &&
                              wide_pixels[kExtra + kPpuXPixels] != 0 &&
                              wide_pixels[kWidePixels - 1] != 0,
                          "legacy Mode 7 renders live side margins");

        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuSetExtraSpaceCentered(ppu, kExtra);
        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] == 0 &&
                              wide_pixels[kExtra - 1] == 0 &&
                              wide_pixels[kExtra + kPpuXPixels] == 0 &&
                              wide_pixels[kWidePixels - 1] == 0,
                          "legacy Mode 7 preserves centered margins");
        failures += check(wide_pixels[kExtra] != 0 &&
                              wide_pixels[kExtra + kPpuXPixels - 1] != 0,
                          "legacy Mode 7 retains centered native columns");
    }

    /* Legacy-renderer titles use the same opt-in HUD anchor bands as the
     * current renderer. The inserted spans are transparent, while the source
     * pixels on either side retain their distance from the window edges. */
    {
        enum {
            kExtra = 8,
            kLeftEnd = 32,
            kRightStart = 224,
            kWidePixels = kPpuXPixels + kExtra * 2
        };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels, 0);
        PpuSetExtraSpace(ppu, kExtra);
        PpuSetWidescreenLayerAnchorBandSlot(
            ppu, 1, 1, 0, 2, kLeftEnd, kRightStart);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 1;
        ppu->screenEnabled[0] = 1 << 1;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(
            wide_pixels[0] != 0 &&
                wide_pixels[kLeftEnd - 1] != 0 &&
                wide_pixels[kExtra + kLeftEnd] != 0 &&
                wide_pixels[kExtra + kRightStart - 1] != 0 &&
                wide_pixels[kExtra * 2 + kRightStart] != 0 &&
                wide_pixels[kWidePixels - 1] != 0,
            "legacy HUD anchors preserve left, center, and right spans");
        failures += check(
            wide_pixels[kLeftEnd] == 0 &&
                wide_pixels[kExtra + kLeftEnd - 1] == 0 &&
                wide_pixels[kExtra + kRightStart] == 0 &&
                wide_pixels[kExtra * 2 + kRightStart - 1] == 0,
            "legacy HUD anchors leave inserted margin spans transparent");
    }

    /* A room boundary may deliberately leave the live world margins empty,
     * while a title-specific HUD still uses the full centering budget. Verify
     * both supported HUD sources: an anchored 4bpp background and explicitly
     * selected OAM slots. Unselected world sprites must remain centered. */
    {
        enum { kExtra = 8, kWidePixels = kPpuXPixels + kExtra * 2 };
        uint32_t wide_pixels[kWidePixels];

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpaceCentered(ppu, kExtra);
        PpuSetWidescreenLayerAnchorBand(ppu, 1, 0, 2, 32, 224);
        PpuSetWidescreenHudAlwaysVisible(ppu, true);
        ppu->inidisp = 0x0f;
        ppu->bgmode = 1;
        ppu->screenEnabled[0] = 1 << 1;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] != 0 &&
                              wide_pixels[kWidePixels - 1] != 0,
                          "always-visible BG HUD uses full centered margins");

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpaceCentered(ppu, kExtra);
        PpuSetWsHudOamBand(ppu, 2, 112, 160);
        PpuSetWsHudOamShiftRange(ppu, 0, 1);
        PpuSetWidescreenHudAlwaysVisible(ppu, true);
        ppu->inidisp = 0x0f;
        ppu->screenEnabled[0] = 1 << 4;
        for (int slot = 0; slot < 128; slot++)
            ppu->oam[slot * 2] = 0xf000;
        ppu->oam[0] = 0x0000;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] != 0 &&
                              wide_pixels[kExtra] == 0,
                          "selected HUD OAM shifts into centered margin");

        ppu_reset(ppu);
        memset(wide_pixels, 0, sizeof wide_pixels);
        PpuBeginDrawing(ppu, (uint8_t *)wide_pixels,
                        sizeof(uint32_t) * kWidePixels,
                        kPpuRenderFlags_NewRenderer);
        PpuSetExtraSpaceCentered(ppu, kExtra);
        PpuSetWsHudOamBand(ppu, 2, 112, 160);
        PpuSetWsHudOamShiftRange(ppu, 1, 1);
        PpuSetWidescreenHudAlwaysVisible(ppu, true);
        ppu->inidisp = 0x0f;
        ppu->screenEnabled[0] = 1 << 4;
        for (int slot = 0; slot < 128; slot++)
            ppu->oam[slot * 2] = 0xf000;
        ppu->oam[0] = 0x0000;
        for (size_t i = 0; i < sizeof ppu->vram / sizeof ppu->vram[0]; i++)
            ppu->vram[i] = 0xffff;
        ppu->cgram[0] = 0;
        for (size_t i = 1; i < sizeof ppu->cgram / sizeof ppu->cgram[0]; i++)
            ppu->cgram[i] = 0x7fff;

        ppu_runLine(ppu, 0);
        ppu_runLine(ppu, 1);
        failures += check(wide_pixels[0] == 0 &&
                              wide_pixels[kExtra] != 0,
                          "unselected world OAM remains centered");
    }

    ppu_free(ppu);
    if (failures) return 1;

    /* Mirror axis: reflect BG1 about screen columns -16 and 240 while the
     * rest of the wide line keeps its rendered columns. */
    {
        enum { kWideExtra = 43,
               kWidePitch = (kPpuXPixels + kWideExtra * 2) * 4 };
        static uint8_t wide_pixels[kWidePitch * 2];
        ppu_reset(ppu);
        PpuBeginDrawing(ppu, wide_pixels, kWidePitch,
                        kPpuRenderFlags_NewRenderer);
        ppu->inidisp = 0x0f;
        setup_mirror_axis_case(ppu);
        PpuSetExtraSpace(ppu, kWideExtra);
        PpuSetWidescreenLayerMask(ppu, 0x01);
        ppu_runLine(ppu, 1);
        int intact = 1;
        for (int x = -kWideExtra; x < kPpuXPixels + kWideExtra; x++)
            intact &= (bg_pixel(ppu, x) & 0xff) == expected_color(x);
        failures += check(intact, "wide BG1 line renders every column");

        PpuSetExtraSpace(ppu, kWideExtra);
        PpuSetWidescreenLayerMask(ppu, 0x01);
        PpuSetWidescreenLayerMirrorAxis(ppu, 0, -16, 240);
        ppu_runLine(ppu, 1);
        int mirrored = 1, kept = 1;
        for (int x = -kWideExtra; x < -16; x++)
            mirrored &= bg_pixel(ppu, x) == bg_pixel(ppu, -33 - x);
        for (int x = kPpuXPixels; x < kPpuXPixels + kWideExtra; x++)
            mirrored &= bg_pixel(ppu, x) == bg_pixel(ppu, 479 - x);
        for (int x = -16; x < kPpuXPixels; x++)
            kept &= (bg_pixel(ppu, x) & 0xff) == expected_color(x);
        failures += check(mirrored,
                          "margin columns past each axis reflect the far side");
        failures += check(kept, "columns between the axes and the native "
                                "view are untouched");
        failures += check((bg_pixel(ppu, -17) & 0xff) == expected_color(-16) &&
                          (bg_pixel(ppu, -43) & 0xff) == expected_color(10),
                          "reflection is exact about the axis");

        /* An axis one tile inside the native view reflects outward from that
         * line, skipping the authored edge strip, and still leaves every
         * native column alone. */
        PpuSetExtraSpace(ppu, kWideExtra);
        PpuSetWidescreenLayerMask(ppu, 0x01);
        PpuSetWidescreenLayerMirrorAxis(ppu, 0, 8, 248);
        ppu_runLine(ppu, 1);
        mirrored = 1;
        kept = 1;
        for (int x = -kWideExtra; x < 0; x++)
            mirrored &= bg_pixel(ppu, x) == bg_pixel(ppu, 15 - x);
        for (int x = kPpuXPixels; x < kPpuXPixels + kWideExtra; x++)
            mirrored &= bg_pixel(ppu, x) == bg_pixel(ppu, 495 - x);
        for (int x = 0; x < kPpuXPixels; x++)
            kept &= (bg_pixel(ppu, x) & 0xff) == expected_color(x);
        failures += check(mirrored && kept,
                          "an inset axis reflects into the margin only");

        /* An axis beyond the rendered span leaves that side alone, and the
         * policy resets with the others when the border is reapplied. */
        PpuSetExtraSpace(ppu, kWideExtra);
        PpuSetWidescreenLayerMask(ppu, 0x01);
        PpuSetWidescreenLayerMirrorAxis(ppu, 0, -1000, 1000);
        ppu_runLine(ppu, 1);
        intact = 1;
        for (int x = -kWideExtra; x < kPpuXPixels + kWideExtra; x++)
            intact &= (bg_pixel(ppu, x) & 0xff) == expected_color(x);
        failures += check(intact, "off-span axes change nothing");
        PpuSetWidescreenLayerMirrorAxis(ppu, 0, 0, 256);
        PpuSetExtraSpace(ppu, kWideExtra);
        failures += check(ppu->wsMirrorAxisMask == 0,
                          "mirror axes reset with the layer policies");
    }

    puts("ppu_sprite_limit_test: PASS");
    return 0;
}
