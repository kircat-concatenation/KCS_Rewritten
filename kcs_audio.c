// Fixed kcs_audio.c
#include "kcs_audio.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#define SAMPLE_RATE 44100 

#define TONE_FREQ_1200 1200.0
#define TONE_SPACE_2400 2400.0

#pragma pack(push, 1)
typedef struct {
    char chunk_id[4];
    uint32_t chunk_size;
    char format[4];

    char subchunk1_id[4];
    uint32_t subchunk1_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;

    char subchunk2_id[4];
    uint32_t subchunk2_size;
} wav_header_t;
#pragma pack(pop)

#define WAV_HEADER_SIZE 44

static double bit_duration(int baud_rate) {
    return 1.0 / baud_rate;
}

static uint8_t calc_parity(uint8_t byte, char parity) {
    int count = 0;
    for (int i = 0; i < 8; i++) {
        if (byte & (1 << i)) count++;
    }
    if (parity == 'E') {
        return (count % 2 == 0) ? 0 : 1;
    } else if (parity == 'O') {
        return (count % 2 == 0) ? 1 : 0;
    }
    return 0;
}

static void generate_bit(int16_t *samples, size_t *index, int bit, size_t samples_per_bit) {
    double freq = (bit == 0) ? TONE_FREQ_1200 : TONE_SPACE_2400;
    double samples_per_cycle = (double)SAMPLE_RATE / freq;
    for (size_t i = 0; i < samples_per_bit; i++) {
        double cycle_pos = fmod(i, samples_per_cycle);
        samples[(*index)++] = (cycle_pos < samples_per_cycle / 2) ? 32767 : -32768;
    }
}

static int detect_bit(const int16_t *samples, size_t start, size_t samples_per_bit) {
    size_t zero_crossings = 0;
    for (size_t i = start + 1; i < start + samples_per_bit; i++) {
        if ((samples[i - 1] < 0 && samples[i] >= 0) || (samples[i - 1] >= 0 && samples[i] < 0)) {
            zero_crossings++;
        }
    }
    double freq_est = ((double)zero_crossings * SAMPLE_RATE) / (2.0 * samples_per_bit);
    double diff_1200 = fabs(freq_est - TONE_FREQ_1200);
    double diff_2400 = fabs(freq_est - TONE_SPACE_2400);
    return (diff_2400 < diff_1200) ? 1 : 0;
}

void decode_kcs(const AudioBuffer *audio, uint8_t *out_data, size_t *out_size, SerialFormat fmt, int baud_rate) {
    if (!audio || !out_data || !out_size || baud_rate <= 0) return;

    double bit_len = bit_duration(baud_rate);
    size_t samples_per_bit = (size_t)(SAMPLE_RATE * bit_len);
    size_t samples = audio->sample_count;
    const int16_t *samples_ptr = audio->samples;

    size_t sample_index = 0;
    size_t data_index = 0;

    int parity_bit_count = (fmt.parity == 'N') ? 0 : 1;
    int total_bits_per_byte = 1 + fmt.data_bits + parity_bit_count + fmt.stop_bits;

    while (sample_index + samples_per_bit * total_bits_per_byte <= samples) {
        int start_bit = detect_bit(samples_ptr, sample_index, samples_per_bit);
        if (start_bit != 0) {
            sample_index++;
            continue;
        }
        sample_index += samples_per_bit;
        uint8_t byte = 0;
        for (int b = 0; b < fmt.data_bits; b++) {
            int bit = detect_bit(samples_ptr, sample_index, samples_per_bit);
            byte |= (bit & 1) << b;
            sample_index += samples_per_bit;
        }
        if (parity_bit_count) {
            uint8_t parity_val = detect_bit(samples_ptr, sample_index, samples_per_bit);
            sample_index += samples_per_bit;
            uint8_t calc = calc_parity(byte, fmt.parity);
        }
        for (int s = 0; s < fmt.stop_bits; s++) {
            int stop_bit = detect_bit(samples_ptr, sample_index, samples_per_bit);
            sample_index += samples_per_bit;
        }
        if (data_index >= BUFFER_SIZE) break;
        out_data[data_index++] = byte;
    }
    *out_size = data_index;
}

AudioBuffer load_audio(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        perror("Failed to open audio file");
        exit(EXIT_FAILURE);
    }

    uint8_t header[WAV_HEADER_SIZE];
    if (fread(header, 1, WAV_HEADER_SIZE, file) != WAV_HEADER_SIZE) {
        fprintf(stderr, "Failed to read WAV header\n");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        fprintf(stderr, "Not a valid WAV file\n");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    uint16_t audio_format   = header[20] | (header[21] << 8);
    uint16_t num_channels   = header[22] | (header[23] << 8);
    uint32_t sample_rate    = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    uint16_t bits_per_sample = header[34] | (header[35] << 8);
    uint32_t data_size      = header[40] | (header[41] << 8) | (header[42] << 16) | (header[43] << 24);

    if (audio_format != 1 || num_channels != 1 || bits_per_sample != 16 || sample_rate != SAMPLE_RATE) {
        fprintf(stderr, "Unsupported WAV format. Must be 16-bit mono PCM at 44100 Hz\n");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    int16_t *samples = malloc(data_size);
    if (!samples || fread(samples, 1, data_size, file) != data_size) {
        fprintf(stderr, "Failed to read audio data\n");
        free(samples);
        fclose(file);
        exit(EXIT_FAILURE);
    }
    fclose(file);

    AudioBuffer buffer = {
        .samples = samples,
        .sample_count = data_size / sizeof(int16_t)
    };
    return buffer;
}

void save_audio(const AudioBuffer *audio, const char *path) {
    if (!audio || !audio->samples || audio->sample_count == 0 || !path) {
        fprintf(stderr, "Invalid arguments to save_audio\n");
        return;
    }

    FILE *f = fopen(path, "wb");
    if (!f) {
        perror("Failed to open file for writing");
        return;
    }

    uint32_t subchunk2_size = audio->sample_count * sizeof(int16_t);
    uint32_t chunk_size = 4 + (8 + 16) + (8 + subchunk2_size);
    uint8_t header[44] = {
        'R','I','F','F',
        chunk_size & 0xFF, (chunk_size >> 8) & 0xFF, (chunk_size >> 16) & 0xFF, (chunk_size >> 24) & 0xFF,
        'W','A','V','E',
        'f','m','t',' ',
        16, 0, 0, 0,
        1, 0,
        1, 0,
        SAMPLE_RATE & 0xFF, (SAMPLE_RATE >> 8) & 0xFF, (SAMPLE_RATE >> 16) & 0xFF, (SAMPLE_RATE >> 24) & 0xFF,
        (SAMPLE_RATE * 2) & 0xFF, ((SAMPLE_RATE * 2) >> 8) & 0xFF, ((SAMPLE_RATE * 2) >> 16) & 0xFF, ((SAMPLE_RATE * 2) >> 24) & 0xFF,
        2, 0,
        16, 0,
        'd','a','t','a',
        subchunk2_size & 0xFF, (subchunk2_size >> 8) & 0xFF, (subchunk2_size >> 16) & 0xFF, (subchunk2_size >> 24) & 0xFF
    };
    fwrite(header, 1, 44, f);
    fwrite(audio->samples, sizeof(int16_t), audio->sample_count, f);
    fclose(f);
}

void encode_kcs(const uint8_t *data, size_t size, AudioBuffer *audio, SerialFormat fmt, int baud_rate) {
    if (!data || size == 0 || !audio || baud_rate <= 0) return;

    double bit_len = bit_duration(baud_rate);
    size_t samples_per_bit = (size_t)(SAMPLE_RATE * bit_len);
    int parity_bit = (fmt.parity == 'N') ? 0 : 1;
    size_t total_bits = size * (1 + fmt.data_bits + parity_bit + fmt.stop_bits);
    size_t total_samples = total_bits * samples_per_bit;

    audio->samples = (int16_t *)malloc(sizeof(int16_t) * total_samples);
    if (!audio->samples) {
        audio->sample_count = 0;
        return;
    }
    audio->sample_count = total_samples;
    size_t sample_index = 0;

    for (size_t i = 0; i < size; i++) {
        uint8_t byte = data[i];
        generate_bit(audio->samples, &sample_index, 0, samples_per_bit);
        for (int bit = 0; bit < fmt.data_bits; bit++) {
            generate_bit(audio->samples, &sample_index, (byte >> bit) & 1, samples_per_bit);
        }
        if (parity_bit) {
            uint8_t parity_val = calc_parity(byte, fmt.parity);
            generate_bit(audio->samples, &sample_index, parity_val, samples_per_bit);
        }
        for (int s = 0; s < fmt.stop_bits; s++) {
            generate_bit(audio->samples, &sample_index, 1, samples_per_bit);
        }
    }
}

void free_audio_buffer(AudioBuffer *buffer) {
    if (buffer && buffer->samples) {
        free(buffer->samples);
        buffer->samples = NULL;
        buffer->sample_count = 0;
    }
}
