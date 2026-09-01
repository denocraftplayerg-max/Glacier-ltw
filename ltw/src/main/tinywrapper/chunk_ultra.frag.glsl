#version 320 es
#extension GL_EXT_shader_pixel_local_storage2 : enable
#extension GL_EXT_conservative_depth          : enable
#extension GL_OES_standard_derivatives        : enable
#extension GL_EXT_gpu_shader5                 : enable

precision highp float;
precision highp sampler2D;

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vLight;
flat in uint vInstanceID;

uniform sampler2D uAtlas;
uniform float     uAlphaRef;

#ifdef GL_EXT_conservative_depth
layout (depth_greater) out float gl_FragDepth;
#endif

#ifdef GL_EXT_shader_pixel_local_storage2
__pixel_local_outEXT GBuffer {
    layout(rgba8)   highp vec4 albedoAO;
    layout(rgba8)   highp vec4 normalMat;
    layout(rgba16f) highp vec4 lightSky;
} pls;
#else
out vec4 fragColor;
#endif

void main() {
    vec4 texel = textureGrad(uAtlas, vUV, dFdx(vUV), dFdy(vUV));
    if (texel.a < uAlphaRef) discard;

#ifdef GL_EXT_shader_pixel_local_storage2
    pls.albedoAO  = vec4(texel.rgb, vLight.z);
    pls.normalMat = vec4(vNormal * 0.5 + 0.5, vLight.w);
    pls.lightSky  = vec4(vec2(vLight.y), vLight.x, gl_FragCoord.z);
#else
    fragColor = texel;
#endif

#ifdef GL_EXT_conservative_depth
    gl_FragDepth = gl_FragCoord.z;
#endif
}
