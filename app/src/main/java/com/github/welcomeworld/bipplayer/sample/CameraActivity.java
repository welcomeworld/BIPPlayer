package com.github.welcomeworld.bipplayer.sample;

import android.Manifest;
import android.annotation.SuppressLint;
import android.content.pm.PackageManager;
import android.media.AudioFormat;
import android.media.AudioRecord;
import android.media.MediaRecorder;
import android.os.Bundle;
import android.util.Log;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.camera.core.CameraSelector;
import androidx.camera.core.ImageAnalysis;
import androidx.camera.core.ImageProxy;
import androidx.camera.core.Preview;
import androidx.camera.lifecycle.ProcessCameraProvider;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import com.github.welcomeworld.bipplayer.DefaultBipPublisher;
import com.github.welcomeworld.bipplayer.sample.databinding.ActivityCameraBinding;
import com.google.common.util.concurrent.ListenableFuture;

import java.nio.ByteBuffer;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class CameraActivity extends AppCompatActivity {
    private static final String TAG = "CameraActivity";
    private ActivityCameraBinding binding;
    private String publishUrl = null;
    ExecutorService cameraExecutor = null;
    DefaultBipPublisher bipPublisher = new DefaultBipPublisher();
    AudioRecord audioRecord;
    boolean publish = false;
    int bufferSizeInBytes = 0;
    boolean audioStart = false;
    long startRecordTime = 0;


    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        publishUrl = getIntent().getStringExtra("publishUrl");
        if (publishUrl == null) {
            finish();
            return;
        }
        Log.e(TAG, "recreate");
        checkPermission();
        binding = ActivityCameraBinding.inflate(getLayoutInflater());
        setContentView(binding.getRoot());
        binding.videoPublishButton.setOnClickListener(v -> publish());
        cameraExecutor = Executors.newSingleThreadExecutor();
        createAudioRecord();
        new Thread(() -> {
            bipPublisher.setFps(30);
            bipPublisher.setPublishVideoInfo(640, 480);
            bipPublisher.prepare(null, publishUrl);
            try {
                Thread.sleep(500);
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
            publish = true;
        }).start();
        startCamera();
    }

    private void startRecord() {
        audioStart = true;
        new Thread(() -> {
            int ffmpegSize = 1024 * 2 * 2;
            byte[] audioData = new byte[ffmpegSize];
            int readOffset = 0;
            while (publish) {
                int readSize = audioRecord.read(audioData, readOffset, ffmpegSize - readOffset);
                if (readSize == AudioRecord.ERROR_INVALID_OPERATION || readSize == AudioRecord.ERROR_BAD_VALUE) {
                    Log.e(TAG, "record error:" + readSize);
                    continue;
                }
                readOffset += readSize;
                Log.e(TAG, "record size" + readOffset);
                if (readOffset == ffmpegSize) {
                    readOffset = 0;
                    bipPublisher.writeAudio(audioData, ffmpegSize);
                } else {
                    Log.e(TAG, "record size not write");
                }
            }
        }).start();
    }

    @SuppressLint("MissingPermission")
    private void createAudioRecord() {
        bufferSizeInBytes = AudioRecord.getMinBufferSize(44100, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_16BIT);
        audioRecord = new AudioRecord(MediaRecorder.AudioSource.MIC, 44100, AudioFormat.CHANNEL_IN_STEREO, AudioFormat.ENCODING_PCM_16BIT, bufferSizeInBytes);
        audioRecord.startRecording();
    }

    private void publish() {
        if (bipPublisher != null) {
            publish = false;
            audioRecord.stop();
            Log.e("DefaultBIPPlayerNative", "record time:" + (System.currentTimeMillis() - startRecordTime));
            bipPublisher.writeCompleted();
            finish();
        }
    }

    private void startCamera() {
        ListenableFuture<ProcessCameraProvider> processCameraProviderFuture = ProcessCameraProvider.getInstance(this);
        processCameraProviderFuture.addListener(() -> {
            try {
                ProcessCameraProvider processCameraProvider = processCameraProviderFuture.get();
                Preview preview = new Preview.Builder().build();
                preview.setSurfaceProvider(binding.viewFinder.getSurfaceProvider());
                ImageAnalysis imageAnalysis = new ImageAnalysis.Builder().build();
                imageAnalysis.setAnalyzer(cameraExecutor, new LuminosityAnalyzer());
                //解除所有绑定
                processCameraProvider.unbindAll();
                //重新绑定
                processCameraProvider.bindToLifecycle(CameraActivity.this, CameraSelector.DEFAULT_BACK_CAMERA, preview, imageAnalysis);
            } catch (ExecutionException | InterruptedException e) {
                e.printStackTrace();
            }

        }, ContextCompat.getMainExecutor(this));
    }

    private class LuminosityAnalyzer implements ImageAnalysis.Analyzer {
        ByteBuffer yuvBuffer;


        @Override
        public void analyze(@NonNull ImageProxy image) {
            int width = image.getWidth();
            int height = image.getHeight();
            if (yuvBuffer == null) {
                yuvBuffer = ByteBuffer.allocate(width * height * 3 / 2);
            } else {
                yuvBuffer.clear();
            }
            //y数据
            ByteBuffer yBuffer = image.getPlanes()[0].getBuffer();
            ByteBuffer uBuffer = image.getPlanes()[1].getBuffer();
            ByteBuffer vBuffer = image.getPlanes()[2].getBuffer();
            byte[] row;
            ImageProxy.PlaneProxy[] planes = image.getPlanes();

            int pixStride = planes[1].getPixelStride();
            if (pixStride == 1) {
                row = new byte[yBuffer.remaining()];
                yBuffer.get(row);
                yuvBuffer.put(row);
                row = new byte[uBuffer.remaining()];
                uBuffer.get(row);
                yuvBuffer.put(row);
                row = new byte[vBuffer.remaining()];
                vBuffer.get(row);
                yuvBuffer.put(row);
            } else if (pixStride == 2) {
                row = new byte[width * height];
                yBuffer.get(row);
                yuvBuffer.put(row);
                yuvBuffer.put(vBuffer.get());
                row = new byte[uBuffer.remaining()];
                uBuffer.get(row);
                yuvBuffer.put(row);
            }
            if (bipPublisher != null && publish) {
                if (!audioStart) {
                    startRecordTime = System.currentTimeMillis();
                    startRecord();
                }
                bipPublisher.writeImage(yuvBuffer.array(), width, height);
            }
            image.close();
        }
    }

    private void checkPermission() {
        String[] permissions = {Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO};
        for (String permission : permissions) {
            int permissionCode = ContextCompat.checkSelfPermission(this, permission);
            if (permissionCode != PackageManager.PERMISSION_GRANTED) {
                try {
                    ActivityCompat.requestPermissions(
                            this, new String[]{permission}, 666);
                } catch (Exception e) {
                    e.printStackTrace();
                }
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == 666) {
            if (grantResults[0] != PackageManager.PERMISSION_GRANTED) {
                Toast.makeText(this,
                        "Permissions not granted by the user.",
                        Toast.LENGTH_SHORT).show();
                finish();
            }
        }
    }
}