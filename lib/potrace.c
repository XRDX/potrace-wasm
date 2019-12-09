/* Copyright (C) 2001-2019 Peter Selinger.
   This file is part of Potrace. It is free software and it is covered
   by the GNU General Public License. See the file COPYING for details. */

#include <stdio.h>
#include <stdint.h>

#include "potrace.h"
#include "potracelib.h"
#include "bitmap.h"
#include "backend_svg.h"

#define UNDEF ((double)(1e30)) /* a value to represent "undefined" */

/* ---------------------------------------------------------------------- */
/* calculations with bitmap dimensions, positioning etc */

/* determine the dimensions of the output based on command line and
   image dimensions, and optionally, based on the actual image outline. */
static void calc_dimensions(imginfo_t *imginfo, potrace_path_t *plist)
{
    /* we take care of a special case: if one of the image dimensions is
     0, we change it to 1. Such an image is empty anyway, so there
     will be 0 paths in it. Changing the dimensions avoids division by
     0 error in calculating scaling factors, bounding boxes and
     such. This doesn't quite do the right thing in all cases, but it
     is better than causing overflow errors or "nan" output in
     backends.  Human users don't tend to process images of size 0
     anyway; they might occur in some pipelines. */
    if (imginfo->pixwidth == 0)
    {
        imginfo->pixwidth = 1;
    }
    if (imginfo->pixheight == 0)
    {
        imginfo->pixheight = 1;
    }

    /* apply default dimension to width, height, margins */
    imginfo->lmar = 0;
    imginfo->rmar = 0;
    imginfo->tmar = 0;
    imginfo->bmar = 0;

    /* start with a standard rectangle */
    trans_from_rect(&imginfo->trans, imginfo->pixwidth, imginfo->pixheight);

    /* if width and height are still variable, tenatively use the
     default scaling factor of 72dpi (for dimension-based backends) or
     1 (for pixel-based backends). For fixed-size backends, this will
     be adjusted later to fit the page. */
    imginfo->width = imginfo->trans.bb[0];
    imginfo->height = imginfo->trans.bb[1];

    /* apply scaling */
    trans_scale_to_size(&imginfo->trans, imginfo->width, imginfo->height);
}

const char *convert_svg(uint8_t pixels[], int width, int height)
{
    // initialize the bitmap with given pixels.
    potrace_bitmap_t *bm = bm_new(width, height);
    for (int i = 0; i < width * height * 4; i += 4)
    {
        int index = i / 4;
        int x = index % width;
        int y = height - (index / width) - 1;

        // Relative luminance
        int color = 0.2126 * pixels[i] + 0.7152 * pixels[i + 1] + 0.0722 * pixels[i + 2];
        int a = pixels[i + 3];
        BM_UPUT(bm, x, y, a != 0 && color < 128);
    }

    // start the potrace algorithm.
    potrace_param_t param = {
        .turdsize = 2,
        .turnpolicy = 4,
        .alphamax = 1,
        .opticurve = 1,
        .opttolerance = 0.2,
    };

    potrace_state_t *st = potrace_trace(&param, bm);
    if (!st || st->status != POTRACE_STATUS_OK)
    {
        fprintf(stderr, "trace error: %s\n", strerror(errno));
        exit(2);
    }

    // conver the trace to image information for svg backend generation.
    imginfo_t imginfo = {
        .pixwidth = bm->w,
        .pixheight = bm->h,
    };
    bm_free(bm);

    // calculte the dimension of image.
    calc_dimensions(&imginfo, st->plist);

    // start convert to svg.
    char *buf;
    size_t len;
    FILE *stream = open_memstream(&buf, &len);
    int r = page_svg(stream, st->plist, &imginfo);
    if (r)
    {
        fprintf(stderr, "page_svg error: %s\n", strerror(errno));
        exit(2);
    }

    // release resource.
    fclose(stream);
    potrace_state_free(st);

    return buf;
}