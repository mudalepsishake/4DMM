/* BRender:
 * Copyright (c) 1993-1995 Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: prepmap.c 1.2 1998/09/25 15:37:40 johng Exp $
 * $Locker: $
 *
 * Precompute information for texture maps
 */
#include "v1db.h"
#include "brassert.h"

void BR_PUBLIC_ENTRY BrMapUpdate(br_pixelmap *map, br_uint_16 flags)
{
    UASSERT_MESSAGE("Invalid BrMapUpdate pointer", map != NULL);
    BrBufferUpdate(map, BRT_COLOUR_MAP_O, flags);
}

#if BRENDER_LEGACY_3DMM_MODEL_ABI
/*
 * v243: 3DMM's application-owned legacy pixelmaps cannot safely carry the
 * renderer-private `stored` pointer in-band.  Movie/scene code can overwrite
 * that public structure after registration; v242 proved that the field can
 * change from a valid br_buffer_stored pointer to arbitrary application data
 * while the renderer is alive.  Keep the authoritative association out of the
 * pixelmap and mirror it back only for compatibility with untouched code.
 */
typedef struct br_legacy_buffer_binding {
    br_pixelmap *pixelmap;
    br_buffer_stored *stored;
    struct br_legacy_buffer_binding *next;
} br_legacy_buffer_binding;

static br_legacy_buffer_binding *legacy_buffer_bindings;

static br_legacy_buffer_binding *LegacyBufferBindingFind(br_pixelmap *pm)
{
    br_legacy_buffer_binding *binding;

    for(binding = legacy_buffer_bindings; binding != NULL; binding = binding->next)
        if(binding->pixelmap == pm)
            return binding;

    return NULL;
}

static br_buffer_stored *LegacyBufferStoredGet(br_pixelmap *pm)
{
    br_legacy_buffer_binding *binding = LegacyBufferBindingFind(pm);
    return binding != NULL ? binding->stored : NULL;
}

static void LegacyBufferStoredSet(br_pixelmap *pm, br_buffer_stored *stored)
{
    br_legacy_buffer_binding *binding = LegacyBufferBindingFind(pm);

    if(stored == NULL) {
        br_legacy_buffer_binding **link = &legacy_buffer_bindings;

        while(*link != NULL) {
            if((*link)->pixelmap == pm) {
                binding = *link;
                *link = binding->next;
                BrMemFree(binding);
                break;
            }
            link = &(*link)->next;
        }
        pm->stored = NULL;
        return;
    }

    if(binding == NULL) {
        binding = BrMemAllocate(sizeof(*binding), BR_MEMORY_APPLICATION);
        if(binding == NULL)
            BR_FAILURE0("Could not allocate legacy pixelmap stored binding");

        binding->pixelmap = pm;
        binding->next = legacy_buffer_bindings;
        legacy_buffer_bindings = binding;
    }

    binding->stored = stored;
    pm->stored = stored;
}
#endif

br_buffer_stored *BrBufferStoredGet(br_pixelmap *pm)
{
#if BRENDER_LEGACY_3DMM_MODEL_ABI
    return LegacyBufferStoredGet(pm);
#else
    return pm != NULL ? pm->stored : NULL;
#endif
}

void BrBufferUpdate(br_pixelmap *pm, br_token use, br_uint_16 flags)
{
    br_token_value tv[] = {
        {BRT_PREFER_SHARE_B, {.b = BR_FALSE}},
        {BRT_CAN_SHARE_B,    {.b = BR_TRUE} },
        {BRT_UPDATE_DATA_B,  {.b = BR_FALSE}},
        {0,                  0              },
    };
    br_buffer_stored *stored;

    ASSERT_MESSAGE("Invalid BrBufferUpdate pointer", pm != NULL);
    if(v1db.renderer == NULL) {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        LegacyBufferStoredSet(pm, NULL);
#else
        pm->stored = NULL;
#endif
        return;
    }

    if(flags & BR_MAPU_SHARED)
        tv[0].v.b = BR_TRUE;

    if(flags & BR_MAPU_DATA)
        tv[2].v.b = BR_TRUE;

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    stored = LegacyBufferStoredGet(pm);
    if(stored != NULL) {
        if(pm->stored != stored) {
            static br_uint_32 mismatch_count;
            if(mismatch_count < 64) {
                BrWarning("3DMM v243 pixelmap stored overwrite repaired pm=%p raw=%p tracked=%p",
                          (void *)pm, (void *)pm->stored, (void *)stored);
                ++mismatch_count;
            }
        }
        pm->stored = stored;
        BufferStoredUpdate(stored, (br_device_pixelmap *)pm, tv);
    } else {
        stored = NULL;
        RendererBufferStoredNew(v1db.renderer, &stored, use, (br_device_pixelmap *)pm, tv);
        if(stored != NULL)
            LegacyBufferStoredSet(pm, stored);
        else
            pm->stored = NULL;
    }
#else
    if(pm->stored)
        BufferStoredUpdate(pm->stored, (br_device_pixelmap *)pm, tv);
    else
        RendererBufferStoredNew(v1db.renderer, (br_buffer_stored **)&pm->stored, use, (br_device_pixelmap *)pm, tv);
#endif
}

void BrBufferClear(br_pixelmap *pm)
{
#if BRENDER_LEGACY_3DMM_MODEL_ABI
    br_buffer_stored *stored = LegacyBufferStoredGet(pm);

    if(stored != NULL) {
        if(pm->stored != stored) {
            static br_uint_32 clear_mismatch_count;
            if(clear_mismatch_count < 64) {
                BrWarning("3DMM v243 pixelmap clear ignored corrupt raw stored pm=%p raw=%p tracked=%p",
                          (void *)pm, (void *)pm->stored, (void *)stored);
                ++clear_mismatch_count;
            }
        }
        ObjectFree(stored);
    }

    LegacyBufferStoredSet(pm, NULL);
#else
    if(pm->stored) {
        ObjectFree(pm->stored);
        pm->stored = NULL;
    }
#endif
}
