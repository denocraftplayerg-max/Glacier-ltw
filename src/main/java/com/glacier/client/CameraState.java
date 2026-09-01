package com.glacier.client;

public final class CameraState {

    public double x;
    public double y;
    public double z;
    public float yaw;
    public float pitch;

    public void update(double x, double y, double z,
                        float yaw, float pitch) {
        this.x = x;
        this.y = y;
        this.z = z;
        this.yaw = yaw;
        this.pitch = pitch;
    }
}
