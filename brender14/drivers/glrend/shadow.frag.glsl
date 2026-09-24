#version 430 core

flat in uint shadowOwner;
layout(location=0) out uint shadowOwnerOut;

uniform sampler2D shadowBaseDepthTexture;
uniform int shadowLayerMode;

void main()
{
    /*
     * v239 blocker depth peel. The blocker map must contain the nearest
     * no-cast surface *behind* the ordinary caster stored in the base shadow
     * map. If we simply store the nearest no-cast surface from the light, an
     * upstream no-cast object hides every downstream blocker and produces the
     * off/on/off "double negative" reported in 238.
     *
     * Base and blocker passes share the same fitted projection and dimensions,
     * so gl_FragCoord.xy addresses the exact base texel for this blocker
     * fragment. Reject blocker fragments that are not beyond a real base
     * caster; the blocker's normal GL_LESS depth test then keeps the nearest
     * qualifying downstream blocker.
     */
    if(shadowLayerMode == 1) {
        ivec2 baseSize = textureSize(shadowBaseDepthTexture, 0);
        ivec2 coord = clamp(ivec2(gl_FragCoord.xy), ivec2(0), baseSize - ivec2(1));
        float casterDepth = texelFetch(shadowBaseDepthTexture, coord, 0).r;
        const float depth24Epsilon = 4.0 / 16777216.0;

        if(casterDepth >= 1.0 - depth24Epsilon ||
           gl_FragCoord.z <= casterDepth + depth24Epsilon)
            discard;
    }

    shadowOwnerOut = shadowOwner;
}
