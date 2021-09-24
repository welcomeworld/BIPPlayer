package com.github.welcomeworld.bipplayer;

import android.app.Application;

import com.tencent.bugly.crashreport.CrashReport;

public class BIPApp extends Application {

    @Override
    public void onCreate() {
        super.onCreate();
        CrashReport.initCrashReport(getApplicationContext(), BuildConfig.buglyAppId, BuildConfig.DEBUG);
    }
}
