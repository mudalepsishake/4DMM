/* BRender:
 * Copyright (c) 1993-1997 by Argonaut Technologies Limited. All rights reserved.
 *
 * $Id: loadvue.c 1.1 1997/12/10 16:41:02 jon Exp $
 * $Locker: $
 *
 * Loader for vue format
 *
 * TODO hierarchy support
 */
#include "brender.h"
#include "fmt.h"

// BRender: ***************************************************************
// BRender: Allocate a vue
// BRender: ***************************************************************
br_vue *BR_PUBLIC_ENTRY BrVueAllocate(int nframes, int ntransforms)
{
    br_vue *vue;

    // BRender: UASSERT(nframes >= 0);
    // BRender: UASSERT(ntransforms >= 0);

    vue = BrResAllocate(NULL, sizeof(*vue), BR_MEMORY_SCRATCH);

    vue->frames             = BrResAllocate(vue, sizeof(*vue->frames) * nframes, BR_MEMORY_SCRATCH);
    vue->frames->transforms = BrResAllocate(vue, sizeof(*vue->frames->transforms) * ntransforms, BR_MEMORY_SCRATCH);

    vue->nframes     = nframes;     // BRender: max number of frames
    vue->ntransforms = ntransforms; // BRender: max number of transforms per frame
    vue->frame_delta = 1;           // BRender: default frame step
    return vue;
}
// BRender: ***************************************************************

// BRender: ***************************************************************
// BRender: Free a vue
// BRender: ***************************************************************
void BR_PUBLIC_ENTRY BrVueFree(br_vue *vue)
{
    BrResFree(vue);
}
// BRender: ***************************************************************

// BRender: ***************************************************************
// BRender: Grutuitously ripped off from robot.c!
// BRender: ***************************************************************
void BR_PUBLIC_ENTRY BrLoadVUE(const char *file_name, br_actor *root, br_vue *vue)
{
    void                 *fileh;
    int                   total_frames;
    static int            first = 0;
    int                   f     = -1, i;
    char                  buff[1024];
    char                  name[80];
    float                 v[4][3];
    struct vue_transform *t = vue->frames->transforms;

    fileh = BrFileOpenRead(file_name, 0, NULL, NULL);

    if(fileh == NULL)
        BR_ERROR1("Could not open '%s' for reading", file_name);

    while(!BrFileEof(fileh)) {
        BrFileGetLine(buff, 1024, fileh);

        /* BRender: Is it a frame specification line? */

        if(BrSScanf(buff, "frame %d", &i) > 0) {
            if(f >= 0)
                vue->frames[f].ntransforms = t - vue->frames[f].transforms;
            else
                first = i;

            f                         = i - first;
            vue->frames[f].transforms = t;
        }
        /* BRender: Or a transform specification? */

        if(BrSScanf(buff, "transform \"%[^\"]\" %f %f %f %f %f %f %f %f %f %f %f %f", name, &v[0][0], &v[0][1], &v[0][2], &v[1][0],
                    &v[1][1], &v[1][2], &v[2][0], &v[2][1], &v[2][2], &v[3][0], &v[3][1], &v[3][2]) > 0) {
            int k, l;

            /* BRender: Found a transform so convert it to scalars */

            for(k = 0; k < 4; k++)
                for(l = 0; l < 3; l++)
                    t->mat.m[k][l] = BrFloatToScalar(v[k][l]);

            // BRender: Corelates frame identifier with actor identifier.

            t->actor = BrActorSearch(root, name);

            if(t->actor != NULL)
                t++;
        }
    }

    BrFileClose(fileh);

    if(f > 0)
        vue->frames[f].ntransforms = t - vue->frames[f].transforms;

    total_frames = f + 1;

    vue->nframes = total_frames;
}
// BRender: ***************************************************************

// BRender: ***************************************************************
// BRender: Yes you guessed it, more grutuitous rip offs from robot.c!
// BRender: ***************************************************************
// BRender: does the animation
// BRender: ***************************************************************
void BR_PUBLIC_ENTRY BrApplyVue(br_vue *vue, br_actor *actors)
{

    int                 i;
    static br_transform store_root_transform;

    int total_frames = vue->nframes;
    int frame_delta  = vue->frame_delta;

    // BRender: Store current root actor transform.

    BrTransformToTransform(&store_root_transform, &(actors->t));

    // BRender: Play VUE file by loading transforms into actors.

    if(total_frames != 0) {
        // BRender: Clamp frame number to valid range.

        if(vue->current_frame < 0)
            vue->current_frame = total_frames - 1;

        if(vue->current_frame >= total_frames)
            vue->current_frame = 0;

        // BRender: Apply animation to world.
        if(frame_delta != 0)
            for(i = 0; i < vue->frames[vue->current_frame].ntransforms; i++)
                vue->frames[vue->current_frame].transforms[i].actor->t.t.mat = vue->frames[vue->current_frame].transforms[i].mat;

        // BRender: Move to next frame.

        vue->current_frame += frame_delta;
    }

    // BRender: Restore current world transform.
    BrTransformToTransform(&(actors->t), &store_root_transform);
}
// BRender: ***************************************************************
