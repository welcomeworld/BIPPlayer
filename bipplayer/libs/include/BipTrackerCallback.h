//
// Created by welcomeworld on 3/16/23.
//

#ifndef BIPPLAYER_BIPTRACKERCALLBACK_H
#define BIPPLAYER_BIPTRACKERCALLBACK_H

class BipTrackerCallback {
public:
    virtual void notifyCompleted() = 0;

    virtual void reportFps(int fps) = 0;

    virtual void requestBuffering() = 0;
};

#endif //BIPPLAYER_BIPTRACKERCALLBACK_H
