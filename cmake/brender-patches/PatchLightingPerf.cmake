# logperf17: low-overhead Source BRender lighting/model performance probes.
# The C side reports aggregate per-frame numbers through C-linkage bridges in
# src/engine/movie.cpp. Nothing is written from inside the per-vertex loops.

if(NOT DEFINED BRENDER_SOURCE_DIR)
    message(FATAL_ERROR "logperf17: BRENDER_SOURCE_DIR was not supplied")
endif()

function(_logperf17_patch_file file marker)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "logperf17: cannot find ${file}")
    endif()
    file(READ "${file}" _src)
    string(FIND "${_src}" "${marker}" _already)
    if(NOT _already EQUAL -1)
        set(LOGPERF17_ALREADY TRUE PARENT_SCOPE)
        set(LOGPERF17_SOURCE "${_src}" PARENT_SCOPE)
    else()
        set(LOGPERF17_ALREADY FALSE PARENT_SCOPE)
        set(LOGPERF17_SOURCE "${_src}" PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# FW/light24.c: time the true-colour per-vertex lighting function and count
# actual vertex-light evaluations. This is the hottest path we specifically
# need to measure when an animated object crosses one or more 4DMM lights.
# ---------------------------------------------------------------------------
set(_file "${BRENDER_SOURCE_DIR}/FW/light24.c")
set(_marker "logperf17: per-vertex lighting profiler")
_logperf17_patch_file("${_file}" "${_marker}")
set(_src "${LOGPERF17_SOURCE}")
if(NOT LOGPERF17_ALREADY)
    set(_inc_old [[#include "brassert.h"
]])
    set(_inc_new [[#include "brassert.h"

/* logperf17: per-vertex lighting profiler */
extern int vgLightingPerformanceEnabled;
extern unsigned long long QwLightingPerformanceProfileNow(void);
extern unsigned int CusecLightingPerformanceProfileElapsed(unsigned long long qwStart);
extern void RecordPerformanceLighting(unsigned int cusec, int cLightCalculations);
]])
    string(FIND "${_src}" "${_inc_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "logperf17: light24 include anchor not found")
    endif()
    string(REPLACE "${_inc_old}" "${_inc_new}" _src "${_src}")

    set(_decl_old [[void BR_SURFACE_CALL LightingColour(br_vertex *v, br_fvector3 *n, br_scalar *comp)
{
    br_active_light *alp;
    int i;
]])
    set(_decl_new [[void BR_SURFACE_CALL LightingColour(br_vertex *v, br_fvector3 *n, br_scalar *comp)
{
    br_active_light *alp;
    int i;
    unsigned long long qwPerfLighting = 0;
    int cPerfLightCalculations = 0;

    if (vgLightingPerformanceEnabled)
    {
        qwPerfLighting = QwLightingPerformanceProfileNow();
        cPerfLightCalculations = fw.nactive_lights_model + fw.nactive_lights_view;
    }
]])
    string(FIND "${_src}" "${_decl_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "logperf17: LightingColour declaration anchor not found")
    endif()
    string(REPLACE "${_decl_old}" "${_decl_new}" _src "${_src}")


    # actorlight9 runs immediately before this script and provides this stable
    # tail marker in the fetched source.
    set(_tail_old [[    comp[C_I] = BR_CONST_DIV(comp[C_R] + comp[C_G] + comp[C_B], 3);
}
]])
    set(_tail_new [[    comp[C_I] = BR_CONST_DIV(comp[C_R] + comp[C_G] + comp[C_B], 3);

    if (vgLightingPerformanceEnabled)
        RecordPerformanceLighting(CusecLightingPerformanceProfileElapsed(qwPerfLighting), cPerfLightCalculations);
}
]])
    string(FIND "${_src}" "${_tail_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "logperf17: actorlight9 LightingColour tail anchor not found")
    endif()
    string(REPLACE "${_tail_old}" "${_tail_new}" _src "${_src}")
    file(WRITE "${_file}" "${_src}")
    message(STATUS "logperf17: instrumented FW/light24.c")
else()
    message(STATUS "logperf17: FW/light24.c already instrumented")
endif()

# ---------------------------------------------------------------------------
# FW/surface.c: measure active-light preparation once per scene and once per
# model. This catches pathological light-transform setup separately from the
# actual per-vertex lighting math.
# ---------------------------------------------------------------------------
set(_file "${BRENDER_SOURCE_DIR}/FW/surface.c")
set(_marker "logperf17: light preparation profiler")
_logperf17_patch_file("${_file}" "${_marker}")
set(_src "${LOGPERF17_SOURCE}")
if(NOT LOGPERF17_ALREADY)
    set(_inc_old [[#include "brassert.h"
]])
    set(_inc_new [[#include "brassert.h"

/* logperf17: light preparation profiler */
extern int vgLightingPerformanceEnabled;
extern unsigned long long QwLightingPerformanceProfileNow(void);
extern unsigned int CusecLightingPerformanceProfileElapsed(unsigned long long qwStart);
extern void RecordPerformanceLightPreparation(unsigned int cusec);
]])
    string(FIND "${_src}" "${_inc_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "logperf17: surface include anchor not found")
    endif()
    string(REPLACE "${_inc_old}" "${_inc_new}" _src "${_src}")

    set(_scene_decl_old [[    br_active_light *alp;

    /*
     * Set up lighting sub funtions depending on type of output pixels
]])
    set(_scene_decl_new [[    br_active_light *alp;
    unsigned long long qwPerfLightPreparation = 0;

    if (vgLightingPerformanceEnabled)
        qwPerfLightPreparation = QwLightingPerformanceProfileNow();

    /*
     * Set up lighting sub funtions depending on type of output pixels
]])
    string(FIND "${_src}" "${_scene_decl_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "logperf17: SurfacePerScene declaration anchor not found")
    endif()
    string(REPLACE "${_scene_decl_old}" "${_scene_decl_new}" _src "${_src}")

    set(_scene_tail_old [[    fw.nactive_clip_planes = nc;
}

/*
 * Process active lights before descending into a model
]])
    set(_scene_tail_new [[    fw.nactive_clip_planes = nc;

    if (vgLightingPerformanceEnabled)
        RecordPerformanceLightPreparation(CusecLightingPerformanceProfileElapsed(qwPerfLightPreparation));
}

/*
 * Process active lights before descending into a model
]])
    string(FIND "${_src}" "${_scene_tail_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "logperf17: SurfacePerScene tail anchor not found")
    endif()
    string(REPLACE "${_scene_tail_old}" "${_scene_tail_new}" _src "${_src}")

    set(_model_decl_old [[void SurfacePerModel(void)
{
    int i;
    br_active_light *alp;

    alp = fw.active_lights_model;
]])
    set(_model_decl_new [[void SurfacePerModel(void)
{
    int i;
    br_active_light *alp;
    unsigned long long qwPerfLightPreparation = 0;

    if (vgLightingPerformanceEnabled)
        qwPerfLightPreparation = QwLightingPerformanceProfileNow();

    alp = fw.active_lights_model;
]])
    string(FIND "${_src}" "${_model_decl_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "logperf17: SurfacePerModel declaration anchor not found")
    endif()
    string(REPLACE "${_model_decl_old}" "${_model_decl_new}" _src "${_src}")

    set(_model_tail_old [[    }
}

/*
 * Dummy functions - in case renderer decides to invoke
]])
    set(_model_tail_new [[    }

    if (vgLightingPerformanceEnabled)
        RecordPerformanceLightPreparation(CusecLightingPerformanceProfileElapsed(qwPerfLightPreparation));
}

/*
 * Dummy functions - in case renderer decides to invoke
]])
    # This anchor occurs at SurfacePerModel's end immediately before Dummy funcs.
    string(FIND "${_src}" "${_model_tail_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "logperf17: SurfacePerModel tail anchor not found")
    endif()
    string(REPLACE "${_model_tail_old}" "${_model_tail_new}" _src "${_src}")
    file(WRITE "${_file}" "${_src}")
    message(STATUS "logperf17: instrumented FW/surface.c")
else()
    message(STATUS "logperf17: FW/surface.c already instrumented")
endif()

# ---------------------------------------------------------------------------
# FW/prepmesh.c: measure actual full BrModelUpdate rebuilds. Pre-prepared or
# empty models that return immediately are intentionally not counted as rebuilt.
# ---------------------------------------------------------------------------
set(_file "${BRENDER_SOURCE_DIR}/FW/prepmesh.c")
set(_marker "logperf17: model rebuild profiler")
_logperf17_patch_file("${_file}" "${_marker}")
set(_src "${LOGPERF17_SOURCE}")
if(NOT LOGPERF17_ALREADY)
    set(_inc_old [[#include "shortcut.h"
]])
    set(_inc_new [[#include "shortcut.h"

/* logperf17: model rebuild profiler */
extern int vgLightingPerformanceEnabled;
extern unsigned long long QwLightingPerformanceProfileNow(void);
extern unsigned int CusecLightingPerformanceProfileElapsed(unsigned long long qwStart);
extern void RecordPerformanceModelUpdateDetailed(unsigned int cusec, unsigned int grfUpdate, unsigned int grfModel);
]])
    string(FIND "${_src}" "${_inc_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "logperf17: prepmesh include anchor not found")
    endif()
    string(REPLACE "${_inc_old}" "${_inc_new}" _src "${_src}")

    # Do not depend on the exact local-variable list in this 1995 source file.
    # Different cached revisions/formatting of the blazin branch have varied here.
    # Put the profiler variable first in BrModelUpdate so it remains valid C89,
    # and initialize the timer immediately. Early-return/preprepared models still
    # intentionally never reach RecordPerformanceModelUpdate().
    string(REGEX MATCH
        "void[ \t]+BR_PUBLIC_ENTRY[ \t]+BrModelUpdate\\([^\\)]*\\)[ \t\r\n]*\\{"
        _model_decl_match "${_src}")
    if("${_model_decl_match}" STREQUAL "")
        message(FATAL_ERROR "logperf17: BrModelUpdate function signature not found")
    endif()
    set(_model_decl_new "${_model_decl_match}\n    unsigned long long qwPerfModelUpdate = vgLightingPerformanceEnabled ? QwLightingPerformanceProfileNow() : 0;")
    string(REPLACE "${_model_decl_match}" "${_model_decl_new}" _src "${_src}")

    # Find the renderer callback structurally rather than relying on the debug
    # block immediately following it. This is the end of a completed rebuild.
    string(REGEX MATCH
        "if[ \t]*\\([ \t]*fw\\.model_update[ \t]*\\)[ \t\r\n]+fw\\.model_update\\([ \t]*model[ \t]*,[ \t]*flags[ \t]*\\);"
        _model_tail_match "${_src}")
    if("${_model_tail_match}" STREQUAL "")
        message(FATAL_ERROR "logperf17: BrModelUpdate renderer callback not found")
    endif()
    set(_model_tail_new "${_model_tail_match}\n\n    if (vgLightingPerformanceEnabled)\n        RecordPerformanceModelUpdateDetailed(CusecLightingPerformanceProfileElapsed(qwPerfModelUpdate), flags, model->flags);")
    string(REPLACE "${_model_tail_match}" "${_model_tail_new}" _src "${_src}")
    file(WRITE "${_file}" "${_src}")
    message(STATUS "logperf17: instrumented FW/prepmesh.c")
else()
    message(STATUS "logperf17: FW/prepmesh.c already instrumented")
endif()

# actorlight27: older fetched build trees may already contain logperf17's
# original one-argument profiler. Upgrade those trees in place so a normal
# reconfigure does not require deleting _deps/3dmm-brender-src first.
set(_file "${BRENDER_SOURCE_DIR}/FW/prepmesh.c")
file(READ "${_file}" _src)
string(FIND "${_src}" "RecordPerformanceModelUpdateDetailed" _detailed)
if(_detailed EQUAL -1)
    string(REPLACE
        "extern void RecordPerformanceModelUpdate(unsigned int cusec);"
        "extern void RecordPerformanceModelUpdateDetailed(unsigned int cusec, unsigned int grfUpdate, unsigned int grfModel);"
        _src "${_src}")
    string(REPLACE
        "RecordPerformanceModelUpdate(CusecLightingPerformanceProfileElapsed(qwPerfModelUpdate));"
        "RecordPerformanceModelUpdateDetailed(CusecLightingPerformanceProfileElapsed(qwPerfModelUpdate), flags, model->flags);"
        _src "${_src}")
    file(WRITE "${_file}" "${_src}")
    message(STATUS "actorlight27: upgraded BrModelUpdate profiler detail")
endif()

# ---------------------------------------------------------------------------
# FW/regsupt.c: BrModelAdd is the only normal source-BRender registration path
# and immediately performs BR_MODU_ALL.  Count it separately, and under -a use
# BRender's own QUICK_UPDATE preparation mode.  This avoids the expensive
# sort/group preparation designed for static models while preserving BRender's
# supported update semantics.  Renderer cost may rise slightly; the CSV tells
# us whether the trade is worthwhile.
# ---------------------------------------------------------------------------
set(_file "${BRENDER_SOURCE_DIR}/FW/regsupt.c")
set(_marker "actorlight27: quick model add profiler")
_logperf17_patch_file("${_file}" "${_marker}")
set(_src "${LOGPERF17_SOURCE}")
if(NOT LOGPERF17_ALREADY)
    set(_inc_old [[#include "datafile.h"
]])
    set(_inc_new [[#include "datafile.h"

/* actorlight27: quick model add profiler */
extern int vgLightingPerformanceEnabled;
extern void RecordPerformanceModelAdd(int fQuick);
]])
    string(FIND "${_src}" "${_inc_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "actorlight27: regsupt include anchor not found")
    endif()
    string(REPLACE "${_inc_old}" "${_inc_new}" _src "${_src}")

    # actorlight27b: CMake's regex handling of \t/\r/\n inside the
    # original whitespace character classes produced a false "anchor not found"
    # against stock blazin FW/regsupt.c. Locate BrModelAdd structurally instead.
    string(FIND "${_src}" "br_model *BR_PUBLIC_ENTRY BrModelAdd(br_model *model)" _model_add_start)
    string(FIND "${_src}" "br_model *BR_PUBLIC_ENTRY BrModelRemove(br_model *model)" _model_remove_start)
    if(_model_add_start EQUAL -1 OR _model_remove_start EQUAL -1 OR _model_remove_start LESS_EQUAL _model_add_start)
        message(FATAL_ERROR "actorlight27b: BrModelAdd function bounds not found")
    endif()
    math(EXPR _model_add_len "${_model_remove_start} - ${_model_add_start}")
    string(SUBSTRING "${_src}" ${_model_add_start} ${_model_add_len} _model_add_body)

    set(_registry_call "RegistryAdd(&fw.reg_models, model);")
    set(_update_call "BrModelUpdate(model, BR_MODU_ALL);")
    string(FIND "${_model_add_body}" "${_registry_call}" _registry_pos)
    string(FIND "${_model_add_body}" "${_update_call}" _update_pos)
    if(_registry_pos EQUAL -1 OR _update_pos EQUAL -1 OR _update_pos LESS_EQUAL _registry_pos)
        message(FATAL_ERROR "actorlight27b: BrModelAdd registry/update calls not found")
    endif()

    set(_registry_new [[RegistryAdd(&fw.reg_models, model);

    /* actorlight28: profile BrModelAdd, but do not globally force
       BR_MODF_QUICK_UPDATE. v27 proved that path cuts preparation time but
       damages TDT texture preparation and triples render cost. actorlight28
       removes the redundant generated-model adds at the TDT call site instead. */
    if (vgLightingPerformanceEnabled)
        RecordPerformanceModelAdd((model->flags & BR_MODF_QUICK_UPDATE) != 0);]])
    string(REPLACE "${_registry_call}" "${_registry_new}" _model_add_body "${_model_add_body}")

    string(SUBSTRING "${_src}" 0 ${_model_add_start} _src_prefix)
    string(SUBSTRING "${_src}" ${_model_remove_start} -1 _src_suffix)
    set(_src "${_src_prefix}${_model_add_body}${_src_suffix}")

    file(WRITE "${_file}" "${_src}")
    message(STATUS "actorlight27: instrumented/accelerated FW/regsupt.c")
else()
    message(STATUS "actorlight27: FW/regsupt.c already instrumented")
endif()

# actorlight28: an existing fetched BRender tree may already contain v27's
# forced QUICK_UPDATE block. Remove it in place as well as omitting it for a
# fresh fetch, so ordinary incremental reconfigure/builds restore full-quality
# model preparation without requiring the user to delete build/_deps.
set(_file "${BRENDER_SOURCE_DIR}/FW/regsupt.c")
file(READ "${_file}" _src)
set(_quick_old [[    if (vgActorLightQuickModelUpdateEnabled && !(model->flags & BR_MODF_PREPREPARED))
        model->flags |= BR_MODF_QUICK_UPDATE;

]])
string(FIND "${_src}" "${_quick_old}" _quick_pos)
if(NOT _quick_pos EQUAL -1)
    string(REPLACE "${_quick_old}" "" _src "${_src}")
    file(WRITE "${_file}" "${_src}")
    message(STATUS "actorlight28: disabled global BRender QUICK_UPDATE; TDT cache handles repeated generated models")
else()
    message(STATUS "actorlight28: global BRender QUICK_UPDATE already disabled")
endif()

# ---------------------------------------------------------------------------
# ZB/zbmesh.c: count each rendered model that contains a BR_MATF_LIGHT vertex
# group. LightingColour counts the exact vertices actually processed.
# ---------------------------------------------------------------------------
set(_file "${BRENDER_SOURCE_DIR}/ZB/zbmesh.c")
set(_marker "logperf17: relit-model counter")
_logperf17_patch_file("${_file}" "${_marker}")
set(_src "${LOGPERF17_SOURCE}")
if(NOT LOGPERF17_ALREADY)
    set(_inc_old [[#include "brassert.h"
]])
    set(_inc_new [[#include "brassert.h"

/* logperf17: relit-model counter */
extern int vgLightingPerformanceEnabled;
extern void RecordPerformanceModelRelit(void);
]])
    string(FIND "${_src}" "${_inc_old}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "logperf17: zbmesh include anchor not found")
    endif()
    string(REPLACE "${_inc_old}" "${_inc_new}" _src "${_src}")

    # Insert profiler locals at the start of ZbMeshRender instead of matching
    # the renderer's exact scratch-local declaration layout.
    string(REGEX MATCH
        "void[ \t]+ZbMeshRender\\([^\\)]*\\)[ \t\r\n]*\\{"
        _mesh_decl_match "${_src}")
    if("${_mesh_decl_match}" STREQUAL "")
        message(FATAL_ERROR "logperf17: ZbMeshRender function signature not found")
    endif()
    set(_mesh_decl_new "${_mesh_decl_match}\n    int g_perf;\n    br_material *pmat_perf;")
    string(REPLACE "${_mesh_decl_match}" "${_mesh_decl_new}" _src "${_src}")

    string(REGEX MATCH
        "zb\\.model[ \t]*=[ \t]*model[ \t]*;[ \t\r\n]+zb\\.default_material[ \t]*=[ \t]*material[ \t]*;"
        _mesh_body_match "${_src}")
    if("${_mesh_body_match}" STREQUAL "")
        message(FATAL_ERROR "logperf17: ZbMeshRender model assignment not found")
    endif()
    set(_mesh_body_new "${_mesh_body_match}\n\n    if (vgLightingPerformanceEnabled && model->vertex_groups != NULL)\n    {\n        for (g_perf = 0; g_perf < model->nvertex_groups; g_perf++)\n        {\n            pmat_perf = model->vertex_groups[g_perf].material ? model->vertex_groups[g_perf].material : material;\n            if (pmat_perf != NULL && (pmat_perf->flags & BR_MATF_LIGHT))\n            {\n                RecordPerformanceModelRelit();\n                break;\n            }\n        }\n    }")
    string(REPLACE "${_mesh_body_match}" "${_mesh_body_new}" _src "${_src}")
    file(WRITE "${_file}" "${_src}")
    message(STATUS "logperf17: instrumented ZB/zbmesh.c")
else()
    message(STATUS "logperf17: ZB/zbmesh.c already instrumented")
endif()
