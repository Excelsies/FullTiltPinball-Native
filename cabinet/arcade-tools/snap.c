/* Snapshot the composited Pi display (all DispmanX layers, including GLES) to a PPM file.
 * build: gcc -O2 -o snap snap.c -I/opt/vc/include -L/opt/vc/lib -lbcm_host
 * usage: snap out.ppm */
#include <stdio.h>
#include <stdlib.h>
#include "bcm_host.h"

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: snap out.ppm\n"); return 2; }
    bcm_host_init();
    DISPMANX_DISPLAY_HANDLE_T display = vc_dispmanx_display_open(0);
    if (!display) { fprintf(stderr, "cannot open display\n"); return 1; }
    DISPMANX_MODEINFO_T info;
    if (vc_dispmanx_display_get_info(display, &info)) { fprintf(stderr, "no display info\n"); return 1; }
    int w = info.width, h = info.height, pitch = ALIGN_UP(w * 3, 32);
    uint32_t handle;
    DISPMANX_RESOURCE_HANDLE_T res = vc_dispmanx_resource_create(VC_IMAGE_RGB888, w, h, &handle);
    if (!res) { fprintf(stderr, "cannot create resource\n"); return 1; }
    if (vc_dispmanx_snapshot(display, res, DISPMANX_NO_ROTATE)) { fprintf(stderr, "snapshot failed\n"); return 1; }
    unsigned char *buf = malloc((size_t)pitch * h);
    VC_RECT_T rect;
    vc_dispmanx_rect_set(&rect, 0, 0, w, h);
    if (!buf || vc_dispmanx_resource_read_data(res, &rect, buf, pitch)) { fprintf(stderr, "read failed\n"); return 1; }
    FILE *out = fopen(argv[1], "wb");
    if (!out) { perror(argv[1]); return 1; }
    fprintf(out, "P6\n%d %d\n255\n", w, h);
    for (int y = 0; y < h; y++)
        if (fwrite(buf + (size_t)y * pitch, 3, w, out) != (size_t)w) { perror("write"); return 1; }
    if (fclose(out)) { perror("close"); return 1; }
    vc_dispmanx_resource_delete(res);
    vc_dispmanx_display_close(display);
    return 0;
}
