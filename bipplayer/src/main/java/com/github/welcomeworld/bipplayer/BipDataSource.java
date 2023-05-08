package com.github.welcomeworld.bipplayer;

import android.os.ParcelFileDescriptor;

public class BipDataSource {
    String source;
    boolean isSingleSource = true;
    boolean isSync = false;
    boolean videoEnable = true;
    boolean audioEnable = true;
    long seekPosition = 0;
    long startOffset = 0;
    ParcelFileDescriptor keepFD;
}
