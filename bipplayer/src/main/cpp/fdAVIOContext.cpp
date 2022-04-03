//
// Created by welcomeworld on 2022/4/3.
//

#include "fdAVIOContext.h"

FdAVIOContext::FdAVIOContext() {
    const int aBufferSize = 32768;
    auto *aBufferIO = (unsigned char *) av_malloc(
            aBufferSize + AV_INPUT_BUFFER_PADDING_SIZE);
    myAvioCtx = avio_alloc_context(aBufferIO, aBufferSize, 0, this, readCallback,
                                   writeCallback, seekCallback);
}

FdAVIOContext::~FdAVIOContext() {
    close();
    if (myAvioCtx != nullptr) { av_freep(myAvioCtx); }
}

void FdAVIOContext::close() {
    if (myFile != nullptr) {
        fclose(myFile);
        myFile = nullptr;
    }
}

bool FdAVIOContext::openFromDescriptor(int theFD, const char *theMode) {
    close();
    myFile = ::fdopen(theFD, theMode);
    return myFile != nullptr;
}

AVIOContext *FdAVIOContext::getAvioContext() const { return myAvioCtx; }

int FdAVIOContext::read(uint8_t *theBuf,
                        int theBufSize) {
    if (myFile == nullptr) { return -1; }

    int aNbRead = (int) ::fread(theBuf, 1, theBufSize, myFile);
    if (aNbRead == 0 && feof(myFile) != 0) { return AVERROR_EOF; }
    return aNbRead;
}

int FdAVIOContext::write(uint8_t *theBuf,
                         int theBufSize) {
    if (myFile == nullptr) { return -1; }
    return (int) ::fwrite(theBuf, 1, theBufSize, myFile);
}

int64_t FdAVIOContext::seek(int64_t theOffset,
                            int theWhence) {
    if (theWhence == AVSEEK_SIZE || myFile == nullptr) { return -1; }
    bool isOk = ::fseeko(myFile, theOffset, theWhence) == 0;
    if (!isOk) { return -1; }
    return ::ftello(myFile);
}

int FdAVIOContext::readCallback(void *theOpaque,
                                uint8_t *theBuf,
                                int theBufSize) {
    return theOpaque != nullptr
           ? ((FdAVIOContext *) theOpaque)->read(theBuf, theBufSize)
           : 0;
}

int FdAVIOContext::writeCallback(void *theOpaque,
                                 uint8_t *theBuf,
                                 int theBufSize) {
    return theOpaque != nullptr
           ? ((FdAVIOContext *) theOpaque)->write(theBuf, theBufSize)
           : 0;
}

int64_t FdAVIOContext::seekCallback(void *theOpaque,
                                    int64_t theOffset,
                                    int theWhence) {
    return theOpaque != nullptr
           ? ((FdAVIOContext *) theOpaque)->seek(theOffset, theWhence)
           : -1;
}
