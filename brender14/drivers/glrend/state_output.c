#include "drv.h"

#define S    BRTV_SET
#define Q    BRTV_QUERY
#define A    BRTV_ALL

#define AX   0
#define AF   BRTV_ALL

#define F(f) offsetof(state_stack, f)

static br_tv_template_entry template_entries[] = {
    {BRT(COLOUR_BUFFER_O), F(output.colour),      Q | S | A, BRTV_CONV_COPY},
    {BRT(DEPTH_BUFFER_O),  F(output.depth),        Q | S | A, BRTV_CONV_COPY},
    {DEV(SHADOW_PASS_B),                    F(output.shadow_pass),                 Q | S | A,  BRTV_CONV_COPY},
    {DEV(SHADOW_BLOCKER_PASS_B),            F(output.shadow_blocker_pass),         Q | S | A,  BRTV_CONV_COPY},
    {DEV(SHADOW_DETAIL_PASS_B),             F(output.shadow_detail_pass),          Q | S | A,  BRTV_CONV_COPY},
    {DEV(SHADOW_OWNER_U32),                  F(output.shadow_owner),                Q | S | A,  BRTV_CONV_COPY},
    {DEV(SHADOW_MODEL_TO_LIGHT_VALID_B),    F(output.shadow_model_to_light_valid), Q | S | A,  BRTV_CONV_COPY},
    {DEV(SHADOW_MODEL_TO_LIGHT_M34_X),      F(output.shadow_model_to_light),       Q | S | AX, BRTV_CONV_M34_FIXED_SCALAR},
    {DEV(SHADOW_MODEL_TO_LIGHT_M34_F),      F(output.shadow_model_to_light),       Q | S | AF, BRTV_CONV_M34_FLOAT_SCALAR},
};

static const state_output default_state = {
    .colour                      = NULL,
    .depth                       = NULL,
    .shadow_pass                 = BR_FALSE,
    .shadow_blocker_pass         = BR_FALSE,
    .shadow_detail_pass          = BR_FALSE,
    .shadow_owner                = 0,
    .shadow_model_to_light_valid = BR_FALSE,
    .shadow_model_to_light       = {{
        BR_VECTOR3(1, 0, 0),
        BR_VECTOR3(0, 1, 0),
        BR_VECTOR3(0, 0, 1),
        BR_VECTOR3(0, 0, 0),
    }},
};

void StateGLInitOutput(state_all *state)
{
    state->templates.output = BrTVTemplateAllocate(state->res, template_entries, BR_ASIZE(template_entries));

    state->default_.output = default_state;
    state->default_.valid |= MASK_STATE_OUTPUT;
}
