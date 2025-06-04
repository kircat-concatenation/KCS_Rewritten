/*
Modern Reinterpretation of KCS08 For Unix-Like Systems
Kira
6/1/2025
*/

// Fixed main.c for pointer safety and consistency, with optional binary output on decode
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include "kcs_audio.h"

//#define BUFFER_SIZE 65536

void print_hex(const uint8_t *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        printf("%02X ", data[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    if (size % 16 != 0) printf("\n");
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage:\n");
        fprintf(stderr, "  %s encode <input_bin> <output_audio>\n", argv[0]);
        fprintf(stderr, "  %s decode <input_audio> [output_bin]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *mode = argv[1];
    SerialFormat fmt = {8, 1, 'N'};
    int baud_rate = 1200;

    if (strcmp(mode, "encode") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Encode mode requires input binary and output audio file\n");
            return EXIT_FAILURE;
        }

        const char *input_bin_path = argv[2];
        const char *output_audio_path = argv[3];

        FILE *bin_file = fopen(input_bin_path, "rb");
        if (!bin_file) {
            perror("Failed to open input binary file");
            return EXIT_FAILURE;
        }
        size_t alloc_size = 65536;
        size_t data_size = 0;
        uint8_t *data = malloc(alloc_size);
        
        if (!data) { perror("Memory allocation failed"); fclose(bin_file); return EXIT_FAILURE; }
        
        while ((bytes = fread(buffer, 1, sizeof(buffer), bin_file) > 0)) {

            if (data_size +bytes > alloc_size) {
                alloc_size *= 2;
                uint8_t *newdata = realloc(data, alloc_size);
                if (!new_data) { free(data); perror("Memory reallocation failed"); fclose(bin_file);return EXIT_FAILURE;}
                data = newdata;
            }
            memcpy(data + data_size, buffer, bytes);
            data_size += bytes;


        }

        /*uint8_t *data = malloc(BUFFER_SIZE);
        if (!data) {
            fprintf(stderr, "Memory allocation failed\n");
            fclose(bin_file);
            return EXIT_FAILURE;
        }

        size_t data_size = fread(data, 1, BUFFER_SIZE, bin_file);
        fclose(bin_file);

        if (data_size == 0) {
            fprintf(stderr, "Input binary file is empty or read failed\n");
            free(data);
            return EXIT_FAILURE;
        }*/

        AudioBuffer audio = {0};
        encode_kcs(data, data_size, &audio, fmt, baud_rate);
        save_audio(&audio, output_audio_path);
        free_audio_buffer(&audio);
        free(data);

        printf("Encoded %zu bytes from %s to %s\n", data_size, input_bin_path, output_audio_path);

    } else if (strcmp(mode, "decode") == 0) {
        const char *input_audio_path = argv[2];
        AudioBuffer audio = load_audio(input_audio_path);

        if (!audio.samples || audio.sample_count == 0) {
            fprintf(stderr, "Failed to load audio file: %s\n", input_audio_path);
            return EXIT_FAILURE;
        }

        uint8_t *decoded_data = malloc(BUFFER_SIZE);
        if (!decoded_data) {
            fprintf(stderr, "Memory allocation failed\n");
            free_audio_buffer(&audio);
            return EXIT_FAILURE;
        }

        size_t decoded_size = 0;
        decode_kcs(&audio, decoded_data, &decoded_size, fmt, baud_rate);

        if (argc >= 4) {
            const char *output_bin_path = argv[3];
            FILE *out_file = fopen(output_bin_path, "wb");
            if (!out_file) {
                perror("Failed to open output file for writing");
            } else {
                fwrite(decoded_data, 1, decoded_size, out_file);
                fclose(out_file);
                printf("Decoded %zu bytes written to %s\n", decoded_size, output_bin_path);
            }
        } else {
            printf("Decoded %zu bytes:\n", decoded_size);
            print_hex(decoded_data, decoded_size);
        }

        free(decoded_data);
        free_audio_buffer(&audio);

    } else {
        fprintf(stderr, "Unknown mode '%s'. Use 'encode' or 'decode'.\n", mode);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
