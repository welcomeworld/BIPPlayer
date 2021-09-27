package com.github.welcomeworld.bipplayer.sample;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Environment;
import android.view.SurfaceHolder;
import android.view.View;
import android.widget.SeekBar;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.github.welcomeworld.bipplayer.BIPPlayer;
import com.github.welcomeworld.bipplayer.DefaultBIPPlayer;
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
        bipPlayer.setDisplay(binding.surface.getHolder());
        bipPlayer.setScreenOnWhilePlaying(true);
        binding.videoPrepare.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                File file = new File(Environment.getExternalStorageDirectory().getAbsolutePath(), "test.mp4");
//                bipPlayer.setDataSource(file.getAbsolutePath());
//                bipPlayer.setDataSource("http://stream4.iqilu.com/ksd/video/2020/02/17/c5e02420426d58521a8783e754e9f4e6.mp4");
                bipPlayer.setDataSource("https://stream7.iqilu.com/10339/upload_transcode/202002/18/20200218114723HDu3hhxqIT.mp4");
                bipPlayer.prepareAsync();
            }
        });
        binding.videoStart.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                bipPlayer.start();
            }
        });
        binding.videoPause.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                bipPlayer.pause();
            }
        });
        binding.videoStop.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                bipPlayer.stop();
            }
        });
        binding.videoReset.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                bipPlayer.reset();
            }
        });
        new Thread(() -> {
            while (true) {
                runOnUiThread(() -> {
                    long position = bipPlayer.getCurrentPosition();
                    long duration = bipPlayer.getDuration();
                    binding.position.setText(formatTime(position));
                    binding.duration.setText(formatTime(duration));
                    binding.videoHeight.setText("height:" + bipPlayer.getVideoHeight() + "px");
                    binding.videoWidth.setText("width:" + bipPlayer.getVideoWidth() + "px");
                    binding.seekBar.setProgress((int) (position * 1000 / duration));
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
        bipPlayer.setOnPreparedListener(BIPPlayer::start);
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
}