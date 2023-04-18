//
// Created by welcomeworld on 2022/4/3.
//

#ifndef BIPPLAYER_FDAVIOCONTEXT_H
#define BIPPLAYER_FDAVIOCONTEXT_H

#ifdef __cplusplus
extern "C" {
#endif
#include "libavformat/avio.h"
#include "libavcodec/avcodec.h"
#ifdef __cplusplus
}
#endif

//! Wrapper over AVIOContext for passing the custom I/O.
class FdAVIOContext {
public:
    //! Main constructor.
    FdAVIOContext();

    //! Destructor.
    virtual ~FdAVIOContext();

    //! Close the file.
    void close();

    //! Associate a stream with a file that was previously opened for low-level I/O.
    //! The associated file will be automatically closed on destruction.
    bool openFromDescriptor(int theFD, const char *theMode);

    //! Access AVIO context.
    AVIOContext *getAvioContext() const;

public:

    //! Virtual method for reading the data.
    virtual int read(uint8_t *theBuf,
                     int theBufSize);

    //! Virtual method for writing the data.
    virtual int write(uint8_t *theBuf,
                      int theBufSize);

    //! Virtual method for seeking to new position.
    virtual int64_t seek(int64_t theOffset,
                         int theWhence);

private:
    //! Callback for reading the data.
    static int readCallback(void *theOpaque,
                            uint8_t *theBuf,
                            int theBufSize);

    //! Callback for writing the data.
    static int writeCallback(void *theOpaque,
                             uint8_t *theBuf,
                             int theBufSize);

    //! Callback for seeking to new position.
    static int64_t seekCallback(void *theOpaque,
                                int64_t theOffset,
                                int theWhence);

protected:
    AVIOContext *myAvioCtx = nullptr;
    FILE *myFile = nullptr;
};

#endif //BIPPLAYER_FDAVIOCONTEXT_H
