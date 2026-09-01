#version 320 es
#extension GL_EXT_gpu_shader5 : enable

precision highp float;

layout(location=0) in vec3  aPos;
layout(location=1) in uint  aNormPacked;
layout(location=2) in uint  aUVPacked;
layout(location=3) in uint  aLightPacked;

struct InstanceData {
    mat4 model;
    vec4 lightData;
};
layout(std430, binding=4) readonly buffer Instances {
    InstanceData instances[];
};

uniform mat4 uVP;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vLight;
flat out uint vInstanceID;

vec3 unpackNormal(uint packed) {
    float x = float(int(bitfieldExtract(int(packed),  0, 16))) / 32767.0;
    float y = float(int(bitfieldExtract(int(packed), 16, 16))) / 32767.0;
    float z = sqrt(max(0.0, 1.0 - x*x - y*y));
    return vec3(x, y, z);
}

vec2 unpackUV(uint packed) {
    float u = float(bitfieldExtract(int(packed),  0, 16)) / 65535.0;
    float v = float(bitfieldExtract(int(packed), 16, 16)) / 65535.0;
    return vec2(u, v);
}

vec4 unpackLight(uint packed) {
    float sky   = float(bitfieldExtract(int(packed),  0, 8)) / 255.0;
    float block = float(bitfieldExtract(int(packed),  8, 8)) / 255.0;
    float ao    = float(bitfieldExtract(int(packed), 16, 8)) / 255.0;
    float mat   = float(bitfieldExtract(int(packed), 24, 8)) / 255.0;
    return vec4(sky, block, ao, mat);
}

void main() {
    uint iid = uint(gl_InstanceID);
    vInstanceID = iid;

    InstanceData inst = instances[iid];
    vec4 worldPos = inst.model * vec4(aPos, 1.0);
    vWorldPos = worldPos.xyz;

    vNormal = unpackNormal(aNormPacked);
    vUV     = unpackUV(aUVPacked);
    vLight  = unpackLight(aLightPacked);

    gl_Position = uVP * worldPos;
}
