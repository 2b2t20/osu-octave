#include "VideoPlayer.h"
#include "Graphics/GraphicsManager.h"

VideoPlayer VideoPlayer::sInstance;

#ifdef VITA

// -----------------------------------------------------------------------
// Memory / file I/O callbacks for SceAvPlayer
// -----------------------------------------------------------------------
void* VideoPlayer::AvPlayerAlloc(void* /*arg*/, uint32_t alignment, uint32_t size) {
    return memalign(alignment, size);
}

void VideoPlayer::AvPlayerFree(void* /*arg*/, void* ptr) {
    free(ptr);
}

void* VideoPlayer::AvPlayerAllocAlign(void* /*arg*/, uint32_t alignment, uint32_t size) {
    return memalign(alignment, size);
}

void VideoPlayer::AvPlayerDeallocate(void* /*arg*/, void* ptr) {
    free(ptr);
}

// Simple POSIX file callbacks (SceAvPlayer needs its own file I/O)
static FILE* s_videoFile = nullptr;

int VideoPlayer::AvPlayerOpenFile(void* /*arg*/, const char* path) {
    s_videoFile = fopen(path, "rb");
    return (s_videoFile != nullptr) ? 0 : -1;
}

int VideoPlayer::AvPlayerCloseFile(void* /*arg*/, int /*fd*/) {
    if (s_videoFile) { fclose(s_videoFile); s_videoFile = nullptr; }
    return 0;
}

int VideoPlayer::AvPlayerReadOffsetFile(void* /*arg*/, int /*fd*/, void* buffer, uint64_t position, uint32_t length) {
    if (!s_videoFile) return -1;
    fseek(s_videoFile, (long)position, SEEK_SET);
    return (int)fread(buffer, 1, length, s_videoFile);
}

uint64_t VideoPlayer::AvPlayerSizeFile(void* /*arg*/, int /*fd*/) {
    if (!s_videoFile) return 0;
    long cur = ftell(s_videoFile);
    fseek(s_videoFile, 0, SEEK_END);
    uint64_t size = (uint64_t)ftell(s_videoFile);
    fseek(s_videoFile, cur, SEEK_SET);
    return size;
}

// -----------------------------------------------------------------------
// Load
// -----------------------------------------------------------------------
void VideoPlayer::Load(const std::string& path, OOTime offsetMs) {
    Stop();

    mOffsetMs = offsetMs;

    SceAvPlayerInitData initData = {};
    initData.memoryReplacement.objectPointer    = nullptr;
    initData.memoryReplacement.allocate         = AvPlayerAlloc;
    initData.memoryReplacement.deallocate       = AvPlayerFree;
    initData.memoryReplacement.allocateTexture  = AvPlayerAllocAlign;
    initData.memoryReplacement.deallocateTexture = AvPlayerDeallocate;

    initData.fileReplacement.objectPointer = nullptr;
    initData.fileReplacement.open    = AvPlayerOpenFile;
    initData.fileReplacement.close   = AvPlayerCloseFile;
    initData.fileReplacement.readOffset = AvPlayerReadOffsetFile;
    initData.fileReplacement.size    = AvPlayerSizeFile;

    initData.basePriority  = 0xA0;
    initData.numOutputVideoFrameBuffers = 2;
    initData.autoStart = SCE_FALSE;

    mPlayer = sceAvPlayerInit(&initData);
    if (mPlayer == nullptr) {
        fprintf(stderr, "[VideoPlayer] sceAvPlayerInit failed\n");
        return;
    }

    if (sceAvPlayerAddSource(mPlayer, path.c_str()) < 0) {
        fprintf(stderr, "[VideoPlayer] sceAvPlayerAddSource failed: %s\n", path.c_str());
        sceAvPlayerStop(mPlayer);
        sceAvPlayerClose(mPlayer);
        mPlayer = nullptr;
        return;
    }

    // Pre-allocate SDL texture for video frames (YUVA420P / RGBA — AvPlayer gives RGBA on Vita)
    if (mVideoTexture) {
        SDL_DestroyTexture(mVideoTexture);
        mVideoTexture = nullptr;
    }
    mVideoTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!mVideoTexture) {
        fprintf(stderr, "[VideoPlayer] SDL_CreateTexture failed: %s\n", SDL_GetError());
        sceAvPlayerClose(mPlayer);
        mPlayer = nullptr;
        return;
    }

    mActive = true;
    fprintf(stderr, "[VideoPlayer] Loaded: %s (offset %lld ms)\n", path.c_str(), (long long)mOffsetMs);
}

// -----------------------------------------------------------------------
// Update — call each frame
// -----------------------------------------------------------------------
void VideoPlayer::Update(OOTime currentTimeMs) {
    if (!mActive || mPlayer == nullptr) return;

    // Start the player when song reaches the video offset
    OOTime videoTime = currentTimeMs - mOffsetMs;
    if (videoTime < 0) return;  // not yet time to show video

    // Start playback on first valid frame moment
    if (!sceAvPlayerIsActive(mPlayer)) {
        sceAvPlayerStart(mPlayer);
    }

    SceAvPlayerFrameInfo frameInfo = {};
    if (!sceAvPlayerGetVideoData(mPlayer, &frameInfo)) {
        return; // no frame ready yet
    }

    if (frameInfo.pData == nullptr) return;

    // frameInfo gives us a pointer to the decoded RGBA frame
    // Pitch = frameInfo.details.video.pitchWidth * 4 (RGBA)
    uint32_t pitch = frameInfo.details.video.pitchWidth * 4;
    int w = (int)frameInfo.details.video.width;
    int h = (int)frameInfo.details.video.height;

    // Resize texture if resolution changed
    int tw, th;
    SDL_QueryTexture(mVideoTexture, nullptr, nullptr, &tw, &th);
    if (tw != w || th != h) {
        SDL_DestroyTexture(mVideoTexture);
        mVideoTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                           SDL_TEXTUREACCESS_STREAMING, w, h);
        if (!mVideoTexture) return;
    }

    SDL_UpdateTexture(mVideoTexture, nullptr, frameInfo.pData, (int)pitch);

    // Draw fullscreen, maintaining aspect ratio
    SDL_Rect dst;
    float srcAspect = (float)w / (float)h;
    float dstAspect = (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT;
    if (srcAspect > dstAspect) {
        dst.w = SCREEN_WIDTH;
        dst.h = (int)(SCREEN_WIDTH / srcAspect);
        dst.x = 0;
        dst.y = (SCREEN_HEIGHT - dst.h) / 2;
    } else {
        dst.h = SCREEN_HEIGHT;
        dst.w = (int)(SCREEN_HEIGHT * srcAspect);
        dst.x = (SCREEN_WIDTH - dst.w) / 2;
        dst.y = 0;
    }

    SDL_RenderCopy(renderer, mVideoTexture, nullptr, &dst);
}

// -----------------------------------------------------------------------
// Pause / Resume / Stop
// -----------------------------------------------------------------------
void VideoPlayer::Pause() {
    if (mActive && mPlayer) sceAvPlayerPause(mPlayer);
}

void VideoPlayer::Resume() {
    if (mActive && mPlayer) sceAvPlayerResume(mPlayer);
}

void VideoPlayer::Stop() {
    mActive = false;
    if (mPlayer) {
        sceAvPlayerStop(mPlayer);
        sceAvPlayerClose(mPlayer);
        mPlayer = nullptr;
    }
    if (mVideoTexture) {
        SDL_DestroyTexture(mVideoTexture);
        mVideoTexture = nullptr;
    }
}

#else // DESKTOP STUB

void VideoPlayer::Load(const std::string& path, OOTime offsetMs) {
    // Desktop build: no video support. Just log and ignore.
    fprintf(stderr, "[VideoPlayer] Desktop stub: video not supported (%s, offset %lld)\n",
            path.c_str(), (long long)offsetMs);
    mActive = false;
}

void VideoPlayer::Update(OOTime /*currentTimeMs*/) { /* no-op */ }
void VideoPlayer::Pause()  { /* no-op */ }
void VideoPlayer::Resume() { /* no-op */ }
void VideoPlayer::Stop()   { mActive = false; }

#endif
