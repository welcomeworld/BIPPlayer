package com.github.welcomeworld.bipplayer;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.os.Bundle;
import android.os.Environment;
import android.view.SurfaceHolder;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.github.welcomeworld.bipplayer.databinding.ActivityMainBinding;

import java.io.File;
import java.text.SimpleDateFormat;

public class MainActivity extends AppCompatActivity {
    BIPPlayer bipPlayer = new DefaultBIPPlayer();

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
        new Thread(() -> {
            File file = new File(Environment.getExternalStorageDirectory().getAbsolutePath(), "test.mp4");
            bipPlayer.setDataSource(file.getAbsolutePath());
            bipPlayer.prepareAsync();
        }).start();
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
    }

    private static String formatTime(long time) {
        SimpleDateFormat format = new SimpleDateFormat("mm:ss");
        return format.format(time);
    }
}