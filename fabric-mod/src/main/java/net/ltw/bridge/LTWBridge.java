package net.ltw.bridge;

import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;

/**
 * FFM (Foreign Function & Memory) API bridge to libltw.so.
 * Zero JNI overhead - direct ARM64 ABI calls via Project Panama.
 */
public final class LTWBridge {
    
    private static boolean loaded = false;
    private static boolean available = false;
    
    // Method handles for native functions (cached for JIT intrinsification)
    private static final MethodHandle MH_UPDATE_FRUSTUM;
    private static final MethodHandle MH_CLEAR_CHUNKS;
    private static final MethodHandle MH_REGISTER_CHUNK;
    private static final MethodHandle MH_INJECT_ASTC;
    private static final MethodHandle MH_SUBMIT_ENTITY_SNAPSHOT;
    private static final MethodHandle MH_UPLOAD_SUN_STATE;
    private static final MethodHandle MH_UPLOAD_LIGHTS;
    private static final MethodHandle MH_SET_FOVEATED;
    private static final MethodHandle MH_INIT;
    
    // Global arena for long-lived buffers
    private static final Arena GLOBAL_ARENA = Arena.ofAuto();
    private static final MemorySegment FRUSTUM_BUFFER = GLOBAL_ARENA.allocate(24 * 4); // 24 floats * 4 bytes

    static {
        MethodHandle tempUpdateFrustum = null;
        MethodHandle tempClearChunks = null;
        MethodHandle tempRegisterChunk = null;
        MethodHandle tempInjectAstc = null;
        MethodHandle tempSubmitEntities = null;
        MethodHandle tempUploadSun = null;
        MethodHandle tempUploadLights = null;
        MethodHandle tempSetFoveated = null;
        MethodHandle tempInit = null;
        
        try {
            System.loadLibrary("ltw"); // Register lib in JVM namespace
            
            Linker linker = Linker.nativeLinker();
            SymbolLookup lookup = SymbolLookup.loaderLookup();
            
            // ltw_bridge_init()
            tempInit = linker.downcallHandle(
                lookup.find("ltw_bridge_init").orElseThrow(),
                FunctionDescriptor.ofVoid()
            );
            
            // ltw_update_frustum(const float*)
            tempUpdateFrustum = linker.downcallHandle(
                lookup.find("ltw_update_frustum").orElseThrow(),
                FunctionDescriptor.ofVoid(ValueLayout.ADDRESS)
            );
            
            // ltw_clear_chunks()
            tempClearChunks = linker.downcallHandle(
                lookup.find("ltw_clear_chunks").orElseThrow(),
                FunctionDescriptor.ofVoid()
            );
            
            // ltw_register_chunk(int, float, float, float)
            tempRegisterChunk = linker.downcallHandle(
                lookup.find("ltw_register_chunk").orElseThrow(),
                FunctionDescriptor.ofVoid(
                    ValueLayout.JAVA_INT,
                    ValueLayout.JAVA_FLOAT,
                    ValueLayout.JAVA_FLOAT,
                    ValueLayout.JAVA_FLOAT
                )
            );
            
            // ltw_inject_astc(const char*, uint32_t)
            tempInjectAstc = linker.downcallHandle(
                lookup.find("ltw_inject_astc").orElseThrow(),
                FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS, ValueLayout.JAVA_INT)
            );

            // ltw_submit_entity_snapshot(const void*, int32_t)
            tempSubmitEntities = linker.downcallHandle(
                lookup.find("ltw_submit_entity_snapshot").orElseThrow(),
                FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_INT)
            );

            // ltw_upload_sun_state(float, float, float, float, float, float, float)
            tempUploadSun = linker.downcallHandle(
                lookup.find("ltw_upload_sun_state").orElseThrow(),
                FunctionDescriptor.ofVoid(
                    ValueLayout.JAVA_FLOAT, ValueLayout.JAVA_FLOAT, ValueLayout.JAVA_FLOAT,
                    ValueLayout.JAVA_FLOAT, ValueLayout.JAVA_FLOAT, ValueLayout.JAVA_FLOAT,
                    ValueLayout.JAVA_FLOAT
                )
            );

            // ltw_upload_lights(const void*, int32_t)
            tempUploadLights = linker.downcallHandle(
                lookup.find("ltw_upload_lights").orElseThrow(),
                FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.JAVA_INT)
            );

            // ltw_set_foveated_config(int32_t, float, float)
            tempSetFoveated = linker.downcallHandle(
                lookup.find("ltw_set_foveated_config").orElseThrow(),
                FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.JAVA_FLOAT, ValueLayout.JAVA_FLOAT)
            );
            
            loaded = true;
            available = true;
            System.out.println("[LTW-Bridge] FFM API linked to libltw.so");
            
        } catch (Throwable t) {
            System.out.println("[LTW-Bridge] FFM linking failed: " + t.getMessage());
            available = false;
        }
        
        MH_INIT = tempInit;
        MH_UPDATE_FRUSTUM = tempUpdateFrustum;
        MH_CLEAR_CHUNKS = tempClearChunks;
        MH_REGISTER_CHUNK = tempRegisterChunk;
        MH_INJECT_ASTC = tempInjectAstc;
        MH_SUBMIT_ENTITY_SNAPSHOT = tempSubmitEntities;
        MH_UPLOAD_SUN_STATE = tempUploadSun;
        MH_UPLOAD_LIGHTS = tempUploadLights;
        MH_SET_FOVEATED = tempSetFoveated;
    }
    
    public static void tryLoad() {
        if (loaded) return;
        // Static initializer already attempted loading
    }
    
    public static boolean isAvailable() {
        return available;
    }
    
    // --- HOT PATHS (Called every frame) ---
    
    public static void updateFrustumPlanes(float[] planes) {
        if (!available || planes == null || planes.length != 24) return;
        
        // Copy directly to off-heap memory
        FRUSTUM_BUFFER.copyFrom(MemorySegment.ofArray(planes));
        
        try {
            MH_UPDATE_FRUSTUM.invokeExact(FRUSTUM_BUFFER);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to update frustum planes", t);
        }
    }
    
    public static void clearChunkPositions() {
        if (!available) return;
        
        try {
            MH_CLEAR_CHUNKS.invokeExact();
        } catch (Throwable t) {
            throw new RuntimeException("Failed to clear chunks", t);
        }
    }
    
    public static void registerChunkPosition(int baseVertex, float x, float y, float z) {
        if (!available) return;
        
        try {
            MH_REGISTER_CHUNK.invokeExact(baseVertex, x, y, z);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to register chunk", t);
        }
    }

    public static void submitEntitySnapshot(MemorySegment entityBuffer, int entityCount) {
        if (!available || entityBuffer == null || entityCount <= 0) return;

        try {
            MH_SUBMIT_ENTITY_SNAPSHOT.invokeExact(entityBuffer, entityCount);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to submit entity snapshot", t);
        }
    }

    public static void uploadSunAndCamera(float sunX, float sunY, float sunZ, float timeOfDay, float camX, float camY, float camZ) {
        if (!available) return;

        try {
            MH_UPLOAD_SUN_STATE.invokeExact(sunX, sunY, sunZ, timeOfDay, camX, camY, camZ);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to upload sun state", t);
        }
    }

    public static void uploadPointLights(MemorySegment lightsBuffer, int lightCount) {
        if (!available || lightsBuffer == null || lightCount <= 0) return;

        try {
            MH_UPLOAD_LIGHTS.invokeExact(lightsBuffer, lightCount);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to upload point lights", t);
        }
    }

    public static void setFoveatedRendering(boolean enabled, float innerRadius, float middleRadius) {
        if (!available) return;

        try {
            MH_SET_FOVEATED.invokeExact(enabled ? 1 : 0, innerRadius, middleRadius);
        } catch (Throwable t) {
            throw new RuntimeException("Failed to configure foveated rendering", t);
        }
    }
    
    // --- COLD PATHS (Textures) ---
    
    public static boolean loadExternalAstc(String resourcePath, int glId) {
        if (!available || resourcePath == null) return false;
        
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment cString = arena.allocateUtf8String(resourcePath);
            int result = (int) MH_INJECT_ASTC.invokeExact(cString, glId);
            return result == 1;
        } catch (Throwable t) {
            return false;
        }
    }
    
    // --- LEGACY COMPATIBILITY (for existing mixins) ---
    
    public static boolean hasFrustumPlanes() {
        return available; // Simplified: if bridge is available, frustum is tracked
    }
}
