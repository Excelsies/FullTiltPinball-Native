/* Show images on top of everything (EmulationStation and games) on the Pi's DispmanX display.
 * build: gcc -O2 -Wall -o overlay overlay.c -I/opt/vc/include -L/opt/vc/lib -lbcm_host
 * usage: overlay   then commands on stdin, one per line:
 *   show FILE   show FILE centred (FILE: uint32 width, uint32 height, then RGBA rows)
 *   toast FILE  show FILE at the bottom of the screen
 *   hide        remove the image
 * Images larger than the current video mode (emulators change it) are scaled down to fit.
 * Exits (removing the image) at end of input. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bcm_host.h"

#define LAYER 2000000 /* above EmulationStation, its preview videos (omxplayer) and emulators */

static DISPMANX_DISPLAY_HANDLE_T display;
static DISPMANX_MODEINFO_T info;
static DISPMANX_ELEMENT_HANDLE_T element;
static DISPMANX_RESOURCE_HANDLE_T resource;

static void hide(void)
{
    if (element) {
        DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
        vc_dispmanx_element_remove(update, element);
        vc_dispmanx_update_submit_sync(update);
        element = 0;
    }
    if (resource) {
        vc_dispmanx_resource_delete(resource);
        resource = 0;
    }
}

static int show(const char *path, int bottom)
{
    FILE *in = fopen(path, "rb");
    if (!in) { perror(path); return 1; }
    uint32_t size[2];
    if (fread(size, 4, 2, in) != 2 || !size[0] || !size[1] || size[0] > 4096 || size[1] > 4096) {
        fprintf(stderr, "%s: bad header\n", path);
        fclose(in);
        return 1;
    }
    /* The GPU wants rows padded to 16 pixels: read each row into a padded buffer. */
    int w = size[0], h = size[1], pitch = ALIGN_UP(w, 16) * 4;
    unsigned char *pixels = calloc(h, pitch);
    int y = 0;
    while (pixels && y < h && fread(pixels + (size_t)y * pitch, w * 4, 1, in) == 1)
        y++;
    fclose(in);
    if (y != h) { fprintf(stderr, "%s: short image\n", path); free(pixels); return 1; }

    uint32_t handle;
    DISPMANX_RESOURCE_HANDLE_T res = vc_dispmanx_resource_create(VC_IMAGE_RGBA32, w, h, &handle);
    VC_RECT_T rect;
    vc_dispmanx_rect_set(&rect, 0, 0, w, h);
    if (!res || vc_dispmanx_resource_write_data(res, VC_IMAGE_RGBA32, pitch, pixels, &rect)) {
        fprintf(stderr, "%s: cannot load into the GPU\n", path);
        free(pixels);
        return 1;
    }
    free(pixels);

    /* Re-read the video mode each time: an emulator may have changed it since the last pane. */
    if (vc_dispmanx_display_get_info(display, &info)) {
        fprintf(stderr, "cannot read the display mode\n");
        vc_dispmanx_resource_delete(res);
        return 1;
    }
    double scale = 1.0;
    if (w > info.width * 0.95) scale = info.width * 0.95 / w;
    if (h * scale > info.height * 0.95) scale = info.height * 0.95 / h;
    int dw = (int)(w * scale), dh = (int)(h * scale);
    int top = bottom ? info.height - dh - info.height / 14 : (info.height - dh) / 2;
    VC_RECT_T src, dst;
    vc_dispmanx_rect_set(&src, 0, 0, w << 16, h << 16);
    vc_dispmanx_rect_set(&dst, (info.width - dw) / 2, top, dw, dh);
    VC_DISPMANX_ALPHA_T alpha = {DISPMANX_FLAGS_ALPHA_FROM_SOURCE, 255, 0};
    DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);
    DISPMANX_ELEMENT_HANDLE_T el = vc_dispmanx_element_add(update, display, LAYER, &dst, res, &src,
                                                           DISPMANX_PROTECTION_NONE, &alpha, NULL,
                                                           DISPMANX_NO_ROTATE);
    if (element)  /* swap in one update: no flicker between panes */
        vc_dispmanx_element_remove(update, element);
    vc_dispmanx_update_submit_sync(update);
    if (resource)
        vc_dispmanx_resource_delete(resource);
    element = el;
    resource = res;
    return el ? 0 : 1;
}

int main(void)
{
    bcm_host_init();
    display = vc_dispmanx_display_open(0);
    if (!display || vc_dispmanx_display_get_info(display, &info)) {
        fprintf(stderr, "cannot open the display\n");
        return 1;
    }
    char line[1024];
    int failed = 0;
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (!strncmp(line, "show ", 5))
            failed |= show(line + 5, 0);
        else if (!strncmp(line, "toast ", 6))
            failed |= show(line + 6, 1);
        else if (!strcmp(line, "hide"))
            hide();
        else {
            fprintf(stderr, "unknown command: %s\n", line);
            failed = 1;
        }
        printf("%s\n", failed ? "error" : "ok");
        fflush(stdout);
        failed = 0;
    }
    hide();
    vc_dispmanx_display_close(display);
    return 0;
}
