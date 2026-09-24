
/* BRenderModern: brender headers */
#include "brender.h"
#include "fmt.h"
#include "brstb.h"
#include <limits.h>

/* BRenderModern: stb implementation */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

/* BRenderModern: load with stb_image (private) */
static br_pixelmap *BrFmtSTBLoad(const char *name)
{
    /* BRenderModern: variables */
    br_pixelmap *pixelmap, *tmp;
    int          x, y, c;
    stbi_uc     *pixels;
    void        *file;

    /* BRenderModern: open file */
    file = BrFileOpenRead(name, 0, NULL, NULL);
    if(file == NULL) {
        BrLogError("FMT", "Failed to open \"%s\"", name);
        return NULL;
    }

    /* BRenderModern:
     * Get pixels, always request 4 components.
     * Makes our lives easier.
     */
    pixels = stbi_load_from_callbacks(&br_stb_file_cbfns, file, &x, &y, &c, 4);
    if(pixels == NULL) {
        BrLogError("FMT", "Failed to process \"%s\". Reason: %s", name, stbi_failure_reason());
        return NULL;
    }

    /* BRenderModern: close file */
    BrFileClose(file);

    /* BRenderModern:
     * pixels isn't a resource so we can't use it directly.
     * Instead, shove it in a temporary pixelmap, then convert it to BR_PMT_RGBA_8888.
     */
    tmp             = BrPixelmapAllocate(BR_PMT_RGBA_8888_ARR, x, y, pixels, BR_PMAF_NORMAL);
    tmp->identifier = (char *)name; /* BRenderModern: NB: This is safe, the clone below will copy it. */

    pixelmap = BrPixelmapCloneTyped(tmp, BR_PMT_RGBA_8888);

    BrPixelmapFree(tmp);
    BrMemFree(pixels);

    /* BRenderModern: return ptr */
    return pixelmap;
}

/* BRenderModern: load PNG directly from an encoded in-memory buffer. */
br_pixelmap *BR_PUBLIC_ENTRY BrFmtPNGLoadMemory(const void *data, br_size_t size)
{
    br_pixelmap *pixelmap, *tmp;
    int          x, y, c;
    stbi_uc     *pixels;

    if(data == NULL || size == 0 || size > INT_MAX)
        return NULL;

    pixels = stbi_load_from_memory((const stbi_uc *)data, (int)size, &x, &y, &c, 4);
    if(pixels == NULL) {
        BrLogError("FMT", "Failed to process PNG memory buffer. Reason: %s", stbi_failure_reason());
        return NULL;
    }

    tmp = BrPixelmapAllocate(BR_PMT_RGBA_8888_ARR, x, y, pixels, BR_PMAF_NORMAL);
    if(tmp == NULL) {
        BrMemFree(pixels);
        return NULL;
    }
    tmp->identifier = (char *)"4DMM VXP2 PNG";
    pixelmap = BrPixelmapCloneTyped(tmp, BR_PMT_RGBA_8888);

    BrPixelmapFree(tmp);
    BrMemFree(pixels);
    return pixelmap;
}

/* BRenderModern: load png */
br_pixelmap *BR_PUBLIC_ENTRY BrFmtPNGLoad(const char *name, br_uint_32 flags)
{
    return BrFmtSTBLoad(name);
}

/* BRenderModern: load jpg */
br_pixelmap *BR_PUBLIC_ENTRY BrFmtJPGLoad(const char *name, br_uint_32 flags)
{
    return BrFmtSTBLoad(name);
}

br_pixelmap *BR_PUBLIC_ENTRY BrFmtGIFLoad(const char *name, br_uint_32 flags)
{
    return BrFmtSTBLoad(name);
}
