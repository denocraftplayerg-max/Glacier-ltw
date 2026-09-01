#include "foveated_renderer.hpp"
#include "gpu_driven.hpp"
#include "multi_draw_indirect.hpp"
#include "light_engine.hpp"
#include "tess_lod.hpp"
#include "shadow_system.hpp"

static FoveatedRenderer          fovea;
static GpuDrivenRenderer         gpuDriven;
static MultiDrawIndirectEmulated mdi;
static LightEngine               lights;
static TessLODSystem             tessLOD;
static ShadowSystem              shadows;

void HYENGRA_Init(int W, int H) {
    FoveaConfig fc;
    fc.screenW=W; fc.screenH=H;
    fc.innerRadius=0.28f; fc.middleRadius=0.55f;
    fc.innerScale=1.0f; fc.middleScale=0.55f; fc.outerScale=0.25f;
    fovea.init(fc, true);

    gpuDriven.init(8192);
    lights.init();
    tessLOD.init();

    ShadowConfig sc;
    sc.innerResolution = 2048;
    sc.midResolution   = 1024;
    sc.outerResolution = 512;
    sc.cascadeSplits[0]= 24.0f;
    sc.cascadeSplits[1]= 80.0f;
    sc.cascadeSplits[2]= 256.0f;
    sc.darkness        = 0.88f;
    sc.shadowBias      = 0.002f;
    shadows.init(sc);
}

struct HYENGRAFrame {
    float frustumPlanes[24];
    float sunDir[3];
    float camPos[3];
    float timeOfDay;
    std::vector<ObjectData>  objects;
    std::vector<Light>       lights;
    GLuint chunkVAO, chunkIBO;
    GLuint terrainVAO, terrainIBO;
    int    chunkIndexCount;
    int    terrainIndexCount;
};

void HYENGRA_RenderFrame(const HYENGRAFrame& f) {
    gpuDriven.uploadObjects(f.objects);
    gpuDriven.runCulling(f.frustumPlanes);

    shadows.updateCascades(f.sunDir, f.camPos, nullptr, 70.0f, 16.f/9.f, 0.1f);

    shadows.renderCascade(0, f.chunkVAO, f.chunkIBO, f.chunkIndexCount);
    shadows.renderCascade(1, f.chunkVAO, f.chunkIBO, f.chunkIndexCount);
    shadows.renderCascade(2, f.chunkVAO, f.chunkIBO, f.chunkIndexCount);

    lights.uploadLights(f.lights);

    // OUTER ZONE
    fovea.beginZone(FoveaZone::OUTER);
    {
        glUseProgram(0);
        mdi.draw(gpuDriven.drawCmdSSBO, gpuDriven.drawCountBuffer, f.chunkVAO, f.chunkIBO, 8192);
        shadows.bindForLighting(lights.lightingPass);
        lights.renderLightingPass(const_cast<float*>(f.sunDir), f.timeOfDay);
    }
    fovea.endZone();

    // MIDDLE ZONE
    fovea.beginZone(FoveaZone::MIDDLE);
    {
        mdi.draw(gpuDriven.drawCmdSSBO, gpuDriven.drawCountBuffer, f.chunkVAO, f.chunkIBO, 8192);
        TessConfig tc;
        tc.maxTessLevel=4.0f; tc.nearDist=16.0f; tc.farDist=80.0f; tc.lodBias=16.0f;
        tessLOD.bind(tc, f.camPos);
        glDrawElements(GL_PATCHES, f.terrainIndexCount, GL_UNSIGNED_INT, nullptr);
        shadows.bindForLighting(lights.lightingPass);
        lights.renderLightingPass(const_cast<float*>(f.sunDir), f.timeOfDay);
    }
    fovea.endZone();

    // INNER ZONE
    fovea.beginZone(FoveaZone::INNER);
    {
        mdi.draw(gpuDriven.drawCmdSSBO, gpuDriven.drawCountBuffer, f.chunkVAO, f.chunkIBO, 8192);
        TessConfig tc;
        tc.maxTessLevel=8.0f; tc.nearDist=8.0f; tc.farDist=64.0f; tc.lodBias=0.0f;
        tessLOD.bind(tc, f.camPos);
        glBindVertexArray(f.terrainVAO);
        glDrawElements(GL_PATCHES, f.terrainIndexCount, GL_UNSIGNED_INT, nullptr);
        shadows.bindForLighting(lights.lightingPass);
        lights.renderLightingPass(const_cast<float*>(f.sunDir), f.timeOfDay);
    }
    fovea.endZone();

    fovea.composite();
}

void HYENGRA_Destroy() {
    fovea.destroy();
    gpuDriven.destroy();
    lights.destroy();
    tessLOD.destroy();
    shadows.destroy();
}
