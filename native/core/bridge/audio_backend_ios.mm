/*
 * audio_backend_ios.m — CoreAudio/AudioQueue backend for iOS (and macOS).
 *
 * Uses AudioQueueNewOutput with a render callback that pulls frames
 * from the bridge's audio ring. AVAudioSession is configured in
 * playback category so audio survives the silent switch.
 *
 * v1 scaffold: compiles and links; full implementation lands when
 * iOS device testing begins.
 */
#include "audio_backend.h"

#import <AudioToolbox/AudioToolbox.h>
#import <AVFoundation/AVFoundation.h>

#include <cstdio>

@interface YmirAudioQueueBridge : NSObject {
@public
    AudioQueueRef _queue;
    BOOL          _running;
}
@end

@implementation YmirAudioQueueBridge
@end

struct CoreAudioState {
    YmirAudioQueueBridge *bridge = nil;
    AudioQueueRef         queue  = nullptr;
};

static int32_t coreaudio_start(void *user) {
    auto *st = (CoreAudioState *)user;
    NSError *err = nil;
    [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback
                                            error:&err];
    if (err) {
        std::fprintf(stderr, "audio_backend_ios: AVAudioSession setCategory failed\n");
    }
    [[AVAudioSession sharedInstance] setActive:YES error:&err];
    std::fprintf(stderr, "audio_backend_ios: CoreAudio start not yet implemented\n");
    return 0;
}

static void coreaudio_stop(void *user) {
    auto *st = (CoreAudioState *)user;
    (void)st;
}

static int32_t coreaudio_push_frames(void *user, const int16_t *frames, int32_t count) {
    (void)user; (void)frames; (void)count;
    return 0;
}

static void coreaudio_set_muted(void *user, int32_t muted) {
    (void)user; (void)muted;
}

static int32_t coreaudio_get_level(void *user) {
    (void)user;
    return 0;
}

static void coreaudio_destroy(void *user) {
    auto *st = (CoreAudioState *)user;
    if (st->queue) AudioQueueDispose(st->queue, YES);
    delete st;
}

extern "C" YmirAudioBackend *ymir_audio_backend_ios_create(void) {
    auto *be = new YmirAudioBackend();
    auto *st = new CoreAudioState();
    be->start       = coreaudio_start;
    be->stop        = coreaudio_stop;
    be->push_frames = coreaudio_push_frames;
    be->set_muted   = coreaudio_set_muted;
    be->get_level   = coreaudio_get_level;
    be->destroy     = coreaudio_destroy;
    be->user        = st;
    return be;
}