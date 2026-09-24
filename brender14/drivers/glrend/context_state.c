#include <string.h>
#include "drv.h"

static void setup_quirks(br_gl_context_state *ctx)
{
    ctx->quirks.value = 0;

    /* BRenderModern:
     * Disable anisotropic filtering on llvmpipe. It is _slow_.
     */
    if(BrStrCmp(ctx->gl_vendor, "Mesa") == 0 && strstr(ctx->gl_renderer, "llvmpipe (") == ctx->gl_renderer) {
        BrLogInfo("GLREND", "Quirk - using llvmpipe, disabling anisotropic filtering.");
        ctx->quirks.disable_anisotropic_filtering = 1;
    }

    /* BRenderModern:
     * glBufferSubData() causes a pipeline flush on macOS (goddamnit Apple), so force orphaning the buffers instead.
     * https://mojira.dev/MC-295893
     */
    if(strstr(ctx->gl_renderer, "Apple M") == ctx->gl_renderer) {
        BrLogInfo("GLREND", "Quirk - using Apple Silicon, forcing model uniform buffer orphaning.");
        ctx->quirks.orphan_model_buffers = 1;
    }
}

static char **build_extensions_list(void *vparent, const GladGLContext *gl, GLint *p_num_extensions)
{
    char **extensions     = NULL;
    GLint  num_extensions = 0;

    gl->GetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);

    extensions = BrResAllocate(vparent, sizeof(char *) * (num_extensions + 1), BR_MEMORY_DRIVER);
    for(GLuint i = 0; i < num_extensions; ++i) {
        const GLubyte *ext = gl->GetStringi(GL_EXTENSIONS, i);
        extensions[i]      = BrResStrDup(extensions, (const char *)ext);
    }
    extensions[num_extensions] = NULL;

    *p_num_extensions = num_extensions;
    return extensions;
}

static void GLAD_API_PTR gl_debug_callback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *message,
                                           const void *user)
{
    const char *source_string, *type_string, *severity_string;

    (void)user;

    // BRenderModern: clang-format off
    switch(source) {
        case GL_DEBUG_SOURCE_API:             source_string = "API";             break;
        case GL_DEBUG_SOURCE_OTHER:           source_string = "OTHER";           break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:     source_string = "THIRD_PARTY";     break;
        case GL_DEBUG_SOURCE_APPLICATION:     source_string = "APPLICATION";     break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   source_string = "WINDOW_SYSTEM";   break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: source_string = "SHADER_COMPILER"; break;
        default:                              source_string = "UNKNOWN";         break;
    }

    switch(type) {
        case GL_DEBUG_TYPE_ERROR:               type_string = "ERROR";               break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: type_string = "DEPRECATED_BEHAVIOR"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  type_string = "UNDEFINED_BEHAVIOR";  break;
        case GL_DEBUG_TYPE_PORTABILITY:         type_string = "PORTABILITY";         break;
        case GL_DEBUG_TYPE_PERFORMANCE:         type_string = "PERFORMANCE";         break;
        case GL_DEBUG_TYPE_MARKER:              type_string = "MARKER";              break;
        case GL_DEBUG_TYPE_PUSH_GROUP:          type_string = "PUSH_GROUP";          break;
        case GL_DEBUG_TYPE_POP_GROUP:           type_string = "POP_GROUP";           break;
        case GL_DEBUG_TYPE_OTHER:               type_string = "OTHER";               break;
        default:                                type_string = "UNKNOWN";             break;
    }

    switch(severity) {
        case GL_DEBUG_SEVERITY_LOW:          severity_string = "LOW";          break;
        case GL_DEBUG_SEVERITY_MEDIUM:       severity_string = "MEDIUM";       break;
        case GL_DEBUG_SEVERITY_HIGH:         severity_string = "HIGH";         break;
        case GL_DEBUG_SEVERITY_NOTIFICATION: severity_string = "NOTIFICATION"; break;
        default:                             severity_string = "UNKNOWN";      break;
    }
    // BRenderModern: clang-format on

    if(length < 0) {
        BrLogDebug("GLREND", "glDebug: source=%s, type=%s, id=%u, severity=%s: %.*s", source_string, type_string, id, severity_string,
                   (int)length, message);
    } else {
        BrLogDebug("GLREND", "glDebug: source=%s, type=%s, id=%u, severity=%s: %s", source_string, type_string, id, severity_string, message);
    }
}

static GLuint build_white_texture(const GladGLContext *gl)
{
    const static uint8_t white_rgba[] = {255, 255, 255, 255};

    GLuint tex;

    gl->GenTextures(1, &tex);

    gl->BindTexture(GL_TEXTURE_2D, tex);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white_rgba);

    DeviceGLObjectLabel(gl, GL_TEXTURE, tex, BR_GLREND_DEBUG_INTERNAL_PREFIX "white");

    return tex;
}

static void destroy_shadow_target(br_gl_context_state *ctx, const GladGLContext *gl)
{
    if(ctx->shadow_fbo != 0)
        gl->DeleteFramebuffers(1, &ctx->shadow_fbo);
    if(ctx->shadow_blocker_fbo != 0)
        gl->DeleteFramebuffers(1, &ctx->shadow_blocker_fbo);
    if(ctx->shadow_texture != 0)
        gl->DeleteTextures(1, &ctx->shadow_texture);
    if(ctx->shadow_owner_texture != 0)
        gl->DeleteTextures(1, &ctx->shadow_owner_texture);
    if(ctx->shadow_blocker_texture != 0)
        gl->DeleteTextures(1, &ctx->shadow_blocker_texture);
    if(ctx->shadow_detail_fbo != 0)
        gl->DeleteFramebuffers(1, &ctx->shadow_detail_fbo);
    if(ctx->shadow_detail_texture != 0)
        gl->DeleteTextures(1, &ctx->shadow_detail_texture);

    ctx->shadow_fbo = 0;
    ctx->shadow_blocker_fbo = 0;
    ctx->shadow_texture = 0;
    ctx->shadow_owner_texture = 0;
    ctx->shadow_blocker_texture = 0;
    ctx->shadow_detail_fbo = 0;
    ctx->shadow_detail_texture = 0;
    ctx->shadow_size = 0;
    ctx->shadow_detail_size = 0;
}

static br_boolean try_build_shadow_target(br_gl_context_state *ctx, const GladGLContext *gl, GLint size)
{
    const GLfloat border[] = {1.0f, 1.0f, 1.0f, 1.0f};
    GLenum status;
    GLenum error;

    /* Do not attribute an older GL error to this allocation attempt. */
    while(gl->GetError() != GL_NO_ERROR)
        ;

    gl->GenTextures(1, &ctx->shadow_texture);
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_texture);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    gl->TexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0,
                   GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    error = gl->GetError();
    if(error != GL_NO_ERROR) {
        BrLogWarn("GLREND", "Shadow depth allocation %dx%d failed with GL error 0x%X.", (int)size, (int)size, (unsigned)error);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        BrWarning("SHADOW target_try_v227 size=%d stage=depth result=fail gl_error=0x%X", (int)size, (unsigned)error);
#endif
        destroy_shadow_target(ctx, gl);
        gl->BindTexture(GL_TEXTURE_2D, 0);
        return BR_FALSE;
    }

    gl->GenTextures(1, &ctx->shadow_owner_texture);
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_owner_texture);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    /* v227: owner equality uses compact 8-bit slots. At 16K this owner map is
     * 256 MiB instead of 512 MiB for R16UI. Slot 255 is an overflow marker;
     * overflow objects still cast shadows but skip same-owner suppression. */
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, size, size, 0,
                   GL_RED_INTEGER, GL_UNSIGNED_BYTE, NULL);
    error = gl->GetError();
    if(error != GL_NO_ERROR) {
        BrLogWarn("GLREND", "Shadow owner allocation %dx%d failed with GL error 0x%X.", (int)size, (int)size, (unsigned)error);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        BrWarning("SHADOW target_try_v227 size=%d stage=owner result=fail gl_error=0x%X", (int)size, (unsigned)error);
#endif
        destroy_shadow_target(ctx, gl);
        gl->BindTexture(GL_TEXTURE_2D, 0);
        return BR_FALSE;
    }

    /*
     * v233 shadow-stop layer.  Object Properties "shadow casting 0" objects
     * do not enter the ordinary caster depth map, but they still need to stop
     * another object's shadow after receiving it.  A separate DEPTH16 map is
     * enough for this ordering/cutoff test and costs half a DEPTH24/32-sized
     * attachment at the same physical resolution.
     */
    gl->GenTextures(1, &ctx->shadow_blocker_texture);
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_blocker_texture);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    gl->TexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT16, size, size, 0,
                   GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, NULL);
    error = gl->GetError();
    if(error != GL_NO_ERROR) {
        BrLogWarn("GLREND", "Shadow blocker depth allocation %dx%d failed with GL error 0x%X.",
                  (int)size, (int)size, (unsigned)error);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        BrWarning("SHADOW target_try_v233 size=%d stage=blocker_depth result=fail gl_error=0x%X",
                  (int)size, (unsigned)error);
#endif
        destroy_shadow_target(ctx, gl);
        gl->BindTexture(GL_TEXTURE_2D, 0);
        return BR_FALSE;
    }

    gl->GenFramebuffers(1, &ctx->shadow_fbo);
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->shadow_texture, 0);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ctx->shadow_owner_texture, 0);
    gl->DrawBuffer(GL_COLOR_ATTACHMENT0);
    gl->ReadBuffer(GL_COLOR_ATTACHMENT0);

    status = gl->CheckFramebufferStatus(GL_FRAMEBUFFER);
    error = gl->GetError();
    if(status != GL_FRAMEBUFFER_COMPLETE || error != GL_NO_ERROR) {
        BrLogWarn("GLREND", "Shadow framebuffer %dx%d failed: status=0x%X GL error=0x%X.",
                  (int)size, (int)size, (unsigned)status, (unsigned)error);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        BrWarning("SHADOW target_try_v233 size=%d stage=caster_fbo result=fail status=0x%X gl_error=0x%X",
                  (int)size, (unsigned)status, (unsigned)error);
#endif
        destroy_shadow_target(ctx, gl);
        gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
        gl->BindTexture(GL_TEXTURE_2D, 0);
        return BR_FALSE;
    }

    gl->GenFramebuffers(1, &ctx->shadow_blocker_fbo);
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_blocker_fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, ctx->shadow_blocker_texture, 0);
    gl->DrawBuffer(GL_NONE);
    gl->ReadBuffer(GL_NONE);
    status = gl->CheckFramebufferStatus(GL_FRAMEBUFFER);
    error = gl->GetError();
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->BindTexture(GL_TEXTURE_2D, 0);

    if(status != GL_FRAMEBUFFER_COMPLETE || error != GL_NO_ERROR) {
        BrLogWarn("GLREND", "Shadow blocker framebuffer %dx%d failed: status=0x%X GL error=0x%X.",
                  (int)size, (int)size, (unsigned)status, (unsigned)error);
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        BrWarning("SHADOW target_try_v233 size=%d stage=blocker_fbo result=fail status=0x%X gl_error=0x%X",
                  (int)size, (unsigned)status, (unsigned)error);
#endif
        destroy_shadow_target(ctx, gl);
        return BR_FALSE;
    }

    ctx->shadow_size = size;
    DeviceGLObjectLabel(gl, GL_TEXTURE, ctx->shadow_texture, BR_GLREND_DEBUG_INTERNAL_PREFIX "shadow:depth");
    DeviceGLObjectLabel(gl, GL_TEXTURE, ctx->shadow_owner_texture, BR_GLREND_DEBUG_INTERNAL_PREFIX "shadow:owner");
    DeviceGLObjectLabel(gl, GL_TEXTURE, ctx->shadow_blocker_texture, BR_GLREND_DEBUG_INTERNAL_PREFIX "shadow:blocker-depth");
    DeviceGLObjectLabel(gl, GL_FRAMEBUFFER, ctx->shadow_fbo, BR_GLREND_DEBUG_INTERNAL_PREFIX "shadow:fbo");
    DeviceGLObjectLabel(gl, GL_FRAMEBUFFER, ctx->shadow_blocker_fbo, BR_GLREND_DEBUG_INTERNAL_PREFIX "shadow:blocker-fbo");

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    BrWarning("SHADOW target_try_v233 size=%d stage=fbo result=ok approx_total_mib=%u owner_format=R8UI depth_format=DEPTH24 blocker_format=DEPTH16 shadow_stop_layer=1",
              (int)size, (unsigned)(((br_uint_64)size * (br_uint_64)size * 7u) / (1024u * 1024u)));
#endif
    return BR_TRUE;
}

static void destroy_shadow_detail_target(br_gl_context_state *ctx, const GladGLContext *gl)
{
    if(ctx->shadow_detail_fbo != 0)
        gl->DeleteFramebuffers(1, &ctx->shadow_detail_fbo);
    if(ctx->shadow_detail_texture != 0)
        gl->DeleteTextures(1, &ctx->shadow_detail_texture);
    ctx->shadow_detail_fbo = 0;
    ctx->shadow_detail_texture = 0;
    ctx->shadow_detail_size = 0;
}

static br_boolean try_build_shadow_detail_target(br_gl_context_state *ctx, const GladGLContext *gl, GLint size)
{
    const GLfloat border[] = {1.0f, 1.0f, 1.0f, 1.0f};
    GLenum status, error;

    while(gl->GetError() != GL_NO_ERROR)
        ;

    gl->GenTextures(1, &ctx->shadow_detail_texture);
    gl->BindTexture(GL_TEXTURE_2D, ctx->shadow_detail_texture);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    gl->TexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, size, size, 0,
                   GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    error = gl->GetError();
    if(error != GL_NO_ERROR) {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        BrWarning("SHADOW detail_target_try_v238 size=%d stage=depth result=fail gl_error=0x%X",
                  (int)size, (unsigned)error);
#endif
        destroy_shadow_detail_target(ctx, gl);
        gl->BindTexture(GL_TEXTURE_2D, 0);
        return BR_FALSE;
    }

    gl->GenFramebuffers(1, &ctx->shadow_detail_fbo);
    gl->BindFramebuffer(GL_FRAMEBUFFER, ctx->shadow_detail_fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                             ctx->shadow_detail_texture, 0);
    gl->DrawBuffer(GL_NONE);
    gl->ReadBuffer(GL_NONE);
    status = gl->CheckFramebufferStatus(GL_FRAMEBUFFER);
    error = gl->GetError();
    gl->BindFramebuffer(GL_FRAMEBUFFER, 0);
    gl->BindTexture(GL_TEXTURE_2D, 0);

    if(status != GL_FRAMEBUFFER_COMPLETE || error != GL_NO_ERROR) {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        BrWarning("SHADOW detail_target_try_v238 size=%d stage=fbo result=fail status=0x%X gl_error=0x%X",
                  (int)size, (unsigned)status, (unsigned)error);
#endif
        destroy_shadow_detail_target(ctx, gl);
        return BR_FALSE;
    }

    ctx->shadow_detail_size = size;
    DeviceGLObjectLabel(gl, GL_TEXTURE, ctx->shadow_detail_texture,
                        BR_GLREND_DEBUG_INTERNAL_PREFIX "shadow:detail-depth");
    DeviceGLObjectLabel(gl, GL_FRAMEBUFFER, ctx->shadow_detail_fbo,
                        BR_GLREND_DEBUG_INTERNAL_PREFIX "shadow:detail-fbo");
#if BRENDER_LEGACY_3DMM_MODEL_ABI
    BrWarning("SHADOW detail_target_try_v238 size=%d result=ok depth_format=DEPTH24 nested_zoom=4 npot_ok=1",
              (int)size);
#endif
    return BR_TRUE;
}

static void build_shadow_detail_target(br_gl_context_state *ctx, const GladGLContext *gl)
{
    GLint candidate;

    /*
     * v261: owner-focused angular fitting now supplies the useful detail gain.
     * Stop paying for a 15360x15360 physical depth texture on top of the 8192
     * base depth/owner/blocker set.  A base-sized depth-only detail target cuts
     * this layer's pixel count by about 72% on the test GPU while the adaptive
     * crop can still provide up to 64x angular magnification for compact
     * visible casters.  This leaves materially more headroom for future
     * multi-light shadow scenes.
     */
    candidate = ctx->shadow_size;
    if(try_build_shadow_detail_target(ctx, gl, candidate)) {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
        BrWarning("SHADOW detail_target_v261 selected=%d base=%d physical_gain=1 owner_focus_density=1 resource_cap=base_size",
                  (int)candidate, (int)ctx->shadow_size);
#endif
        return;
    }

    /*
     * If memory is too fragmented/tight even for another base-sized depth
     * map, preserve the graceful optional fallback below base resolution.
     */
    for(candidate = ctx->shadow_size / 2; candidate >= 2048; candidate /= 2) {
        if(try_build_shadow_detail_target(ctx, gl, candidate)) {
#if BRENDER_LEGACY_3DMM_MODEL_ABI
            BrWarning("SHADOW detail_target_v238 selected=%d base=%d nested_zoom=4 physical_gain=%.3g effective_linear_gain=%.3g npot_probe=1 fallback_below_base=1",
                      (int)candidate, (int)ctx->shadow_size,
                      (double)candidate / (double)ctx->shadow_size,
                      4.0 * (double)candidate / (double)ctx->shadow_size);
#endif
            return;
        }
    }

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    BrWarning("SHADOW detail_target_v238 result=disabled base=%d npot_probe=1", (int)ctx->shadow_size);
#endif
}

static br_boolean build_shadow_target(br_gl_context_state *ctx, const GladGLContext *gl)
{
    GLint requested = BR_GLREND_SHADOW_REQUESTED_SIZE;
    GLint candidate;

    if(ctx->limits.max_texture_size <= 0)
        gl->GetIntegerv(GL_MAX_TEXTURE_SIZE, &ctx->limits.max_texture_size);

    candidate = requested;
    if(candidate > ctx->limits.max_texture_size)
        candidate = ctx->limits.max_texture_size;

    /* Stay on a power-of-two boundary while stepping down. */
    {
        GLint pow2 = 1;
        while(pow2 <= candidate / 2)
            pow2 *= 2;
        candidate = pow2;
    }

#if BRENDER_LEGACY_3DMM_MODEL_ABI
    BrWarning("SHADOW target_caps_v227 requested=%d gl_max_texture=%d first_candidate=%d",
              (int)requested, (int)ctx->limits.max_texture_size, (int)candidate);
#endif
    BrLogInfo("GLREND", "Shadow target request=%d GL_MAX_TEXTURE_SIZE=%d first candidate=%d.",
              (int)requested, (int)ctx->limits.max_texture_size, (int)candidate);

    while(candidate >= BR_GLREND_SHADOW_MIN_SIZE) {
        if(try_build_shadow_target(ctx, gl, candidate)) {
            BrLogInfo("GLREND", "Shadow target selected %dx%d.", (int)candidate, (int)candidate);
            return BR_TRUE;
        }
        candidate /= 2;
    }

    BrLogWarn("GLREND", "No usable shadow target found; shadow rendering disabled.");
#if BRENDER_LEGACY_3DMM_MODEL_ABI
    BrWarning("SHADOW target_caps_v227 result=disabled requested=%d gl_max_texture=%d",
              (int)requested, (int)ctx->limits.max_texture_size);
#endif
    return BR_FALSE;
}

static GLuint build_checkerboard_texture(const GladGLContext *gl)
{
    // BRenderModern: clang-format off
    const static br_uint_8 checkerboard_rgba[] = {
        0x00, 0x00, 0x00, 0xFF,   0xFF, 0x00, 0xFF, 0xFF,
        0xFF, 0x00, 0xFF, 0xFF,   0x00, 0x00, 0x00, 0xFF,
    };
    // BRenderModern: clang-format on

    GLuint tex;

    gl->GenTextures(1, &tex);

    gl->BindTexture(GL_TEXTURE_2D, tex);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    gl->TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, checkerboard_rgba);

    DeviceGLObjectLabel(gl, GL_TEXTURE, tex, BR_GLREND_DEBUG_INTERNAL_PREFIX "checkerboard");

    return tex;
}

br_error ContextStateGLInit(void *vparent, br_gl_context_state *self, const GladGLContext *gl)
{
    BrMemSet(self, 0, sizeof(br_gl_context_state));

    self->gl = gl;

    self->gl_version  = (const char *)gl->GetString(GL_VERSION);
    self->gl_vendor   = (const char *)gl->GetString(GL_VENDOR);
    self->gl_renderer = (const char *)gl->GetString(GL_RENDERER);

    /* BRenderModern:
     * Always register the debug stuff, it needs to be explicitly glEnable(GL_DEBUG_OUTPUT)'d anyway.
     */
    if(gl->KHR_debug) {
        gl->DebugMessageCallback(gl_debug_callback, self);
        gl->DebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);

#if BR_GLREND_DEBUG
        gl->Enable(GL_DEBUG_OUTPUT);
        // BRenderModern: gl->Enable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif
    }

    /* BRenderModern:
     * Get a copy of the extension list.
     * NULL-terminate so we can expose it as a BRT_POINTER_LIST.
     */
    self->gl_extensions = build_extensions_list(vparent, gl, &self->gl_num_extensions);

    /* BRenderModern:
     * Cache some limits.
     */
    gl->GetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &self->limits.max_uniform_block_size);
    gl->GetIntegerv(GL_MAX_UNIFORM_BUFFER_BINDINGS, &self->limits.max_uniform_buffer_bindings);
    gl->GetIntegerv(GL_MAX_VERTEX_UNIFORM_BLOCKS, &self->limits.max_vertex_uniform_blocks);
    gl->GetIntegerv(GL_MAX_FRAGMENT_UNIFORM_BLOCKS, &self->limits.max_fragment_uniform_blocks);
    gl->GetIntegerv(GL_MAX_SAMPLES, &self->limits.max_samples);
    gl->GetIntegerv(GL_MAX_TEXTURE_SIZE, &self->limits.max_texture_size);

    if(gl->EXT_texture_filter_anisotropic)
        gl->GetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &self->limits.max_anisotropy);
    else
        self->limits.max_anisotropy = 1.0f;

    /* BRenderModern:
     * Everything's init'd, quirkify.
     */
    setup_quirks(self);

    self->tex_white        = build_white_texture(gl);
    self->tex_checkerboard = build_checkerboard_texture(gl);

    /* BRenderModern:
     * We can't use BRender's fonts directly, so build a POT texture with
     * glyph from left-to-right. All fonts have 256 possible characters.
     */

    BrLogTrace("GLREND", "Building fixed 3x5 font array.");
    (void)ShaderGLTextBuildFont(gl, &self->font_fixed3x5, BrFontFixed3x5);

    BrLogTrace("GLREND", "Building proportional 4x6 font array.");
    (void)ShaderGLTextBuildFont(gl, &self->font_prop4x6, BrFontProp4x6);

    BrLogTrace("GLREND", "Building proportional 7x9 font array.");
    (void)ShaderGLTextBuildFont(gl, &self->font_prop7x9, BrFontProp7x9);

    ShaderGLLineCompile(&self->line_shader, gl);
    ShaderGLRectCompile(&self->rect_shader, gl);
    ShaderGLTextCompile(&self->text_shader, gl);
    ShaderGLMainCompile(&self->main_shader, gl, NULL, NULL);

    if(ShaderGLShadowCompile(&self->shadow_shader, gl)) {
        if(build_shadow_target(self, gl))
            build_shadow_detail_target(self, gl);
    }
    else
        BrLogWarn("GLREND", "Shadow shader compilation failed; shadow rendering disabled.");

    return BRE_OK;
}

void ContextStateGLFini(br_gl_context_state *self)
{
    const GladGLContext *gl = self->gl;

    gl->UseProgram(0);
    gl->BindVertexArray(0);
    gl->BindBuffer(GL_ARRAY_BUFFER, 0);
    gl->BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    gl->BindBuffer(GL_UNIFORM_BUFFER, 0);

    if(self->shadow_fbo != 0)
        gl->DeleteFramebuffers(1, &self->shadow_fbo);
    if(self->shadow_blocker_fbo != 0)
        gl->DeleteFramebuffers(1, &self->shadow_blocker_fbo);
    if(self->shadow_texture != 0)
        gl->DeleteTextures(1, &self->shadow_texture);
    if(self->shadow_owner_texture != 0)
        gl->DeleteTextures(1, &self->shadow_owner_texture);
    if(self->shadow_blocker_texture != 0)
        gl->DeleteTextures(1, &self->shadow_blocker_texture);
    if(self->shadow_detail_fbo != 0)
        gl->DeleteFramebuffers(1, &self->shadow_detail_fbo);
    if(self->shadow_detail_texture != 0)
        gl->DeleteTextures(1, &self->shadow_detail_texture);
    if(self->shadow_shader.program != 0)
        gl->DeleteProgram(self->shadow_shader.program);

    gl->DeleteProgram(self->main_shader.program);
    gl->DeleteBuffers(1, &self->main_shader.ubo_scene);

    gl->DeleteProgram(self->text_shader.program);
    gl->DeleteVertexArrays(1, &self->text_shader.vao_glyphs);
    gl->DeleteBuffers(1, &self->text_shader.ubo_glyphs);

    gl->DeleteProgram(self->rect_shader.program);
    gl->DeleteVertexArrays(1, &self->rect_shader.vao);
    gl->DeleteBuffers(1, &self->rect_shader.ubo);

    gl->DeleteProgram(self->line_shader.program);
    gl->DeleteVertexArrays(1, &self->line_shader.vao);
    gl->DeleteBuffers(1, &self->line_shader.ubo);

    gl->DeleteTextures(1, &self->font_prop7x9.tex);
    gl->DeleteTextures(1, &self->font_prop4x6.tex);
    gl->DeleteTextures(1, &self->font_fixed3x5.tex);
    gl->DeleteTextures(1, &self->tex_checkerboard);
    gl->DeleteTextures(1, &self->tex_white);
}
