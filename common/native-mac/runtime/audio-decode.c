/*
 *  Port change: decode a compressed sound image in memory (MP3 and the
 *  other formats of the macOS AudioToolbox) to 16-bit PCM.
 *  Miles plays such images through ASI codecs (Revenant: mp3dec.asi for
 *  its speech in resources.rvr). Here AudioToolbox replaces the codec.
 *  MIT license, see README.md.
 */

#include <AudioToolbox/AudioToolbox.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "audio-mixer.h"

typedef struct {
    const uint8_t *data;
    uint32_t size;
} mem_file;

static OSStatus mem_read(void *user, SInt64 pos, UInt32 count, void *buffer, UInt32 *actual)
{
    mem_file *m = (mem_file *)user;
    if (pos < 0 || pos >= (SInt64)m->size)
    {
        *actual = 0;
        return noErr;
    }
    if (count > m->size - (uint32_t)pos) count = m->size - (uint32_t)pos;
    memcpy(buffer, m->data + pos, count);
    *actual = count;
    return noErr;
}

static SInt64 mem_size(void *user)
{
    return ((mem_file *)user)->size;
}

/* type_hint: a file name suffix such as ".mp3" (or NULL) */
int Mixer_DecodeImage(const uint8_t *img, uint32_t size, const char *type_hint,
                      int16_t **pcm, uint32_t *bytes, int *rate, int *chans)
{
    mem_file m = { img, size };
    AudioFileID af = NULL;
    ExtAudioFileRef f = NULL;
    AudioFileTypeID type = 0;
    AudioStreamBasicDescription in, out;
    UInt32 prop = sizeof(in);
    int16_t *buf = NULL;
    uint32_t cap = 0, used = 0;

    if (type_hint != NULL && strcasecmp(type_hint, ".mp3") == 0) type = kAudioFileMP3Type;
    if (AudioFileOpenWithCallbacks(&m, mem_read, NULL, mem_size, NULL, type, &af) != noErr) return -1;
    if (ExtAudioFileWrapAudioFileID(af, false, &f) != noErr ||
        ExtAudioFileGetProperty(f, kExtAudioFileProperty_FileDataFormat, &prop, &in) != noErr)
    {
        if (f) ExtAudioFileDispose(f);
        AudioFileClose(af);
        return -1;
    }

    memset(&out, 0, sizeof(out));
    out.mSampleRate = in.mSampleRate;
    out.mFormatID = kAudioFormatLinearPCM;
    out.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    out.mChannelsPerFrame = (in.mChannelsPerFrame == 1) ? 1 : 2;
    out.mBitsPerChannel = 16;
    out.mBytesPerFrame = 2 * out.mChannelsPerFrame;
    out.mFramesPerPacket = 1;
    out.mBytesPerPacket = out.mBytesPerFrame;
    if (ExtAudioFileSetProperty(f, kExtAudioFileProperty_ClientDataFormat, sizeof(out), &out) != noErr)
    {
        ExtAudioFileDispose(f);
        AudioFileClose(af);
        return -1;
    }

    for (;;)
    {
        AudioBufferList list;
        UInt32 frames = 4096;
        if (cap - used < frames * out.mBytesPerFrame)
        {
            cap = cap ? cap * 2 : 65536;
            int16_t *grown = (int16_t *)realloc(buf, cap);
            if (grown == NULL) break;
            buf = grown;
        }
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = out.mChannelsPerFrame;
        list.mBuffers[0].mDataByteSize = frames * out.mBytesPerFrame;
        list.mBuffers[0].mData = (uint8_t *)buf + used;
        if (ExtAudioFileRead(f, &frames, &list) != noErr || frames == 0) break;
        used += frames * out.mBytesPerFrame;
    }
    ExtAudioFileDispose(f);
    AudioFileClose(af);

    if (used == 0)
    {
        free(buf);
        return -1;
    }
    *pcm = buf;
    *bytes = used;
    *rate = (int)out.mSampleRate;
    *chans = (int)out.mChannelsPerFrame;
    return 0;
}
