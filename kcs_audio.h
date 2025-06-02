#ifndef KCS_AUDIO_H
#define KCS_AUDIO_H


#include <stdint.h>
#include <stdlib.h>

// Constants
#define SAMPLE_RATE 44100
#define BUFFER_SIZE 65536
#define BAUD_RATE_300 300
#define BAUD_RATE_1200 1200

// Tone Freqs
#define TONE_FREQ_1200 1200
#define TONE_SPACE_2400 2400

// Data Structs

typedef struct {
    uint8_t data_bits;
    uint8_t stop_bits;
    char parity; // 'N' for None, 'E' for Even, 'O' for Odd
} SerialFormat;


typedef struct {
    int16_t *samples;
    size_t sample_count;
} AudioBuffer;


AudioBuffer load_audio(const char *path);
void save_audio(const AudioBuffer *audio, const char *path);

void free_audio_buffer(AudioBuffer *audio);

void decode_kcs(const AudioBuffer *audio, uint8_t *out_data, size_t *out_size, SerialFormat fmt, int baud_rate);
void encode_kcs(const uint8_t *data, size_t size, AudioBuffer *audio, SerialFormat fmt, int baud_rate);



#endif // KCS_AUDIO_H


