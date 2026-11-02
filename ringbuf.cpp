#include "ringbuf.h"
#include <cstring>
#include <algorithm>

void ring_buffer_init(AudioRingBuffer *rb, ma_uint32 capacityFrames, ma_uint32 channels)
{
    rb->capacity = capacityFrames;
    rb->channels = channels;
    rb->readIndex = 0;
    rb->writeIndex = 0;
    // Allocate enough space for capacity + 1 to distinguish full/empty easily if needed,
    // but here we just use capacity and handle modulo carefully.
    rb->buffer = new float[capacityFrames * channels];
    std::memset(rb->buffer, 0, capacityFrames * channels * sizeof(float));
}

void ring_buffer_uninit(AudioRingBuffer *rb)
{
    if (rb->buffer)
    {
        delete[] rb->buffer;
        rb->buffer = nullptr;
    }
}

ma_uint32 ring_buffer_available_read(AudioRingBuffer *rb)
{
    if (rb->writeIndex >= rb->readIndex)
    {
        return rb->writeIndex - rb->readIndex;
    }
    else
    {
        return rb->capacity - rb->readIndex + rb->writeIndex;
    }
}

ma_uint32 ring_buffer_available_write(AudioRingBuffer *rb)
{
    // Leave one slot empty to distinguish full from empty
    ma_uint32 used = ring_buffer_available_read(rb);
    if (rb->capacity > used)
    {
        return rb->capacity - used - 1;
    }
    return 0;
}

ma_uint32 ring_buffer_write(AudioRingBuffer *rb, const float *data, ma_uint32 frameCount)
{
    ma_uint32 available = ring_buffer_available_write(rb);
    if (frameCount > available)
    {
        frameCount = available;
    }

    ma_uint32 channels = rb->channels;
    for (ma_uint32 i = 0; i < frameCount; ++i)
    {
        ma_uint32 writePos = rb->writeIndex % rb->capacity;
        for (ma_uint32 ch = 0; ch < channels; ++ch)
        {
            rb->buffer[writePos * channels + ch] = data[i * channels + ch];
        }
        rb->writeIndex++;
    }

    return frameCount;
}

ma_uint32 ring_buffer_read(AudioRingBuffer *rb, float *data, ma_uint32 frameCount)
{
    ma_uint32 available = ring_buffer_available_read(rb);
    if (frameCount > available)
    {
        frameCount = available;
    }

    ma_uint32 channels = rb->channels;
    for (ma_uint32 i = 0; i < frameCount; ++i)
    {
        ma_uint32 readPos = rb->readIndex % rb->capacity;
        for (ma_uint32 ch = 0; ch < channels; ++ch)
        {
            data[i * channels + ch] = rb->buffer[readPos * channels + ch];
        }
        rb->readIndex++;
    }

    return frameCount;
}