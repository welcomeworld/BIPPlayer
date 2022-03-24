package com.github.welcomeworld.bipplayer;

import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;

import androidx.annotation.NonNull;

import java.lang.ref.WeakReference;

public final class DefaultBIPPlayer implements BIPPlayer {
    private static final int MEDIA_PREPARED = 1;
    private static final int MEDIA_PLAYBACK_COMPLETE = 2;
    private static final int MEDIA_BUFFERING_UPDATE = 3;
    private static final int MEDIA_SEEK_COMPLETE = 4;
    private static final int MEDIA_ERROR = 5;
    private static final int MEDIA_PLAY_STATE_CHANGE = 6;
    private static final int MEDIA_INFO = 7;
    private static final int MEDIA_PLAYER_MESSAGE = 8;

    public static final int OPT_CATEGORY_FORMAT = 1;
    public static final int OPT_CATEGORY_CODEC = 2;
    public static final int OPT_CATEGORY_SWS = 3;
    public static final int OPT_CATEGORY_PLAYER = 4;

    private static final int MEDIA_INFO_BUFFERING_START = 0;
    private static final int MEDIA_INFO_BUFFERING_END = 1;
    private static final int MEDIA_INFO_FPS = 2;

    private SurfaceHolder mSurfaceHolder;
    private boolean mScreenOnWhilePlaying = true;
    private String mDataSource;
    private final EventHandler mEventHandler;
    private OnPreparedListener mOnPreparedListener;
    private OnCompletionListener mOnCompletionListener;
    private OnBufferingUpdateListener mOnBufferingUpdateListener;
    private OnSeekCompleteListener mOnSeekCompleteListener;
    private OnErrorListener mOnErrorListener;
    private OnInfoListener mOnInfoListener;
    @SuppressWarnings("unused")
    private long nativePlayerRef;
    private boolean isPlaying;

    static {
        System.loadLibrary("native-lib");
    }

    public DefaultBIPPlayer() {
        Looper looper;
        if ((looper = Looper.myLooper()) != null) {
            mEventHandler = new EventHandler(this, looper);
        } else if ((looper = Looper.getMainLooper()) != null) {
            mEventHandler = new EventHandler(this, looper);
        } else {
            mEventHandler = null;
        }
        native_setup(new WeakReference<>(this));
    }

    @Override
    public void prepareAsync() {
        _prepareAsync(mDataSource);
    }

    @Override
    public void setSurface(Surface surface) {
        mSurfaceHolder = null;
        _setVideoSurface(surface);
        updateSurfaceScreenOn();
    }

    @Override
    public void setDataSource(String path) {
        mDataSource = path;
    }

    @Override
    public void release() {
        releaseListeners();
        _release();
    }

    private native void _release();

    private native void _prepare_next(String path, boolean dash);

    @Override
    public native void start();

    @Override
    public void reset() {
        mEventHandler.removeCallbacksAndMessages(null);
        _reset();
    }

    private native void _reset();

    @Override
    public native boolean isPlaying();

    @Override
    public native void pause();

    @Override
    public native long getDuration();

    @Override
    public native long getCurrentPosition();

    @Override
    public native int getVideoWidth();

    @Override
    public native int getVideoHeight();

    @Override
    public void setDisplay(SurfaceHolder sh) {
        mSurfaceHolder = sh;
        Surface surface;
        if (sh != null) {
            surface = sh.getSurface();
        } else {
            surface = null;
        }
        _setVideoSurface(surface);
        updateSurfaceScreenOn();
    }

    @Override
    public void setScreenOnWhilePlaying(boolean screenOn) {
        if (mScreenOnWhilePlaying != screenOn) {
            mScreenOnWhilePlaying = screenOn;
            updateSurfaceScreenOn();
        }
    }

    private void setPlaying(boolean isPlaying) {
        this.isPlaying = isPlaying;
        updateSurfaceScreenOn();
    }

    private void updateSurfaceScreenOn() {
        if (mSurfaceHolder != null) {
            mSurfaceHolder.setKeepScreenOn(mScreenOnWhilePlaying && isPlaying);
        }
    }

    @Override
    public native void seekTo(long time);

    @Override
    public native void stop();

    @Override
    public void prepareQualityAsync(String path) {
        _prepare_next(path, true);
    }

    public void prepareNextAsync(String path) {
        _prepare_next(path, false);
    }

    @Override
    public void setOnPreparedListener(OnPreparedListener listener) {
        mOnPreparedListener = listener;
    }

    protected final void notifyOnPrepared() {
        if (mOnPreparedListener != null)
            mOnPreparedListener.onPrepared(this);
    }

    @Override
    public void setOnErrorListener(OnErrorListener listener) {
        mOnErrorListener = listener;
    }

    public void setOnInfoListener(OnInfoListener listener) {
        mOnInfoListener = listener;
    }

    protected final void notifyOnError(int what, int extra) {
        if (mOnErrorListener != null)
            mOnErrorListener.onError(this, what, extra);
    }

    protected final void notifyOnInfo(int what, int extra) {
        if (mOnInfoListener != null)
            mOnInfoListener.onInfo(this, what, extra);
    }

    @Override
    public void setOnCompletionListener(OnCompletionListener listener) {
        mOnCompletionListener = listener;
    }

    private void notifyOnCompletion() {
        if (mOnCompletionListener != null)
            mOnCompletionListener.onCompletion(this);
    }

    @Override
    public void setOnSeekCompleteListener(OnSeekCompleteListener listener) {
        mOnSeekCompleteListener = listener;
    }

    protected final void notifyOnSeekComplete() {
        if (mOnSeekCompleteListener != null)
            mOnSeekCompleteListener.onSeekComplete(this);
    }

    @Override
    public void setOnBufferingUpdateListener(OnBufferingUpdateListener listener) {
        mOnBufferingUpdateListener = listener;
    }

    protected final void notifyOnBufferingUpdate(int percent) {
        if (mOnBufferingUpdateListener != null)
            mOnBufferingUpdateListener.onBufferingUpdate(this, percent);
    }

    public native int _prepareAsync(String path);

    private native void _setVideoSurface(Surface surface);

    private native void native_setup(Object weak_this);

    private native void native_finalize();

    private static class EventHandler extends Handler {
        private final WeakReference<DefaultBIPPlayer> mWeakPlayer;

        public EventHandler(DefaultBIPPlayer bip, Looper looper) {
            super(looper);
            mWeakPlayer = new WeakReference<>(bip);
        }

        @Override
        public void handleMessage(@NonNull Message msg) {
            DefaultBIPPlayer bip = mWeakPlayer.get();
            if (bip == null) {
                return;
            }
            Log.e("DefaultBIPPlayer", msg.what + "arg1:" + msg.arg1 + "arg2:" + msg.arg2);
            switch (msg.what) {
                case MEDIA_BUFFERING_UPDATE:
                    bip.notifyOnBufferingUpdate(msg.arg1);
                    break;
                case MEDIA_PREPARED:
                    bip.notifyOnPrepared();
                    break;
                case MEDIA_PLAYBACK_COMPLETE:
                    bip.notifyOnCompletion();
                    break;
                case MEDIA_SEEK_COMPLETE:
                    bip.notifyOnSeekComplete();
                    break;
                case MEDIA_ERROR:
                    bip.notifyOnError(msg.arg1, msg.arg2);
                    break;
                case MEDIA_PLAY_STATE_CHANGE:
                    bip.setPlaying(msg.arg1 == 1);
                    break;
                case MEDIA_INFO:
                    bip.notifyOnInfo(msg.arg1, msg.arg2);
                    break;
                case MEDIA_PLAYER_MESSAGE:
                    break;
                default:

            }
        }
    }

    public static void postEventFromNative(Object weakThis, int what,
                                           int arg1, int arg2, Object obj) {
        if (weakThis == null) {
            return;
        }
        @SuppressWarnings("unchecked")
        DefaultBIPPlayer bipPlayer = ((WeakReference<DefaultBIPPlayer>) weakThis).get();
        if (bipPlayer == null) {
            return;
        }
        if (bipPlayer.mEventHandler != null) {
            Message msg = bipPlayer.mEventHandler.obtainMessage(what, arg1, arg2, obj);
            bipPlayer.mEventHandler.sendMessage(msg);
        }
    }

    private void releaseListeners() {
        mOnBufferingUpdateListener = null;
        mOnSeekCompleteListener = null;
        mOnCompletionListener = null;
        mOnPreparedListener = null;
        mOnErrorListener = null;
        mOnInfoListener = null;
    }

    @Override
    protected void finalize() throws Throwable {
        super.finalize();
        native_finalize();
    }

    /**
     * should call before call prepare
     */
    @Override
    public native void setOption(int category, String name, String value);

    /**
     * should call after call prepare
     * get fixed fps,not real fps
     */
    @Override
    public native int getFps();
}
