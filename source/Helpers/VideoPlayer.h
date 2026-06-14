#pragma once

#include <string>
#include <cstdio>
#include "defines.h"
#include "types.h"

#ifdef VITA
#include <psp2/avplayer.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/processmgr.h>
#include <SDL.h>
#endif

// VideoPlayer: plays a beatmap video file as background during gameplay.
// On Vita: uses SceAvPlayer (native hardware decoder).
// On Desktop: stub (no video, silent fallback).
class VideoPlayer
{
public:
    static VideoPlayer& Instance() { return sInstance; }

    // Load video from path. offsetMs = Video offset from .osu [Events] (can be negative).
    void Load(const std::string& path, OOTime offsetMs);

    // Call once per frame during gameplay. Draws the current video frame to the SDL renderer,
    // scaled to SCREEN_WIDTH x SCREEN_HEIGHT. Must be called BEFORE DrawBeatmapBackground
    // so that the game background is overridden when video is active.
    void Update(OOTime currentTimeMs);

    // Pause / resume (called on game pause/resume)
    void Pause();
    void Resume();

    // Stop and free all resources. Called when leaving Player mode.
    void Stop();

    [[nodiscard]] bool IsActive() const { return mActive; }

private:
    VideoPlayer() = default;
    ~VideoPlayer() = default;

    static VideoPlayer sInstance;

    bool mActive = false;
    OOTime mOffsetMs = 0;  // video start offset relative to song time=0

#ifdef VITA
    SceAvPlayerHandle mPlayer = nullptr;
    SDL_Texture* mVideoTexture = nullptr;

    // Memory allocation callbacks for SceAvPlayer
    static void* AvPlayerAlloc(void* arg, uint32_t alignment, uint32_t size);
    static void  AvPlayerFree(void* arg, void* ptr);
    static void* AvPlayerAllocAlign(void* arg, uint32_t alignment, uint32_t size);
    static void  AvPlayerDeallocate(void* arg, void* ptr);
    static int   AvPlayerOpenFile(void* arg, const char* path);
    static int   AvPlayerCloseFile(void* arg, int fd);
    static int   AvPlayerReadOffsetFile(void* arg, int fd, void* buffer, uint64_t position, uint32_t length);
    static uint64_t AvPlayerSizeFile(void* arg, int fd);
#endif
};
