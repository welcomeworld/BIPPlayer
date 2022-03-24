package com.github.welcomeworld.bipplayer;

import android.view.Surface;
import android.view.SurfaceHolder;

public interface BIPPlayer {
    void prepareAsync();

    void setOnPreparedListener(OnPreparedListener listener);

    void setSurface(Surface surface);

    void setDataSource(String path);

    void setOnErrorListener(OnErrorListener listener);

    void release();

    void start();

    void reset();

    interface OnPreparedListener {
        void onPrepared(BIPPlayer bp);
    }

    interface OnErrorListener {
        boolean onError(BIPPlayer bp, int what, int extra);
    }

    interface OnCompletionListener {
        void onCompletion(BIPPlayer bp);
    }

    interface OnSeekCompleteListener {
        void onSeekComplete(BIPPlayer bp);
    }

    interface OnBufferingUpdateListener {
        void onBufferingUpdate(BIPPlayer bp, int var2);
    }

    interface OnInfoListener {
        void onInfo(BIPPlayer bp, int what, int extra);
    }

    void stop();

    boolean isPlaying();

    void pause();

    long getDuration();

    long getCurrentPosition();

    int getVideoWidth();

    int getVideoHeight();

    void setDisplay(SurfaceHolder sh);

    void setScreenOnWhilePlaying(boolean screenOn);

    void seekTo(long time);

    void setOnCompletionListener(OnCompletionListener listener);

    void prepareQualityAsync(String path);

    void setOnSeekCompleteListener(OnSeekCompleteListener listener);

    void setOnBufferingUpdateListener(OnBufferingUpdateListener listener);

    void setOption(int category, String name, String value);

    void setOnInfoListener(OnInfoListener listener);

    int getFps();
}
