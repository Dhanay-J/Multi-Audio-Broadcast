#ifndef RINGBUF_H
#define RINGBUF_H

#include "miniaudio.h"
#include <cstddef>

struct AudioRingBuffer
{
    float *buffer;
    ma_uint32 capacity; // in frames
    ma_uint32 readIndex;
    ma_uint32 writeIndex;
    ma_uint32 channels;
};

void ring_buffer_init(AudioRingBuffer *rb, ma_uint32 capacityFrames, ma_uint32 channels);
void ring_buffer_uninit(AudioRingBuffer *rb);
ma_uint32 ring_buffer_available_read(AudioRingBuffer *rb);
ma_uint32 ring_buffer_available_write(AudioRingBuffer *rb);
ma_uint32 ring_buffer_write(AudioRingBuffer *rb, const float *data, ma_uint32 frameCount);
ma_uint32 ring_buffer_read(AudioRingBuffer *rb, float *data, ma_uint32 frameCount);

#endif // RINGBUF_H