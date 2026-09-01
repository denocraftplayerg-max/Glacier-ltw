package net.ltw.bridge;

import org.joml.Matrix4f;

/**
 * Extracts 6 frustum planes from a JOML Matrix4f using the Gribb-Hartmann method.
 * Format: float[24] = 6 planes * 4 components (nx, ny, nz, d).
 * Plane equation: Nx*X + Ny*Y + Nz*Z + D >= 0 means INSIDE frustum.
 */
public final class FrustumExtractor {

    private static final float[] PLANES = new float[24];
    private static final Matrix4f MVP_CACHE = new Matrix4f();
    private static final float[] MATRIX_CACHE = new float[16];

    /**
     * Decompose a combined modelView * projection matrix into 6 frustum planes.
     */
    public static synchronized float[] extract(Matrix4f modelView, Matrix4f projection) {
        if (modelView == null || projection == null) return null;

        MVP_CACHE.set(projection).mul(modelView);
        MVP_CACHE.get(MATRIX_CACHE);

        return fromMatrix4f(MATRIX_CACHE);
    }

    /**
     * Decompose a 4x4 column-major matrix into 6 frustum planes.
     * Gribb-Hartmann method.
     */
    public static float[] fromMatrix4f(float[] m) {
        if (m == null || m.length < 16) return null;

        // Column-major extraction
        float r0x = m[0],  r0y = m[4],  r0z = m[8],  r0w = m[12];
        float r1x = m[1],  r1y = m[5],  r1z = m[9],  r1w = m[13];
        float r2x = m[2],  r2y = m[6],  r2z = m[10], r2w = m[14];
        float r3x = m[3],  r3y = m[7],  r3z = m[11], r3w = m[15];

        // LEFT:   row3 + row0
        PLANES[0]  = r3x + r0x;  PLANES[1]  = r3y + r0y;
        PLANES[2]  = r3z + r0z;  PLANES[3]  = r3w + r0w;
        // RIGHT:  row3 - row0
        PLANES[4]  = r3x - r0x;  PLANES[5]  = r3y - r0y;
        PLANES[6]  = r3z - r0z;  PLANES[7]  = r3w - r0w;
        // BOTTOM: row3 + row1
        PLANES[8]  = r3x + r1x;  PLANES[9]  = r3y + r1y;
        PLANES[10] = r3z + r1z;  PLANES[11] = r3w + r1w;
        // TOP:    row3 - row1
        PLANES[12] = r3x - r1x;  PLANES[13] = r3y - r1y;
        PLANES[14] = r3z - r1z;  PLANES[15] = r3w - r1w;
        // NEAR:   row3 + row2
        PLANES[16] = r3x + r2x;  PLANES[17] = r3y + r2y;
        PLANES[18] = r3z + r2z;  PLANES[19] = r3w + r2w;
        // FAR:    row3 - row2
        PLANES[20] = r3x - r2x;  PLANES[21] = r3y - r2y;
        PLANES[22] = r3z - r2z;  PLANES[23] = r3w - r2w;

        return PLANES;
    }
}
