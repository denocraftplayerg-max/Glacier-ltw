#version 320 es
#extension GL_EXT_shader_group_vote : enable
#extension GL_EXT_gpu_shader5       : enable

layout(local_size_x = 64) in;

struct ObjectData {
    vec4  sphere;
    uvec2 drawParams;
    uvec2 extra;
};

struct DrawCommand {
    uint count;
    uint instanceCount;
    uint firstIndex;
    int  baseVertex;
    uint baseInstance;
};

layout(std430, binding=0) readonly  buffer Objects  { ObjectData objects[];  };
layout(std430, binding=1) writeonly buffer DrawCmds { DrawCommand cmds[];    };
layout(std430, binding=2) buffer    Counter         { uint drawCount;        };
layout(std430, binding=3) writeonly buffer Visible  { uint visible[];        };

uniform vec4  uFrustum[6];
uniform uint  uCount;
uniform vec3  uCamPos;
uniform float uLodBias;

bool sphereInFrustum(vec3 center, float radius) {
    float d0 = dot(uFrustum[0].xyz, center) + uFrustum[0].w;
    float d1 = dot(uFrustum[1].xyz, center) + uFrustum[1].w;
    float d2 = dot(uFrustum[2].xyz, center) + uFrustum[2].w;
    float d3 = dot(uFrustum[3].xyz, center) + uFrustum[3].w;
    float d4 = dot(uFrustum[4].xyz, center) + uFrustum[4].w;
    float d5 = dot(uFrustum[5].xyz, center) + uFrustum[5].w;
    float minD = min(min(min(d0,d1),min(d2,d3)),min(d4,d5));
    return minD >= -radius;
}

uint calcLOD(float dist) {
    if (dist < 32.0 + uLodBias)  return 0u;
    if (dist < 80.0 + uLodBias)  return 1u;
    return 2u;
}

void main() {
    uint id = gl_GlobalInvocationID.x;
    bool valid = (id < uCount);
#ifdef GL_EXT_shader_group_vote
    if (!anyInvocations(valid)) return;
#endif

    if (!valid) return;
    visible[id] = 0u;

    ObjectData obj    = objects[id];
    vec3  center      = obj.sphere.xyz;
    float radius      = obj.sphere.w;

    if (!sphereInFrustum(center, radius)) return;

    float dist = distance(center, uCamPos);
    uint  lod  = calcLOD(dist);

    uint idxCount  = obj.drawParams.x >> (lod * 2u);
    uint firstIdx  = obj.drawParams.y + lod * 128u;

    uint slot = atomicAdd(drawCount, 1u);
    DrawCommand cmd;
    cmd.count         = idxCount;
    cmd.instanceCount = 1u;
    cmd.firstIndex    = firstIdx;
    cmd.baseVertex    = 0;
    cmd.baseInstance  = id;

    cmds[slot]   = cmd;
    visible[id]  = 1u;
}
