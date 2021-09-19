#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2s.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#define I2S_NUM_SPEAKER             I2S_NUM_1
#define GPIO_SPEAKER_CONTROL        32
#define SPEAKER_DMA_BUFFER_LENGTH   256
#define SPEAKER_DMA_BUFFER_COUNT    16

extern const unsigned char test_mp3[];
extern int test_mp3_len;

void initialize_devices(uint32_t sample_rate, bool stereo) {
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = sample_rate,
        .bits_per_sample = 16,
        .channel_format = stereo ? I2S_CHANNEL_FMT_RIGHT_LEFT : I2S_CHANNEL_FMT_ONLY_RIGHT,
        .communication_format = I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB,
        .dma_buf_count = SPEAKER_DMA_BUFFER_COUNT,
        .dma_buf_len = SPEAKER_DMA_BUFFER_LENGTH,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1
    };

	i2s_pin_config_t i2s_pins = {
        .bck_io_num   = GPIO_NUM_26,
        .ws_io_num    = GPIO_NUM_22,
        .data_out_num = GPIO_NUM_25,
        .data_in_num  = I2S_PIN_NO_CHANGE
    };

    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1ULL << GPIO_SPEAKER_CONTROL;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    i2s_driver_install(I2S_NUM_SPEAKER, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_SPEAKER, &i2s_pins);

    gpio_set_level(GPIO_SPEAKER_CONTROL, 1);
}

void play_mp3(const void *mp3, size_t size) {
    int32_t remain_size = size;
    const uint8_t* decode_ptr = (uint8_t*)mp3;

    // Initialize MP3 decoder
    mp3dec_t mp3d;
    mp3dec_init(&mp3d);

    mp3dec_frame_info_t info;
    size_t written = 0;
    bool is_first_frame = true;

    while (1) {
        // デコードする
        short pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
        int samples = mp3dec_decode_frame(&mp3d, decode_ptr, remain_size, pcm, &info);
        if (info.frame_bytes == 0) break;

        // Initialize devices when first frame
        if (is_first_frame) {
            is_first_frame = false;
            initialize_devices(info.hz, info.channels == 2);
        }

        // Write PCM data to I2S device
        size_t write_size = sizeof(int16_t) * samples * info.channels;
        int ret = i2s_write(I2S_NUM_SPEAKER, (char *)pcm, write_size, &written, portMAX_DELAY);
        if (ret != ESP_OK || write_size != written) {
            printf("soundPlayMP3 : i2s_push_sample fail, ret = %d", ret);
            break;
        }

        // Move decode position
        decode_ptr += info.frame_bytes;
        remain_size -= info.frame_bytes;
    }
    
    // Flush DMA buffer
    int16_t buf = 0;
    for(int i = 0; i < SPEAKER_DMA_BUFFER_LENGTH * SPEAKER_DMA_BUFFER_COUNT * info.channels; i++){
        i2s_write(I2S_NUM_SPEAKER, (char *)&buf, sizeof(int16_t), &written, portMAX_DELAY);
    }
}

void app_main() {
    printf("Program start!\n");

    play_mp3(test_mp3, test_mp3_len);

    printf("Done!\n");
    
    while(1) vTaskDelay(1000 / portTICK_PERIOD_MS);
}
