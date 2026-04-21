/*
 * Capture microphone audio and run local meow detector model once.
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <string.h>

#ifdef RT_USING_FINSH
#include <finsh.h>
#endif

#define DBG_TAG "meow.det"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#include "edge-impulse-sdk/tensorflow/lite/micro/micro_interpreter.h"
#include "edge-impulse-sdk/tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "edge-impulse-sdk/tensorflow/lite/schema/schema_generated.h"

#include "tflite-model/tflite-resolver.h"

// Avoid RT-Thread legacy ALIGN macro clash
#ifdef ALIGN
#undef ALIGN
#endif
#include "tflite-model/snore_model_data.h"

// UI helpers
#include "xiaozhi_ui.h"

/* CMSIS-DSP for FFT/Mel */
#include "edge-impulse-sdk/CMSIS/DSP/Include/arm_math.h"
#include <cmath>  // logf, fabsf

/* Hann window constant */
#define PI 3.14159265358979323846f

/* Audio device name */
#ifndef BSP_XIAOZHI_MIC_DEVICE_NAME
#define BSP_XIAOZHI_MIC_DEVICE_NAME "mic0"
#endif

namespace {
/*
 * PDM driver configuration (same as wakeword).
 */
#ifdef ENABLE_STEREO_INPUT_FEED
    #define PDM_FRAME_SAMPLES 320
    #define PDM_MONO_FRAME_SAMPLES (PDM_FRAME_SAMPLES / 2)
    #define PDM_IS_STEREO 1
#else
    #define PDM_FRAME_SAMPLES 160
    #define PDM_MONO_FRAME_SAMPLES PDM_FRAME_SAMPLES
    #define PDM_IS_STEREO 0
#endif
#define PDM_FRAME_SIZE (PDM_FRAME_SAMPLES * sizeof(int16_t))

// Board LEDs (from cycfg_pins.h: P16_7 red, P16_6 green, P16_5 blue)
constexpr int kLedRed = GET_PIN(16, 7);
constexpr int kLedGreen = GET_PIN(16, 6);
constexpr int kLedBlue = GET_PIN(16, 5);
// LED polarity:
// - active-high: PIN_HIGH=ON, PIN_LOW=OFF
// - active-low : PIN_LOW =ON, PIN_HIGH=OFF
// If your LEDs behave inverted, toggle this value.
constexpr int kLedActiveLow = 0;

static void led_write(int pin, rt_bool_t on)
{
    rt_pin_write(pin, (on ^ (kLedActiveLow ? 1 : 0)) ? PIN_HIGH : PIN_LOW);
}

static void leds_init_once(void)
{
    static rt_bool_t inited = RT_FALSE;
    if (inited)
        return;
    inited = RT_TRUE;

    rt_pin_mode(kLedRed, PIN_MODE_OUTPUT);
    rt_pin_mode(kLedGreen, PIN_MODE_OUTPUT);
    rt_pin_mode(kLedBlue, PIN_MODE_OUTPUT);

    // Default: detector not running -> red on, green off, blue off
    led_write(kLedRed, RT_TRUE);
    led_write(kLedGreen, RT_FALSE);
    led_write(kLedBlue, RT_FALSE);
}

constexpr int kSampleRate = 16000; // used for info only
constexpr size_t kInputSamples = 32000; // raw PCM samples (2 seconds @ 16kHz)

// Snore model: input [1,60,20] = 1200 bytes (Mel spectrogram features)
// Quant params from model export
constexpr float kInScale = 0.01948132f;
constexpr int kInZeroPoint = 24;
constexpr float kOutScale = 0.00390625f;
constexpr int kOutZeroPoint = -128;

// DSP params for feature extraction
constexpr int kFFTSize = 512;
constexpr int kFFTOutputSize = 257;  // FFT size / 2 + 1
constexpr int kHopSize = 160;        // 10ms hop @ 16kHz (matches DeepCraft model)
constexpr int kNumFrames = 60;       // frames to accumulate (match model)
constexpr int kMelBins = 20;         // output mel bins
constexpr int kFeatureSize = 60 * 20; // 1200

// Detection threshold (0.0 ~ 1.0)
// Threshold for snore detection (logit difference: class1 - class0)
constexpr float kSnoreThreshold = -0.5f;  // snore if class1 is close to class0
// Cooldown only prevents repeated triggers; lower = more responsive.
constexpr uint32_t kDetectCooldownMs = 500;
// Sliding window hop size (in samples). 16000 @16kHz ~= 1000ms.
constexpr size_t kHopSamples = 16000;

constexpr size_t kTensorArenaSize = 256 * 1024;
__attribute__((aligned(16))) static uint8_t tensor_arena[kTensorArenaSize] rt_section(".m33_m55_shared_hyperram");

static tflite::MicroInterpreter *g_interp = RT_NULL;
static TfLiteTensor *g_in = RT_NULL;
static TfLiteTensor *g_out = RT_NULL;
static rt_tick_t g_last_detect_tick = 0;

/* ── 1-second max score tracking ── */
static float g_max_score_this_second = -1.0f;
static rt_tick_t g_score_window_start = 0;

/* ── Decibel monitor (10-second ring buffer) ── */
#define DECIBEL_BUF_SIZE 10
static float g_decibel_buf[DECIBEL_BUF_SIZE];
static uint8_t g_decibel_idx = 0;
static uint32_t g_decibel_sample_count = 0;
static float g_decibel_sum = 0.0f;
static rt_tick_t g_last_decibel_tick = 0;

static rt_thread_t g_meow_tid = RT_NULL;        /* main detect thread (legacy) */
static rt_thread_t g_mic_tid = RT_NULL;         /* mic read thread */
static rt_thread_t g_infer_tid = RT_NULL;       /* inference thread */
static rt_event_t g_meow_event = RT_NULL;       /* detection event */
static volatile rt_bool_t g_meow_running = RT_FALSE;
static rt_device_t g_mic_dev = RT_NULL;
static rt_tick_t g_blue_blink_until = 0;
static rt_tick_t g_blue_blink_next_toggle = 0;
static rt_bool_t g_blue_blink_on = RT_FALSE;
static uint32_t g_infer_buf_pos = 0;            /* inference buffer position */

/* ── FFT / Mel workspace (static to save stack) ─────────────────────── */
static arm_rfft_fast_instance_f32 g_fft_inst;
ALIGN(16) static float g_fft_buf[kFFTSize] rt_section(".m33_m55_shared_hyperram");
ALIGN(16) static float g_fft_out[kFFTSize] rt_section(".m33_m55_shared_hyperram");
ALIGN(16) static float g_magnitude[kFFTOutputSize] rt_section(".m33_m55_shared_hyperram");
ALIGN(16) static float g_frame_f32[kFFTSize] rt_section(".m33_m55_shared_hyperram");
ALIGN(16) static float g_mel_spectrogram[kNumFrames][kMelBins] rt_section(".m33_m55_shared_hyperram");

/* Mel filterbank weights: 20 filters × 257 bins (computed at init) */
static float g_mel_weights[kMelBins][kFFTOutputSize] rt_section(".m33_m55_shared_hyperram");

/* Triangular filter points (matching DeepCraft model: _K13) */
static const short g_mel_filter_points[22] = {9, 13, 16, 21, 25, 31, 37, 43, 50, 58, 67, 77, 87, 99, 113, 127, 144, 162, 182, 204, 229, 256};

/* ── Ring buffer for mic_thread → infer_thread (cough_detect style) ── */
/* Use fixed value 160 (default PDM_FRAME_SAMPLES) to avoid forward reference issue */
constexpr int kRingBufFrames = 100;                      /* ~100 × 160 = 16000 samples */
constexpr int kRingBufSamples = kRingBufFrames * PDM_FRAME_SAMPLES;    /* 16000 samples */

static int16_t g_ring_buf[kRingBufSamples] rt_section(".m33_m55_shared_hyperram");
static uint32_t g_ring_wr = 0;
static uint32_t g_ring_rd = 0;
static rt_mutex_t g_ring_mutex = RT_NULL;

/* ── Inference buffer (accumulate samples for Mel spectrogram) ──────── */
constexpr int kInferWindowSamples = (kNumFrames - 1) * kHopSize + kFFTSize;  /* 15232 samples */
static int16_t g_infer_buf[kInferWindowSamples] rt_section(".m33_m55_shared_hyperram");

/* Quantization params (cached from tensor) */
static float g_input_scale = 0.0f;
static int g_input_zero_point = 0;

static void blue_blink_for_ms(uint32_t duration_ms, uint32_t period_ms)
{
    /* Non-blocking blink: handled in the detect thread loop */
    if (period_ms == 0)
        period_ms = 100;
    const rt_tick_t now = rt_tick_get();
    g_blue_blink_until = now + rt_tick_from_millisecond(duration_ms);
    g_blue_blink_next_toggle = now;
    g_blue_blink_on = RT_FALSE;
}

static void print_tensor_dims(TfLiteTensor *t, const char *name)
{
    if (!t || !t->dims) {
        LOG_I("%s: tensor is null or no dims", name);
        return;
    }
    int dims = t->dims->size;
    LOG_I("%s: type=%d, dims=%d, shape=[", name, (int)t->type, dims);
    for (int i = 0; i < dims; i++) {
        rt_kprintf("%d", t->dims->data[i]);
        if (i < dims - 1) rt_kprintf(",");
    }
    rt_kprintf("], bytes=%d, scale=%.6f, zp=%d\n", (int)t->bytes, t->params.scale, t->params.zero_point);
}

/* ================================================================
 *  Pre-compute triangular Mel filterbank weights
 * ================================================================ */
static void compute_mel_weights(void)
{
    /* Mel scale: f_mel = 2595 * log10(1 + f/700) */
    const float freq_to_mel = 2595.0f;
    const float sample_rate = (float)kSampleRate;
    const int n_fft = kFFTSize;
    const int n_bins = kFFTOutputSize;  // 257

    /* Convert bin index to frequency */
    auto bin_to_freq = [sample_rate, n_fft](int bin) {
        return (float)bin * sample_rate / n_fft;
    };
    /* Convert frequency to mel */
    auto freq_to_mel_fn = [freq_to_mel](float freq) {
        return freq_to_mel * log10f(1.0f + freq / 700.0f);
    };
    /* Convert mel to frequency */
    auto mel_to_freq_fn = [freq_to_mel](float mel) {
        return 700.0f * (powf(10.0f, mel / freq_to_mel) - 1.0f);
    };

    const float f_low = 0.0f;
    const float f_high = sample_rate / 2.0f;
    const float mel_low = freq_to_mel_fn(f_low);
    const float mel_high = freq_to_mel_fn(f_high);
    const float mel_step = (mel_high - mel_low) / (kMelBins + 1);

    for (int m = 0; m < kMelBins; m++) {
        float mel_center = mel_low + (m + 1) * mel_step;
        float mel_left = mel_center - mel_step;
        float mel_right = mel_center + mel_step;

        float f_left = mel_to_freq_fn(mel_left);
        float f_center = mel_to_freq_fn(mel_center);
        float f_right = mel_to_freq_fn(mel_right);

        for (int k = 0; k < n_bins; k++) {
            float f = bin_to_freq(k);
            float w = 0.0f;

            if (f >= f_left && f < f_center) {
                w = (f - f_left) / (f_center - f_left);
            } else if (f >= f_center && f <= f_right) {
                w = (f_right - f) / (f_right - f_center);
            }

            g_mel_weights[m][k] = w;
        }
    }

    LOG_I("Mel weights computed for %d filters x %d bins", kMelBins, n_bins);
}

static int init_model()
{
    if (g_interp)
        return 0;

    const tflite::Model *model = tflite::GetModel(snore_model_tflite);
    if (!model)
    {
        LOG_E("init_model: GetModel failed");
        return -RT_ERROR;
    }
    LOG_I("init_model: model=%p, arena=%d, subgraphs=%d",
          model, (int)kTensorArenaSize,
          (int)model->subgraphs()->size());

    EI_TFLITE_RESOLVER
    static tflite::MicroInterpreter interp(model, resolver, tensor_arena, kTensorArenaSize);

    TfLiteStatus st = interp.AllocateTensors(true);
    if (st != kTfLiteOk)
    {
        LOG_E("init_model: AllocateTensors failed status=%d, arena=%d", st, (int)kTensorArenaSize);
        LOG_I("init_model: subgraphs=%d, inputs=%d, outputs=%d",
              (int)model->subgraphs()->size(),
              (int)interp.inputs_size(), (int)interp.outputs_size());
        return -RT_ERROR;
    }

    g_interp = &interp;
    g_in = g_interp->input(0);
    g_out = g_interp->output(0);

    /* Print tensor dimensions for debugging */
    print_tensor_dims(g_in, "INPUT");
    print_tensor_dims(g_out, "OUTPUT");

    /* Cache quantization params from tensor */
    g_input_scale = g_in->params.scale;
    g_input_zero_point = g_in->params.zero_point;
    LOG_I("init_model: quant scale=%.6f zp=%d", g_input_scale, g_input_zero_point);

    /* Initialize CMSIS-DSP FFT */
    if (arm_rfft_fast_init_f32(&g_fft_inst, kFFTSize) != ARM_MATH_SUCCESS) {
        LOG_E("init_model: arm_rfft_fast_init_f32 failed");
        return -RT_ERROR;
    }

    /* Using fixed filter points (DeepCraft model), no need to compute weights */
    // compute_mel_weights();

    LOG_I("init_model: OK");
    return 0;
}

#ifdef RT_USING_FINSH
static void led_probe(void)
{
    leds_init_once();
    rt_kprintf("\n[led_probe] Toggle LED pins HIGH/LOW: R=P16_7, G=P16_6, B=P16_5\n");
    rt_kprintf("[led_probe] Please observe which level turns each LED ON.\n\n");

    struct
    {
        const char *name;
        int pin;
    } leds[] = {
        {"RED  (P16_7)", kLedRed},
        {"GREEN(P16_6)", kLedGreen},
        {"BLUE (P16_5)", kLedBlue},
    };

    for (size_t i = 0; i < sizeof(leds) / sizeof(leds[0]); i++)
    {
        rt_kprintf("[led_probe] %s: write HIGH for 1000ms\n", leds[i].name);
        rt_pin_write(leds[i].pin, PIN_HIGH);
        rt_thread_mdelay(1000);

        rt_kprintf("[led_probe] %s: write LOW  for 1000ms\n", leds[i].name);
        rt_pin_write(leds[i].pin, PIN_LOW);
        rt_thread_mdelay(1000);

        rt_kprintf("[led_probe] %s: OFF via led_write(false)\n\n", leds[i].name);
        led_write(leds[i].pin, RT_FALSE);
        rt_thread_mdelay(300);
    }

    rt_kprintf("[led_probe] Done.\n");
}
MSH_CMD_EXPORT(led_probe, Toggle LED pins HIGH/LOW to determine polarity);
#endif

static inline int8_t clamp_i8(int32_t v)
{
    if (v > 127)
        return 127;
    if (v < -128)
        return -128;
    return (int8_t)v;
}

static void pcm16_to_int8(const int16_t *pcm, int8_t *dst, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        // int16 -> float [-1, 1)
        const float x = (float)pcm[i] / 32768.0f;
        const int32_t q = (int32_t)(x / kInScale + (float)kInZeroPoint);
        dst[i] = clamp_i8(q);
    }
}



/* ================================================================
 *  Compute one Mel spectrogram frame (20 bins) from 512 samples
 * ================================================================ */
static void compute_mel_spectrum(const float *samples_512, float *mel_out)
{
    /* Apply Hann window */
    for (int i = 0; i < kFFTSize; i++) {
        float w = 0.5f * (1.0f - cosf(2.0f * PI * i / (kFFTSize - 1)));
        g_fft_buf[i] = samples_512[i] * w;
    }

    /* Forward Real FFT (output format: [real(dc), imag(dc)=0, real(1), imag(1), ..., real(nyq), imag(nyq)=0]) */
    arm_rfft_fast_f32(&g_fft_inst, g_fft_buf, g_fft_out, 0);

    /* --- CRITICAL FIX: Manually compute magnitude from RFFT output --- */
    // g_fft_out[0] is DC component (real part)
    g_magnitude[0] = fabsf(g_fft_out[0]);
    // g_fft_out[1] is Nyquist component (real part)
    g_magnitude[kFFTOutputSize - 1] = fabsf(g_fft_out[1]);
    // For complex bins 1 to kFFTOutputSize-2 (i.e., frequencies 1 to 255)
    for (int i = 1; i < kFFTOutputSize - 1; i++) {
        float real_part = g_fft_out[2 * i];
        float imag_part = g_fft_out[2 * i + 1];
        g_magnitude[i] = sqrtf(real_part * real_part + imag_part * imag_part);
    }
    /* --- END OF FIX --- */

        // Check for NaN or Inf in magnitude
    bool has_nan_or_inf = false;
    for (int k = 0; k < kFFTOutputSize; k++) {
        if (isnan(g_magnitude[k]) || isinf(g_magnitude[k])) {
            has_nan_or_inf = true;
            LOG_E("Magnitude[%d] is NaN or Inf: %f", k, g_magnitude[k]);
            break; // Only report the first occurrence
        }
    }
    if (has_nan_or_inf) {
        LOG_E("FFT Magnitude calculation FAILED!");
        // Optionally return or set all mel_out to a safe value
        for (int m = 0; m < kMelBins; m++) {
            mel_out[m] = 0.0f; // Or some default value
        }
        return; // Exit early if magnitude is invalid
    }

    // Print a few magnitude values and their range
    float mag_min = g_magnitude[0];
    float mag_max = g_magnitude[0];
    for (int k = 0; k < kFFTOutputSize; k++) {
        if (g_magnitude[k] < mag_min) mag_min = g_magnitude[k];
        if (g_magnitude[k] > mag_max) mag_max = g_magnitude[k];
    }

        
    /* Apply Mel filterbank (DeepCraft triangular filter) */
    for (int m = 0; m < kMelBins; m++) {
        short n0 = g_mel_filter_points[m];
        short n1 = g_mel_filter_points[m + 1];
        short n2 = g_mel_filter_points[m + 2];
        short c0 = n1 - n0;
        short c1 = n2 - n1;
        float sum = 0.0f;

        // Rising part: n0 to n1 (triangular, increasing)
        for (int i = 0; i <= c0; i++) {
            float rate = (float)i / (float)c0;
            sum += g_magnitude[i + n0] * rate;
        }

        // Falling part: n1 to n2 (triangular, decreasing)
        for (int i = 1; i <= c1; i++) {
            float rate = (float)i / (float)c1;
            sum += g_magnitude[i + n1] * (1.0f - rate);
        }

        mel_out[m] = logf(sum + 1e-6f);
    }
}

/* ================================================================
 *  Quantize float to int8 using cached scale/zero_point
 * ================================================================ */
static inline int8_t quantize_f32(float value)
{
    int32_t q = (int32_t)std::roundf(value / g_input_scale) + g_input_zero_point;
    return clamp_i8(q);
}



} // namespace

static int meow_infer_from_pcm(const int16_t *pcm, float *out_score, rt_bool_t *out_detected)
{
#ifndef RT_USING_AUDIO
    LOG_E("RT_USING_AUDIO not enabled");
    return -RT_ERROR;
#else
    if (init_model() != 0)
        return -RT_ERROR;

    leds_init_once();

    // Check model input requirements
    if (!g_in || !g_out) {
        LOG_E("Tensor not initialized");
        return -RT_ERROR;
    }

    // Snore model: INPUT [1,60,20] = 1200 bytes int8
    // Expected: Mel spectrogram features (60 frames x 20 bins)
    const int kExpectedInputBytes = 1200;
    const int kFeatureFrames = 60;
    const int kFeatureBins = 20;

    if (g_in->type != kTfLiteInt8) {
        LOG_E("Unexpected input type: %d", g_in->type);
        return -RT_ERROR;
    }

    // Debug: Print output tensor type and quant params (moved before input processing)
    LOG_I("infer: output type=%d, bytes=%d", g_out->type, (int)g_out->bytes);
    LOG_I("infer: output quant: scale=%.8f, zp=%d", g_out->params.scale, g_out->params.zero_point);

    if (g_in->bytes != kExpectedInputBytes) {
        LOG_E("Input bytes mismatch: expected=%d, got=%d",
              kExpectedInputBytes, (int)g_in->bytes);
        return -RT_ERROR;
    }

    /* ── Extract Mel spectrogram from PCM ───────────────────────── */
    /*
     *  Frame 0  : pcm[0       .. 511]
     *  Frame 1  : pcm[256     .. 767]
     *  ...
     *  Total needed: 60 frames (model expects [1,60,20,1])
     *
     *  PCM total: 32000 samples (2 sec @ 16kHz)
     *  With hop=256: max frames = (32000-512)/256 + 1 = 125 frames
     */
    const int max_frames = (int)kInputSamples - kFFTSize + 1;
    const int frames_per_block = max_frames / kHopSize;  // ~124 frames available

    /* Process first kFeatureFrames frames from PCM */
    for (int f = 0; f < kFeatureFrames; f++) {
        int offset = f * kHopSize;
        if (offset + kFFTSize > kInputSamples) break;

        /* Convert int16 PCM → float [-1.0, 1.0) */
        for (int i = 0; i < kFFTSize; i++) {
            g_frame_f32[i] = (float)pcm[offset + i] / 32768.0f;
        }

        /* Compute Mel spectrogram frame (20 bins) */
        compute_mel_spectrum(g_frame_f32, g_mel_spectrogram[f]);

        /* Quantize and write to tensor input
         * Tensor layout: [batch=1][frame][mel_bin][channel=1]
         * Flattened: [frame * 20 + bin] */
        int t_off = f * kFeatureBins;
        for (int b = 0; b < kFeatureBins; b++) {
            g_in->data.int8[t_off + b] = quantize_f32(g_mel_spectrogram[f][b]);
        }
    }

    // Debug: Print samples from throughout the input tensor
    LOG_I("infer: input[0..4]=%d,%d,%d,%d,%d",
          g_in->data.int8[0], g_in->data.int8[1], g_in->data.int8[2],
          g_in->data.int8[3], g_in->data.int8[4]);
    LOG_I("infer: input[500..504]=%d,%d,%d,%d,%d",
          g_in->data.int8[500], g_in->data.int8[501], g_in->data.int8[502],
          g_in->data.int8[503], g_in->data.int8[504]);
    LOG_I("infer: input[595..599]=%d,%d,%d,%d,%d (frame 29 last 5)",
          g_in->data.int8[595], g_in->data.int8[596], g_in->data.int8[597],
          g_in->data.int8[598], g_in->data.int8[599]);
    LOG_I("infer: input[1195..1199]=%d,%d,%d,%d,%d (last 5)",
          g_in->data.int8[1195], g_in->data.int8[1196], g_in->data.int8[1197],
          g_in->data.int8[1198], g_in->data.int8[1199]);

    if (g_interp->Invoke() != kTfLiteOk)
    {
        LOG_E("Invoke failed");
        return -RT_ERROR;
    }

    /* ── Read output AFTER inference ─────────────────────────────── */
    // Support both int8 and float output tensors
    float out0, out1;
    if (g_out->type == kTfLiteFloat32) {
        out0 = g_out->data.f[0];
        out1 = g_out->data.f[1];
        LOG_I("infer: output (float): out0=%.6f, out1=%.6f", out0, out1);
    } else if (g_out->type == kTfLiteInt8) {
        // Model output: [1,2] int8 quantized
        // Class 0: non-snore, Class 1: snore
        // Must use int8 and dequantize
        const int8_t out_q0 = g_out->data.int8[0];
        const int8_t out_q1 = g_out->data.int8[1];
        const float out_scale = g_out->params.scale;
        const float out_zp = (float)g_out->params.zero_point;

        // Debug: Print raw output tensor bytes
        LOG_I("infer: raw output bytes[0..1]=%d,%d",
              g_out->data.int8[0], g_out->data.int8[1]);

        // Dequantize: float = (int8 - zero_point) * scale
        out0 = (out_q0 - out_zp) * out_scale;
        out1 = (out_q1 - out_zp) * out_scale;

        LOG_I("infer: out_quant: scale=%.8f, zp=%.1f, raw_q[0]=%d, q[1]=%d",
              out_scale, out_zp, out_q0, out_q1);
        LOG_I("infer: dequant: out0=%.6f, out1=%.6f", out0, out1);
    } else {
        LOG_E("Unsupported output type: %d", g_out->type);
        return -RT_ERROR;
    }

    /* Debug: print input data values (first 10) */
    // Print first few values of the first Mel spectrogram frame (unquantized floats)
    LOG_I("infer: mel[0][0..4]=%.6f,%.6f,%.6f,%.6f,%.6f",
        g_mel_spectrogram[0][0], g_mel_spectrogram[0][1], g_mel_spectrogram[0][2],
        g_mel_spectrogram[0][3], g_mel_spectrogram[0][4]);

    // Print first few values of the model input tensor (quantized ints)
    LOG_I("infer: input[0..4]=%d,%d,%d,%d,%d",
        g_in->data.int8[0], g_in->data.int8[1], g_in->data.int8[2],
        g_in->data.int8[3], g_in->data.int8[4]);

    // Print min/max of the first frame's Mel values (to check range before quantization)
    float min_mel = g_mel_spectrogram[0][0];
    float max_mel = g_mel_spectrogram[0][0];
    for (int b = 1; b < kMelBins; b++) {
        if (g_mel_spectrogram[0][b] < min_mel) min_mel = g_mel_spectrogram[0][b];
        if (g_mel_spectrogram[0][b] > max_mel) max_mel = g_mel_spectrogram[0][b];
    }
    LOG_I("infer: mel[0] range: min=%.6f, max=%.6f", min_mel, max_mel);

    // Print quantization parameters being used
    LOG_I("infer: quant_params: scale=%.8f, zp=%d", g_input_scale, g_input_zero_point);

    // Model outputs raw logits (not softmax probabilities)
    // Dequantized values are direct class scores
    // Use out1 - out0 as the snore score (logit difference)
    const float snore_score = out1 - out0;  // Logit difference

    LOG_I("infer: snore_score=%.6f (class0=%.6f, class1=%.6f)", snore_score, out0, out1);

    /* ── Track max score within 1-second window ── */
    const rt_tick_t now = rt_tick_get();
    if (g_score_window_start == 0) g_score_window_start = now;

    // Reset window every 1 second
    if (now - g_score_window_start >= RT_TICK_PER_SECOND) {
        LOG_I("infer: 1s max score = %.6f", g_max_score_this_second);
        g_max_score_this_second = -1.0f;
        g_score_window_start = now;
    }

    // Update max score
    if (snore_score > g_max_score_this_second) {
        g_max_score_this_second = snore_score;
    }

    const rt_bool_t cooldown_ok = (g_last_detect_tick == 0) ||
                                  ((now - g_last_detect_tick) >= rt_tick_from_millisecond(kDetectCooldownMs));

    // Use max score within 1 second for detection
    const float detect_score = g_max_score_this_second;
    const rt_bool_t detected = (detect_score >= kSnoreThreshold && cooldown_ok);

    if (detected)
    {
        g_last_detect_tick = now;
        LOG_W("SNORE DETECTED: 1s_max=%.3f (inst=%.3f) threshold=%.2f", detect_score, snore_score, kSnoreThreshold);
    }
    else
    {
        LOG_I("NO SNORE: 1s_max=%.3f (inst=%.3f) threshold=%.2f%s",
              detect_score, snore_score, kSnoreThreshold,
              (detect_score >= kSnoreThreshold && !cooldown_ok) ? " (cooldown)" : "");
    }

    if (out_score)
        *out_score = detect_score;  // Return the 1-second max
    if (out_detected)
        *out_detected = detected;

    return RT_EOK;
#endif
}

/* ── mic_thread: reads PDM frames and pushes to ring buffer ────────────── */
static void mic_thread_entry(void *param)
{
    (void)param;
    int16_t frame[PDM_FRAME_SAMPLES];

    LOG_I("snore: mic_thread started");

    while (g_meow_running)
    {
        rt_size_t bytes = rt_device_read(g_mic_dev, 0, frame, PDM_FRAME_SIZE);
        if (bytes != PDM_FRAME_SIZE)
        {
            rt_thread_mdelay(1);
            continue;
        }

        /* Accumulate samples for decibel calculation */
        for (int i = 0; i < PDM_FRAME_SAMPLES; i++) {
            g_decibel_sum += (float)frame[i] * (float)frame[i];
            g_decibel_sample_count++;
        }

        /* Push frame into the ring buffer */
        rt_mutex_take(g_ring_mutex, RT_WAITING_FOREVER);
        for (int i = 0; i < PDM_FRAME_SAMPLES; i++)
        {
            g_ring_buf[g_ring_wr % kRingBufSamples] = frame[i];
            g_ring_wr++;
        }
        rt_mutex_release(g_ring_mutex);

        /* Every 1 second: calculate decibel and send via WebSocket */
        rt_tick_t now = rt_tick_get();
        if (g_last_decibel_tick == 0) g_last_decibel_tick = now;

        if (now - g_last_decibel_tick >= RT_TICK_PER_SECOND) {
            float db = -60.0f;
            if (g_decibel_sample_count > 0) {
                float rms = sqrtf(g_decibel_sum / g_decibel_sample_count);
                if (rms > 1e-6f) {
                    db = 20.0f * log10f(rms / 32768.0f) + 90.0f;  // Approximate dB SPL
                }
            }

            /* Write to ring buffer */
            g_decibel_buf[g_decibel_idx] = db;
            g_decibel_idx = (g_decibel_idx + 1) % DECIBEL_BUF_SIZE;

            LOG_I("decibel: %.1f dB", db);

            /* Build and send JSON via WebSocket */
            char json_buf[256];
            int pos = rt_snprintf(json_buf, sizeof(json_buf),
                "{\"timestamp\":%lu,\"decibel_array\":[",
                (unsigned long)(now / RT_TICK_PER_SECOND + 1700000000));

            for (int i = 0; i < DECIBEL_BUF_SIZE; i++) {
                int idx = (g_decibel_idx + i) % DECIBEL_BUF_SIZE;
                pos += rt_snprintf(json_buf + pos, sizeof(json_buf) - pos, "%.1f%s",
                    g_decibel_buf[idx], (i < DECIBEL_BUF_SIZE - 1) ? "," : "");
            }
            pos += rt_snprintf(json_buf + pos, sizeof(json_buf) - pos, "]}");

            /* Send via dedicated decibel WebSocket */
            extern void decibel_ws_send(uint8_t *data, int len);
            decibel_ws_send((uint8_t*)json_buf, pos);

            /* Reset accumulators */
            g_decibel_sum = 0.0f;
            g_decibel_sample_count = 0;
            g_last_decibel_tick = now;
        }
    }

    LOG_I("snore: mic_thread exiting");
}

/* ── infer_thread: accumulates audio and runs Mel spectrogram inference ── */
static void infer_thread_entry(void *param)
{
    (void)param;
    int16_t frame[PDM_FRAME_SAMPLES];
    g_infer_buf_pos = 0;

    LOG_I("snore: infer_thread started");

    /* Wait for model to be initialized (done by main thread) */
    while (!g_interp)
    {
        if (!g_meow_running) goto exit;
        rt_thread_mdelay(10);
    }

    while (g_meow_running)
    {
        /* Check available samples in ring buffer */
        rt_mutex_take(g_ring_mutex, RT_WAITING_FOREVER);
        uint32_t avail = g_ring_wr - g_ring_rd;
        rt_mutex_release(g_ring_mutex);

        if (avail < PDM_FRAME_SAMPLES)
        {
            rt_thread_mdelay(5);
            continue;
        }

        /* Copy one frame out of the ring buffer */
        rt_mutex_take(g_ring_mutex, RT_WAITING_FOREVER);
        for (int i = 0; i < PDM_FRAME_SAMPLES; i++)
        {
            frame[i] = g_ring_buf[g_ring_rd % kRingBufSamples];
            g_ring_rd++;
        }
        rt_mutex_release(g_ring_mutex);

        /* Accumulate into inference buffer */
        if (g_infer_buf_pos + PDM_FRAME_SAMPLES <= kInferWindowSamples)
        {
            for (int i = 0; i < PDM_FRAME_SAMPLES; i++)
            {
                g_infer_buf[g_infer_buf_pos + i] = frame[i];
            }
            g_infer_buf_pos += PDM_FRAME_SAMPLES;
        }

        /* Trigger inference when buffer is full */
        if (g_infer_buf_pos + PDM_FRAME_SAMPLES > kInferWindowSamples)
        {
            /* Zero-pad if needed */
            if (g_infer_buf_pos < kInferWindowSamples)
            {
                rt_memset(&g_infer_buf[g_infer_buf_pos], 0,
                          (kInferWindowSamples - g_infer_buf_pos) * sizeof(int16_t));
            }

            /* Run inference */
            float score = 0.0f;
            rt_bool_t detected = RT_FALSE;
            int ret = meow_infer_from_pcm(g_infer_buf, &score, &detected);

            if (ret == RT_EOK && detected)
            {
                /* Send detection event */
                rt_event_send(g_meow_event, 0x01);
            }

            /* Reset for next window */
            g_infer_buf_pos = 0;
        }
    }

exit:
    LOG_I("snore: infer_thread exiting");
}

#define MEOW_THREAD_STACK_SIZE 4096
#define MEOW_THREAD_PRIORITY   18
#define MEOW_THREAD_TICK       10

static void meow_detect_thread_entry(void *parameter)
{
    (void)parameter;

    LOG_I("snore: sliding window started (2s window, hop=%d samples)", (int)kHopSamples);

    if (init_model() != 0)
    {
        LOG_E("snore: init_model failed");
        g_meow_running = RT_FALSE;
        g_meow_tid = RT_NULL;
        return;
    }

    leds_init_once();
    led_write(kLedBlue, RT_FALSE);

    g_mic_dev = rt_device_find(BSP_XIAOZHI_MIC_DEVICE_NAME);
    if (!g_mic_dev)
    {
        LOG_E("snore: cannot find audio device '%s'", BSP_XIAOZHI_MIC_DEVICE_NAME);
        g_meow_running = RT_FALSE;
        g_meow_tid = RT_NULL;
        return;
    }

    if (rt_device_open(g_mic_dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK)
    {
        LOG_E("snore: cannot open audio device");
        g_mic_dev = RT_NULL;
        g_meow_running = RT_FALSE;
        g_meow_tid = RT_NULL;
        return;
    }

    /* Legacy single-thread mode - kept for compatibility */
    static int16_t ring[kInputSamples] rt_section(".m33_m55_shared_hyperram");
    static int16_t snap[kInputSamples] rt_section(".m33_m55_shared_hyperram");
    int16_t pdm_frame[PDM_FRAME_SAMPLES];
    size_t w = 0;
    size_t filled = 0;
    size_t since_last = 0;

    while (g_meow_running)
    {
        const rt_size_t read_size = rt_device_read(g_mic_dev, 0, pdm_frame, PDM_FRAME_SIZE);
        if (read_size == 0)
        {
            /* keep UI responsive and allow quick stop */
            rt_thread_mdelay(1);
            continue;
        }

        const size_t total_samples = read_size / sizeof(int16_t);
#if PDM_IS_STEREO
        const size_t mono_samples = total_samples / 2;
        for (size_t i = 0; i < mono_samples; i++)
        {
            ring[w] = pdm_frame[i * 2 + 1];
            w = (w + 1) % kInputSamples;
            if (filled < kInputSamples) filled++;
            since_last++;
        }
#else
        for (size_t i = 0; i < total_samples; i++)
        {
            ring[w] = pdm_frame[i];
            w = (w + 1) % kInputSamples;
            if (filled < kInputSamples) filled++;
            since_last++;
        }
#endif

        /* Non-blocking blue blink handling */
        const rt_tick_t now = rt_tick_get();
        if (g_blue_blink_until != 0 && now < g_blue_blink_until)
        {
            if (now >= g_blue_blink_next_toggle)
            {
                g_blue_blink_on = (g_blue_blink_on == RT_FALSE) ? RT_TRUE : RT_FALSE;
                led_write(kLedBlue, g_blue_blink_on);
                g_blue_blink_next_toggle = now + rt_tick_from_millisecond(50);
            }
        }
        else
        {
            g_blue_blink_until = 0;
            g_blue_blink_on = RT_FALSE;
            led_write(kLedBlue, RT_FALSE);
        }

        if (filled < kInputSamples)
            continue;

        if (since_last < kHopSamples)
            continue;

        since_last = 0;

        /* Snapshot last 2s from ring into linear buffer */
        size_t tail = w; /* w points to next write => oldest is w */
        const size_t n1 = kInputSamples - tail;
        memcpy(&snap[0], &ring[tail], n1 * sizeof(int16_t));
        if (tail != 0)
            memcpy(&snap[n1], &ring[0], tail * sizeof(int16_t));

        /* During "sampling+infer", keep blue off (unless blinking) */
        if (g_blue_blink_until == 0)
            led_write(kLedBlue, RT_FALSE);

        float score = 0.0f;
        rt_bool_t detected = RT_FALSE;
        int ret = meow_infer_from_pcm(snap, &score, &detected);
        if (ret != RT_EOK)
        {
            LOG_W("snore: infer failed (%d)", ret);
            continue;
        }

        xiaozhi_ui_set_meow_result(detected ? true : false, score);
        if (detected)
        {
            blue_blink_for_ms(1000, 100);
        }
    }

    if (g_mic_dev)
    {
        rt_device_close(g_mic_dev);
        g_mic_dev = RT_NULL;
    }

    LOG_I("snore: detect thread exiting");
    g_meow_tid = RT_NULL;
}

/* ── Event handler thread (waits for detection events from infer_thread) ── */
static void meow_event_thread_entry(void *param)
{
    (void)param;
    rt_uint32_t recv = 0;

    LOG_I("snore: event thread started");

    while (g_meow_running)
    {
        rt_err_t ret = rt_event_recv(g_meow_event, 0x01,
                                      RT_EVENT_FLAG_OR | RT_EVENT_FLAG_CLEAR,
                                      rt_tick_from_millisecond(100), &recv);
        if (ret != RT_EOK)
            continue;

        if (recv & 0x01)
        {
            LOG_W("SNORE DETECTED via event");
            xiaozhi_ui_set_meow_result(true, 1.0f);
            blue_blink_for_ms(1000, 100);
        }
    }

    LOG_I("snore: event thread exiting");
}

extern "C" {

int meow_detect_start(void)
{
#ifndef RT_USING_AUDIO
    LOG_E("RT_USING_AUDIO not enabled");
    return -RT_ERROR;
#else
    /* Print memory info */
    {
        rt_size_t total = 0, used = 0, max_used = 0;
        rt_memory_info(&total, &used, &max_used);
        LOG_I("=== Memory Info ===");
        LOG_I("total: %u KB, used: %u KB, available: %u KB",
              total / 1024, used / 1024, (total - used) / 1024);
        LOG_I("ring_buf: %u KB, infer_buf: %u KB",
              (unsigned)(kRingBufSamples * sizeof(int16_t)) / 1024,
              (unsigned)(kInferWindowSamples * sizeof(int16_t)) / 1024);
    }

    if (g_meow_running)
    {
        LOG_I("snore: detect already running");
        return RT_EOK;
    }

    /* Initialize model first */
    if (init_model() != 0)
    {
        return -RT_ERROR;
    }

    /* Create synchronization objects */
    g_ring_mutex = rt_mutex_create("meow_rbuf", RT_IPC_FLAG_FIFO);
    if (!g_ring_mutex)
    {
        LOG_E("snore: create ring mutex failed");
        return -RT_ERROR;
    }

    g_meow_event = rt_event_create("meow_evt", RT_IPC_FLAG_FIFO);
    if (!g_meow_event)
    {
        LOG_E("snore: create event failed");
        rt_mutex_delete(g_ring_mutex);
        g_ring_mutex = RT_NULL;
        return -RT_ERROR;
    }

    /* Open microphone device */
    g_mic_dev = rt_device_find(BSP_XIAOZHI_MIC_DEVICE_NAME);
    if (!g_mic_dev)
    {
        LOG_E("snore: cannot find audio device '%s'", BSP_XIAOZHI_MIC_DEVICE_NAME);
        rt_event_delete(g_meow_event);
        rt_mutex_delete(g_ring_mutex);
        g_meow_event = RT_NULL;
        g_ring_mutex = RT_NULL;
        return -RT_ERROR;
    }

    if (rt_device_open(g_mic_dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK)
    {
        LOG_E("snore: cannot open audio device");
        g_mic_dev = RT_NULL;
        rt_event_delete(g_meow_event);
        rt_mutex_delete(g_ring_mutex);
        g_meow_event = RT_NULL;
        g_ring_mutex = RT_NULL;
        return -RT_ERROR;
    }

    leds_init_once();
    // Running: red off, green on, blue off
    led_write(kLedRed, RT_FALSE);
    led_write(kLedGreen, RT_TRUE);
    led_write(kLedBlue, RT_FALSE);

    g_meow_running = RT_TRUE;
    g_ring_wr = 0;
    g_ring_rd = 0;
    g_infer_buf_pos = 0;

    /* Create mic thread */
    g_mic_tid = rt_thread_create("meow_mic",
                                  mic_thread_entry,
                                  RT_NULL,
                                  1024 * 2,
                                  MEOW_THREAD_PRIORITY,
                                  MEOW_THREAD_TICK);
    if (!g_mic_tid)
    {
        LOG_E("snore: create mic thread failed");
        g_meow_running = RT_FALSE;
        rt_device_close(g_mic_dev);
        g_mic_dev = RT_NULL;
        rt_event_delete(g_meow_event);
        rt_mutex_delete(g_ring_mutex);
        g_meow_event = RT_NULL;
        g_ring_mutex = RT_NULL;
        return -RT_ENOMEM;
    }

    /* Create infer thread */
    g_infer_tid = rt_thread_create("meow_inf",
                                    infer_thread_entry,
                                    RT_NULL,
                                    1024 * 8,
                                    MEOW_THREAD_PRIORITY - 1,
                                    MEOW_THREAD_TICK);
    if (!g_infer_tid)
    {
        LOG_E("snore: create infer thread failed");
        g_meow_running = RT_FALSE;
        rt_thread_delete(g_mic_tid);
        g_mic_tid = RT_NULL;
        rt_device_close(g_mic_dev);
        g_mic_dev = RT_NULL;
        rt_event_delete(g_meow_event);
        rt_mutex_delete(g_ring_mutex);
        g_meow_event = RT_NULL;
        g_ring_mutex = RT_NULL;
        return -RT_ENOMEM;
    }

    /* Create event handler thread */
    g_meow_tid = rt_thread_create("meow_evt",
                                  meow_event_thread_entry,
                                  RT_NULL,
                                  1024 * 2,
                                  MEOW_THREAD_PRIORITY + 1,
                                  MEOW_THREAD_TICK);
    if (!g_meow_tid)
    {
        LOG_E("snore: create event thread failed");
        g_meow_running = RT_FALSE;
        rt_thread_delete(g_mic_tid);
        rt_thread_delete(g_infer_tid);
        g_mic_tid = RT_NULL;
        g_infer_tid = RT_NULL;
        rt_device_close(g_mic_dev);
        g_mic_dev = RT_NULL;
        rt_event_delete(g_meow_event);
        rt_mutex_delete(g_ring_mutex);
        g_meow_event = RT_NULL;
        g_ring_mutex = RT_NULL;
        return -RT_ENOMEM;
    }

    /* Start threads */
    rt_thread_startup(g_mic_tid);
    rt_thread_startup(g_infer_tid);
    rt_thread_startup(g_meow_tid);

    LOG_I("snore: detect started (dual-thread mode)");
    return RT_EOK;
#endif
}

int meow_detect_stop(void)
{
    /* Idempotent stop: always force LED state to "stopped" */
    g_meow_running = RT_FALSE;

    /* Wait for threads to exit */
    if (g_meow_tid)
    {
        rt_thread_mdelay(50);
        g_meow_tid = RT_NULL;
    }
    if (g_mic_tid)
    {
        rt_thread_mdelay(50);
        g_mic_tid = RT_NULL;
    }
    if (g_infer_tid)
    {
        rt_thread_mdelay(50);
        g_infer_tid = RT_NULL;
    }

    /* Close and cleanup */
    if (g_mic_dev)
    {
        rt_device_close(g_mic_dev);
        g_mic_dev = RT_NULL;
    }

    if (g_meow_event)
    {
        rt_event_delete(g_meow_event);
        g_meow_event = RT_NULL;
    }

    if (g_ring_mutex)
    {
        rt_mutex_delete(g_ring_mutex);
        g_ring_mutex = RT_NULL;
    }

    // Stopped: red on, green off, blue off
    leds_init_once();
    led_write(kLedRed, RT_TRUE);
    led_write(kLedGreen, RT_FALSE);
    led_write(kLedBlue, RT_FALSE);

    LOG_I("snore: detect stopped");
    return RT_EOK;
}

} // extern "C"

#ifdef RT_USING_FINSH
/*
 * Debug command: capture ~2s audio and infer once.
 * This is kept for manual testing via MSH and is independent of the sliding-window thread.
 */
static int meow_detect_once(void)
{
#ifndef RT_USING_AUDIO
    LOG_E("RT_USING_AUDIO not enabled");
    return -RT_ERROR;
#else
    if (init_model() != 0)
        return -RT_ERROR;

    rt_device_t dev = rt_device_find(BSP_XIAOZHI_MIC_DEVICE_NAME);
    if (!dev)
    {
        LOG_E("Cannot find audio device '%s'", BSP_XIAOZHI_MIC_DEVICE_NAME);
        return -RT_ERROR;
    }

    if (rt_device_open(dev, RT_DEVICE_FLAG_RDONLY) != RT_EOK)
    {
        LOG_E("Cannot open audio device");
        return -RT_ERROR;
    }

    static int16_t pcm[kInputSamples] rt_section(".m33_m55_shared_hyperram");
    int16_t pdm_frame[PDM_FRAME_SAMPLES];

    size_t collected = 0;
    while (collected < kInputSamples)
    {
        const rt_size_t read_size = rt_device_read(dev, 0, pdm_frame, PDM_FRAME_SIZE);
        if (read_size == 0)
        {
            rt_thread_mdelay(1);
            continue;
        }

        const size_t total_samples = read_size / sizeof(int16_t);
#if PDM_IS_STEREO
        const size_t mono_samples = total_samples / 2;
        for (size_t i = 0; i < mono_samples && collected < kInputSamples; i++)
        {
            pcm[collected++] = pdm_frame[i * 2 + 1]; // right channel
        }
#else
        for (size_t i = 0; i < total_samples && collected < kInputSamples; i++)
        {
            pcm[collected++] = pdm_frame[i];
        }
#endif
    }

    rt_device_close(dev);

    led_write(kLedBlue, RT_FALSE);

    float score = 0.0f;
    rt_bool_t detected = RT_FALSE;
    int ret = meow_infer_from_pcm(pcm, &score, &detected);
    if (ret != RT_EOK)
        return ret;

    xiaozhi_ui_set_meow_result(detected ? true : false, score);
    if (detected)
        blue_blink_for_ms(1000, 100);

    return RT_EOK;
#endif
}

MSH_CMD_EXPORT(meow_detect_once, Capture mic0 audio (32000 samples) and run meow model once);
#endif

