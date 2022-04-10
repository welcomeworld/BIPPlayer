# BIPPlayer
in project build.gradle
```gradle
allprojects {
 repositories {
    ...
    maven { url "https://jitpack.io" }
 }
}
  ```
  
  in module build.gradle
  ```gradle
  dependencies {
    implementation 'com.github.welcomeworld:BIPPlayer:version'
}
  ```
## ToDo
-边播边缓存（seek复用缓存）  
~~-支持多种方式设置DataSource(复刻MediaPlayer)~~  
~~-使用MediaCodeC硬解码~~
-支持变速播放