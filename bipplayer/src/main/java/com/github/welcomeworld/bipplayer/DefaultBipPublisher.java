package com.github.welcomeworld.bipplayer;

public class DefaultBipPublisher {
    @SuppressWarnings("unused")
    private long nativePublisherRef;

    public DefaultBipPublisher() {
        native_setup();
    }

    private native void native_setup();

    public native void writeImage(byte[] yuvData, int width, int height);

    public native void writeAudio(byte[] audioData, int dataSize);

    public native void prepare(String inputUrl, String publishUrl);

    public native void setFps(int fps);

    public native void setPublishVideoInfo(int width, int height);

    public native void writeCompleted();
}
