#version 430 core

#include "brender.common.glsl"

layout(location=0) in vec3 aPosition;

flat out uint shadowOwner;

void main()
{
    vec3 light_position = (shadow_model_to_light * vec4(aPosition, 1.0)).xyz;
    shadowOwner = shadow_owner_info.x;
    gl_Position = shadowProjectLight(light_position);
}
