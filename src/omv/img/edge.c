/*
 * This file is part of the OpenMV project.
 * Copyright (c) 2013/2014 Ibrahim Abdelkader <i.abdalkader@gmail.com>
 * This work is licensed under the MIT license, see the file LICENSE for details.
 *
 * Edge Detection.
 *
 */
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "imlib.h"
#include "fb_alloc.h"

typedef struct gvec {
    uint16_t t;
    uint16_t g;
} gvec_t;

static const int8_t kernel_high_pass_33[] = {
    -1, -1, -1,
    -1, +8, -1,
    -1, -1, -1
};

static const int8_t kernel_gauss_55[] = {
    2, 4,  5,  4,  2,
    4, 9,  12, 9,  4,
    5, 12, 15, 12, 5,
    4, 9,  12, 9,  4,
    2, 4,  5,  4,  2,
};


void imlib_edge_simple(image_t *src, rectangle_t *roi, int low_thresh, int high_thresh)
{
    imlib_morph(src, 1, kernel_high_pass_33, 1.0f, 0.0f);
    simple_color_t lt = {.G=low_thresh};
    simple_color_t ht = {.G=high_thresh};
    imlib_binary(src, 1, &lt, &ht, false);
    imlib_erode(src, 1, 2);
}

void imlib_edge_canny(image_t *src, rectangle_t *roi, int low_thresh, int high_thresh)
{
    int w = src->w;

    gvec_t *gm = fb_alloc0(src->w*src->h*sizeof*gm);

    //1. Noise Reduction with a 5x5 Gaussian filter
    imlib_morph(src, 2, kernel_gauss_55, 1.0f/159.0f, 0.0f);

    //2. Finding Image Gradients
    for (int y=roi->y+1; y<roi->y+roi->h-1; y++) {
        for (int x=roi->x+1; x<roi->x+roi->w-1; x++) {
            int vx=0, vy=0;
            // sobel kernel in the horizontal direction
            vx  = src->data [(y-1)*w+x-1]
                - src->data [(y-1)*w+x+1]
                + (src->data[(y+0)*w+x-1]<<1)
                - (src->data[(y+0)*w+x+1]<<1)
                + src->data [(y+1)*w+x-1]
                - src->data [(y+1)*w+x+1];

            // sobel kernel in the vertical direction
            vy  = src->data [(y-1)*w+x-1]
                + (src->data[(y-1)*w+x+0]<<1)
                + src->data [(y-1)*w+x+1]
                - src->data [(y+1)*w+x-1]
                - (src->data[(y+1)*w+x+0]<<1)
                - src->data [(y+1)*w+x+1];

            // Find magnitude
            int g = (int) fast_sqrtf(vx*vx + vy*vy);
            // Find the direction and round angle to 0, 45, 90 or 135
            int t = (int) fast_fabsf((atan2f(vy, vx)*180.0f/M_PI));
            if (t < 22) {
                t = 0;
            } else if (t < 67) {
                t = 45;
            } else if (t < 112) {
                t = 90;
            } else if (t < 160) {
                t = 135;
            } else if (t <= 180) {
                t = 0;
            }

            gm[(y)*w+(x)].t = t;
            gm[(y)*w+(x)].g = g;
        }
    }

    // 3. Hysteresis Thresholding
    for (int y=roi->y+1; y<roi->y+roi->h-1; y++) {
        for (int x=roi->x+1; x<roi->x+roi->w-1; x++) {
            gvec_t *vc = &gm[y*w+x];
            if (vc->g >= high_thresh) {
                vc->g = vc->g;
            } else if (vc->g < low_thresh) {
                vc->g = 0;
            } else if (gm[(y-1)*w+(x-1)].g >= high_thresh ||
                       gm[(y-1)*w+(x+0)].g >= high_thresh ||
                       gm[(y-1)*w+(x+1)].g >= high_thresh ||
                       gm[(y+0)*w+(x-1)].g >= high_thresh ||
                       gm[(y+0)*w+(x+1)].g >= high_thresh ||
                       gm[(y+1)*w+(x-1)].g >= high_thresh ||
                       gm[(y+1)*w+(x+0)].g >= high_thresh ||
                       gm[(y+1)*w+(x+1)].g >= high_thresh) {
                vc->g = vc->g;
            } else {
                vc->g = 0;
            }
        }
    }

    // 4. Non-maximum Suppression and output
    for (int y=roi->y; y<roi->y+roi->h; y++) {
        for (int x=roi->x; x<roi->x+roi->w; x++) {
            int i = y*w+x;
            gvec_t *va=NULL, *vb=NULL, *vc = &gm[i];

            if (y < (roi->y+2) || y > (roi->y+roi->h-3) ||
                x < (roi->x+2) || x > (roi->x+roi->w-3)) {
                src->data[i] = 0;
                continue;
            }

            switch (vc->t) {
                case 0: {
                    va = &gm[(y+0)*w+(x-1)];
                    vb = &gm[(y+0)*w+(x+1)];
                    break;
                }

                case 45: {
                    va = &gm[(y+1)*w+(x-1)];
                    vb = &gm[(y-1)*w+(x+1)];
                    break;
                }

                case 90: {
                    va = &gm[(y+1)*w+(x+0)];
                    vb = &gm[(y-1)*w+(x+0)];
                    break;
                }

                case 135: {
                    va = &gm[(y+1)*w+(x+1)];
                    vb = &gm[(y-1)*w+(x-1)];
                    break;
                }
            }

            if (!(vc->g > va->g && vc->g > vb->g)) {
                src->data[i] = 0;
            } else {
                src->data[i] = 255;
            }
        }
    }

    fb_free();
}
