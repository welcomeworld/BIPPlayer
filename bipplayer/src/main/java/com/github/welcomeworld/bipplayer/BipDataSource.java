package com.github.welcomeworld.bipplayer;

import android.os.ParcelFileDescriptor;

public class BipDataSource {
    public String source;
    public boolean isSingleSource = true;
    public boolean isSync = false;
    public boolean videoEnable = true;
    public boolean audioEnable = true;
    public long seekPosition = 0;
    public long startOffset = 0;
    public ParcelFileDescriptor keepFD;
}
