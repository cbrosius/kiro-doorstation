#ifndef AUDIO_HANDLER_H
#define AUDIO_HANDLER_H

#include "driver/i2s.h"

#define SAMPLE_RATE     8000
#define BITS_PER_SAMPLE 16
#define CHANNELS        1
#define DMA_BUF_COUNT   8
#define DMA_BUF_LEN     1024

typedef struct {
    int16_t *buffer;
    size_t length;
} audio_buffer_t;

void audio_handler_init(void);
void audio_start_recording(void);
void audio_stop_recording(void);
void audio_start_playback(void);
void audio_stop_playback(void);
size_t audio_read(int16_t *buffer, size_t length);
size_t audio_write(const int16_t *buffer, size_t length);

#endif