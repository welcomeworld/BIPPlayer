package com.github.welcomeworld.bipplayer.sample;

import android.Manifest;
import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.AssetFileDescriptor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.view.SurfaceHolder;
import android.view.View;
import android.widget.SeekBar;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.github.welcomeworld.bipplayer.DefaultBIPPlayer;
import com.github.welcomeworld.bipplayer.DefaultBipPublisher;
import com.github.welcomeworld.bipplayer.sample.databinding.ActivityMainBinding;

import java.io.File;
import java.text.SimpleDateFormat;

public class MainActivity extends AppCompatActivity {
    DefaultBIPPlayer bipPlayer = new DefaultBIPPlayer();

    private ActivityMainBinding binding;

    private static boolean checkPermission(Activity activity, String... permissions) {
        boolean permissionGranted = true;
        for (String permission : permissions) {
            int permissionCode = ContextCompat.checkSelfPermission(activity, permission);
            if (permissionCode != PackageManager.PERMISSION_GRANTED) {
                permissionGranted = false;
                try {
                    ActivityCompat.requestPermissions(
                            activity, new String[]{permission}, 666);
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        }
        return permissionGranted;
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        binding = ActivityMainBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());
        checkPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE);
        bipPlayer.setOption(DefaultBIPPlayer.OPT_CATEGORY_FORMAT, "allowed_extensions", "ALL");
        bipPlayer.setOption(DefaultBIPPlayer.OPT_CATEGORY_FORMAT, "reconnect", "1");
        bipPlayer.setOption(DefaultBIPPlayer.OPT_CATEGORY_PLAYER, "start-on-prepared", "1");
        bipPlayer.setOption(DefaultBIPPlayer.OPT_CATEGORY_FORMAT, "user_agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/94.0.4606.81 Safari/537.36");
        binding.surface.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(@NonNull SurfaceHolder holder) {

            }

            @Override
            public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {
                bipPlayer.setDisplay(holder);
            }

            @Override
            public void surfaceDestroyed(@NonNull SurfaceHolder holder) {

            }
        });
        bipPlayer.setOnInfoListener((bp, what, extra) -> {
            switch (what) {
                case 0:
                    binding.buffering.setVisibility(View.VISIBLE);
                    break;
                case 1:
                    binding.buffering.setVisibility(View.GONE);
                    break;
                case 2:
                    String fps = extra + "fps";
                    binding.videoFps.setText(fps);
                    break;
            }
        });
        bipPlayer.setDisplay(binding.surface.getHolder());
        bipPlayer.setScreenOnWhilePlaying(true);
        bipPlayer.setOnBufferingUpdateListener((bp, var2) -> binding.seekBar.setSecondaryProgress(var2 * 10));
        binding.videoPrepare.setOnClickListener(v -> {
            File file = new File(Environment.getExternalStorageDirectory().getAbsolutePath(), "jiandao.mp4");
            bipPlayer.setDataSource(file.getAbsolutePath());
//                bipPlayer.setDataSource("http://stream4.iqilu.com/ksd/video/2020/02/17/c5e02420426d58521a8783e754e9f4e6.mp4");
//                bipPlayer.setDataSource("https://stream7.iqilu.com/10339/upload_transcode/202002/18/20200218114723HDu3hhxqIT.mp4");
            bipPlayer.prepareAsync();
        });
        binding.videoPublishCamera.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {

            }
        });
        binding.videoPublishFile.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                Intent intent = new Intent("android.intent.action.GET_CONTENT");
                intent.setType("video/*");
                startActivityForResult(intent, 234);
            }
        });
        binding.videoPublishUrl.setText("rtmp://192.168.0.101/live/test_rtmp");
        binding.videoPlayUrl.setText("rtmp://192.168.0.101/live/test_rtmp");
        binding.videoPlayRtmp.setOnClickListener(v -> {
            if (binding.videoPlayUrl.getText() != null) {
                bipPlayer.setDataSource(binding.videoPlayUrl.getText().toString());
                bipPlayer.prepareAsync();
            }
        });
        binding.videoQuality.setOnClickListener(v -> {
            File file = new File(Environment.getExternalStorageDirectory().getAbsolutePath(), "jiandao_videoonly.mp4");
            bipPlayer.setDataSource(file.getAbsolutePath());
            bipPlayer.prepareQualityAsync(file.getAbsolutePath());
        });
        binding.videoNext.setOnClickListener(v -> {
            File file = new File(Environment.getExternalStorageDirectory().getAbsolutePath(), "tescc.m3u8");
            bipPlayer.setDataSource(file.getAbsolutePath());
            bipPlayer.prepareNextAsync(file.getAbsolutePath());
        });

        binding.videoStart.setOnClickListener(v -> bipPlayer.start());
        binding.videoPause.setOnClickListener(v -> bipPlayer.pause());
        binding.videoStop.setOnClickListener(v -> bipPlayer.stop());
        binding.videoReset.setOnClickListener(v -> bipPlayer.reset());
        binding.videoRelease.setOnClickListener(v -> bipPlayer.release());
        binding.videoFile.setOnClickListener(v -> {
            Intent intent = new Intent("android.intent.action.GET_CONTENT");
            intent.setType("video/*");
            startActivityForResult(intent, 233);
        });
        new Thread(() -> {
            while (bipPlayer != null) {
                runOnUiThread(() -> {
                    if (bipPlayer == null) {
                        return;
                    }
                    long position = bipPlayer.getCurrentPosition();
                    long duration = bipPlayer.getDuration();
                    binding.position.setText(formatTime(position));
                    binding.duration.setText(formatTime(duration));
                    binding.videoHeight.setText("height:" + bipPlayer.getVideoHeight() + "px");
                    binding.videoWidth.setText("width:" + bipPlayer.getVideoWidth() + "px");
                    if (duration != 0) {
                        binding.seekBar.setProgress((int) (position * 1000 / duration));
                    }
                });
                try {
                    Thread.sleep(500);
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            }
        }).start();
        binding.seekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
            }

            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {

            }

            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                bipPlayer.seekTo(seekBar.getProgress() * bipPlayer.getDuration() / 1000);
            }
        });
        binding.videoSpeed025.setOnClickListener(v -> bipPlayer.setSpeed(0.25f));
        binding.videoSpeed05.setOnClickListener(v -> bipPlayer.setSpeed(0.5f));
        binding.videoSpeed075.setOnClickListener(v -> bipPlayer.setSpeed(0.75f));
        binding.videoSpeed1.setOnClickListener(v -> bipPlayer.setSpeed(1f));
        binding.videoSpeed125.setOnClickListener(v -> bipPlayer.setSpeed(1.25f));
        binding.videoSpeed15.setOnClickListener(v -> bipPlayer.setSpeed(1.5f));
        binding.videoSpeed175.setOnClickListener(v -> bipPlayer.setSpeed(1.75f));
        binding.videoSpeed2.setOnClickListener(v -> bipPlayer.setSpeed(2));
    }

    private static String formatTime(long time) {
        SimpleDateFormat format = new SimpleDateFormat("mm:ss");
        return format.format(time);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (bipPlayer != null) {
            bipPlayer.release();
            bipPlayer = null;
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        if (requestCode == 233) {
            if (resultCode == RESULT_OK && data != null) {
                Uri uri = data.getData();
                Log.e("file select :", uri.toString());
                bipPlayer.stop();
                bipPlayer.setDataSource(this, uri);
                bipPlayer.prepareAsync();
            } else {
                Log.e("file select :", "fail");
            }
        } else if (requestCode == 234) {
            if (resultCode == RESULT_OK && data != null) {
                Uri uri = data.getData();
                Log.e("file select :", uri.toString());
                bipPlayer.stop();
                if (binding.videoPlayUrl.getText() != null) {
                    ContentResolver resolver = getContentResolver();
                    try (AssetFileDescriptor fd = resolver.openAssetFileDescriptor(uri, "r")) {
                        if (fd != null) {
                            try (ParcelFileDescriptor pfd = ParcelFileDescriptor.dup(fd.getFileDescriptor())) {
//                                bipPlayer._setOutputPath("fd:" + pfd.detachFd(), getExternalFilesDir(null)+"/test_publish.flv");
                                new DefaultBipPublisher()._setOutputPath("fd:" + pfd.detachFd(), binding.videoPublishUrl.getText().toString());
                            } catch (Exception ignore) {
                            }
                        }
                    } catch (Exception ignored) {
                    }
                }
            } else {
                Log.e("file select :", "fail");
            }
        } else {
            Log.e("file select :", "code error");
            super.onActivityResult(requestCode, resultCode, data);
        }
    }
}