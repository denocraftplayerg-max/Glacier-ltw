package com.glacier.client;

public final class RenderFrame {

    public long frameId;
    public final CameraState camera = new CameraState();
    public long frameStartNs;

    public void begin(double x, double y, double z,
                      float yaw, float pitch) {
        frameId++;
        frameStartNs = System.nanoTime();
        camera.update(x, y, z, yaw, pitch);
    }
}
