package com.github.welcomeworld.bipplayer;

import android.view.Surface;
import android.view.SurfaceHolder;

public class DefaultBIPPlayer implements BIPPlayer {
    private SurfaceHolder mSurfaceHolder;
    private String mDataSource;
    // Used to load the 'native-lib' library on application startup.
    static {
        System.loadLibrary("native-lib");
    }

    @Override
    public void prepareAsync() {
        play(mDataSource);
    }

    @Override
    public void setOnPreparedListener(OnPreparedListener listener) {

    }

    @Override
    public void setSurface(Surface surface) {
        this.mSurfaceHolder = null;
        _setVideoSurface(surface);
    }

    @Override
    public void setDataSource(String path) {
        this.mDataSource = path;
    }

    @Override
    public void setOnErrorListener(OnErrorListener listener) {

    }

    @Override
    public void release() {

    }

    @Override
    public void start() {

    }

    @Override
    public void reset() {

    }

    @Override
    public void updatePlayer() {

    }

    @Override
    public boolean isPlaying() {
        return false;
    }

    @Override
    public void pause() {

    }

    @Override
    public long getDuration() {
        return 0;
    }

    @Override
    public long getCurrentPosition() {
        return 0;
    }

    @Override
    public int getVideoWidth() {
        return 0;
    }

    @Override
    public int getVideoHeight() {
        return 0;
    }

    @Override
    public void setDisplay(SurfaceHolder sh) {
        this.mSurfaceHolder = sh;
        Surface surface;
        if (sh != null) {
            surface = sh.getSurface();
        } else {
            surface = null;
        }
        _setVideoSurface(surface);
    }

    @Override
    public void setScreenOnWhilePlaying(boolean screenOn) {

    }

    @Override
    public void seekTo(long time) {

    }

    @Override
    public void setOnCompletionListener(OnCompletionListener listener) {

    }

    @Override
    public void prepareQualityAsync(String path) {

    }

    @Override
    public void setOnSeekCompleteListener(OnSeekCompleteListener listener) {

    }



    public native int play(String path);

    private native void _setVideoSurface(Surface surface);


}
