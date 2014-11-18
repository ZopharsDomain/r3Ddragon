#include <malloc.h>
#include "allegro_compat.h"

void masked_blit(BITMAP *src, BITMAP *dst, int src_x, int src_y, int dst_x, int dst_y, int w, int h) {
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            if (src->line[(src_y+y)%(src->h)][(src_x+x)%(src->w)]) {
                dst->line[(dst_y+y)%(dst->h)][(dst_x+x)%(dst->w)] = src->line[(src_y+y)%(src->h)][(src_x+x)%(src->w)];
            }
        }
    }
}

void masked_stretch_blit(BITMAP *source, BITMAP *dest, int source_x, int source_y, int source_w, int source_h, int dest_x, int dest_y, int dest_w, int dest_h) {
    masked_blit(source, dest, source_x, source_y, dest_x, dest_y, source_w, source_h);
}

BITMAP *create_bitmap(int w, int h) {
    BITMAP *bm = (BITMAP*)malloc(sizeof(BITMAP));

    bm->w = w;
    bm->h = h;

    bm->line = malloc(h*sizeof(uint8_t*));
    int i;
//    for (i = 0; i < h; i++) {
//        bm->line[i] = (uint8_t*)malloc(w*sizeof(uint8_t));
//    }
    bm->dat = malloc(w * h * sizeof(char));
    if (h > 0) {
        bm->line[0] = bm->dat;
        for (i=1; i<h; i++)
            bm->line[i] = bm->line[i-1] + w;
    }

    return bm;

//    return create_bitmap_ex(8, w, h);
}

void destroy_bitmap(BITMAP *bitmap) {
    int i;
    for (i = 0; i < (bitmap->h); i++) {
        free(bitmap->line[i]);
    }
    free(bitmap->line);
    free(bitmap);
}

BITMAP *create_sub_bitmap(BITMAP *parent, int x, int y, int width, int height) {
    return 0;
}

void clear_to_color(BITMAP *bitmap, int color) {
    int x, y;

    if (!bitmap) {
        return;
    }

    for (y = 0; y < bitmap->h; y++){
        for (x = 0; x < bitmap->w; x++){
            bitmap->line[y][x] = (color&0b11)*0xff + (color&0b11100)*0xff00 + (color&0b11100000)*0xff0000;
        }
    }
}