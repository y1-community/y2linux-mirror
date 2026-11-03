/* SPDX-License-Identifier: GPL-2.0-only
 * Host-only preview of the recovery screen: renders recovery-ui.c's draw()
 * into memory and writes a PPM. Never built for the device.
 * Usage: recovery-ui-preview OUT.ppm SCREEN-FILE "PERCENT STEP" ["TIME LINE" [PHASE]] */
#define main recovery_ui_main
#include "recovery-ui.c"
#undef main
int main(int argc, char **argv) {
    if (argc < 4 || argc > 6)
        return 2;
    static uint8_t pixels[480 * 360 * 4];
    Display d = {.fd = -1, .width = 480, .height = 360, .pitch = 480 * 4, .pixels = pixels};
    (void)present;
    char screen[1024] = {0};
    if (!slurp(argv[2], screen, sizeof(screen)))
        return 1;
    draw(&d, screen, argv[3], argc > 4 ? argv[4] : "", argc > 5 ? (unsigned)atoi(argv[5]) : 0);
    FILE *f = fopen(argv[1], "wb");
    if (!f)
        return 1;
    fprintf(f, "P6\n480 360\n255\n");
    for (unsigned i = 0; i < 480 * 360; i++) {
        uint32_t p = ((uint32_t *)pixels)[i];
        fputc(p >> 16 & 255, f);
        fputc(p >> 8 & 255, f);
        fputc(p & 255, f);
    }
    return fclose(f) ? 1 : 0;
}
