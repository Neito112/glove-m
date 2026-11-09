#ifndef PERIPHERAL_AUDIO_H
#define PERIPHERAL_AUDIO_H

#include "peripheral.h"

// Microphone and speaker.
//
// Both use the I2S peripheral. The microphone records into a DMA buffer and
// the speaker plays a buffer out of it.
//
// The samples are 16 bit, mono, at 16 kHz. That is enough for a voice
// assistant and it keeps the buffer small enough for a target without PSRAM.

#define PERIPHERAL_AUDIO_SAMPLE_RATE 16000
#define PERIPHERAL_AUDIO_SAMPLE_BITS 16
#define PERIPHERAL_AUDIO_CHANNELS 1

// The number of samples one buffer holds. At 16 kHz that is 100 ms.
#define PERIPHERAL_AUDIO_BUFFER_SAMPLES 1600

// The size of one sample buffer in bytes.
#define PERIPHERAL_AUDIO_BUFFER_BYTES \
    (PERIPHERAL_AUDIO_BUFFER_SAMPLES * (PERIPHERAL_AUDIO_SAMPLE_BITS / 8) * \
     PERIPHERAL_AUDIO_CHANNELS)

// Bring the microphone up. The audio input runs on the given data pin.
esp_err_t peripheral_mic_start(const peripheral_config_t *config);
void peripheral_mic_stop(void);
bool peripheral_mic_is_running(void);

// Read the loudest sample of the last buffer. It is a simple level meter,
// enough to show that the microphone works and to detect a voice.
esp_err_t peripheral_mic_read_level(int *level_out);

// The peak level of the last capture, 0 to 32767.
int peripheral_mic_peak(void);

// Bring the speaker up. The audio output runs on the given data pin.
esp_err_t peripheral_speaker_start(const peripheral_config_t *config);
void peripheral_speaker_stop(void);
bool peripheral_speaker_is_running(void);

// Play a tone of the given frequency. The call returns as soon as the tone
// is queued, so it never blocks the agent.
esp_err_t peripheral_speaker_play_tone(int frequency_hz, int duration_ms);

// Play a short click, handy to confirm that the speaker works.
esp_err_t peripheral_speaker_beep(void);

#endif // PERIPHERAL_AUDIO_H
