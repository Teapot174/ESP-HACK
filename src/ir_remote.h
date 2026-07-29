#pragma once


/* ir_remote_icon.h */
/**
 * @file icon.h
 * GUI: Icon API
 */

#pragma once

#include <stdint.h>
#ifndef FURI_DEPRECATED
#define FURI_DEPRECATED
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Icon Icon;

/** Get icon width
 *
 * @param[in]  instance  pointer to Icon data
 *
 * @return     width in pixels
 */
uint16_t icon_get_width(const Icon* instance);

/** Get icon height
 *
 * @param[in]  instance  pointer to Icon data
 *
 * @return     height in pixels
 */
uint16_t icon_get_height(const Icon* instance);

/** Get Icon XBM bitmap data for the first frame
 *
 * @param[in]  instance  pointer to Icon data
 *
 * @return     pointer to compressed XBM bitmap data
 */
FURI_DEPRECATED const uint8_t* icon_get_data(const Icon* instance);

/** Get Icon frame count
 *
 * @param[in]  instance  pointer to Icon data
 *
 * @return     frame count
 */
uint32_t icon_get_frame_count(const Icon* instance);

/** Get Icon XBM bitmap data for a particular frame
 *
 * @param[in]  instance  pointer to Icon data
 * @param[in]  frame     frame index
 *
 * @return     pointer to compressed XBM bitmap data
 */
const uint8_t* icon_get_frame_data(const Icon* instance, uint32_t frame);

#ifdef __cplusplus
}
#endif


/* ir_remote_icon_i.h */
/**
 * @file icon_i.h
 * GUI: internal Icon API
 */

#pragma once
#include <stdint.h>

struct Icon {
    const uint16_t width;
    const uint16_t height;
    const uint8_t frame_count;
    const uint8_t frame_rate;
    const uint8_t* const* frames;
};


/* ir_remote_heatshrink_common.h */
#ifndef HEATSHRINK_H
#define HEATSHRINK_H

#define HEATSHRINK_AUTHOR "Scott Vokes <vokes.s@gmail.com>"
#define HEATSHRINK_URL "https://github.com/atomicobject/heatshrink"

/* Version 0.4.1 */
#define HEATSHRINK_VERSION_MAJOR 0
#define HEATSHRINK_VERSION_MINOR 4
#define HEATSHRINK_VERSION_PATCH 1

#define HEATSHRINK_MIN_WINDOW_BITS 4
#define HEATSHRINK_MAX_WINDOW_BITS 15

#define HEATSHRINK_MIN_LOOKAHEAD_BITS 3

#define HEATSHRINK_LITERAL_MARKER 0x01
#define HEATSHRINK_BACKREF_MARKER 0x00

#endif


/* ir_remote_heatshrink_config.h */
#ifndef HEATSHRINK_CONFIG_H
#define HEATSHRINK_CONFIG_H

/* Should functionality assuming dynamic allocation be used? */
#ifndef HEATSHRINK_DYNAMIC_ALLOC
#define HEATSHRINK_DYNAMIC_ALLOC 1
#endif

#if HEATSHRINK_DYNAMIC_ALLOC
    /* Optional replacement of malloc/free */
    #define HEATSHRINK_MALLOC(SZ) malloc(SZ)
    #define HEATSHRINK_FREE(P, SZ) free(P)
#else
    /* Required parameters for static configuration */
    #define HEATSHRINK_STATIC_INPUT_BUFFER_SIZE 32
    #define HEATSHRINK_STATIC_WINDOW_BITS 8
    #define HEATSHRINK_STATIC_LOOKAHEAD_BITS 4
#endif

/* Turn on logging for debugging. */
#define HEATSHRINK_DEBUGGING_LOGS 0

/* Use indexing for faster compression. (This requires additional space.) */
#define HEATSHRINK_USE_INDEX 1

#endif


/* ir_remote_heatshrink_decoder.h */
#ifndef HEATSHRINK_DECODER_H
#define HEATSHRINK_DECODER_H

#include <stdint.h>
#include <stddef.h>



typedef enum {
    HSDR_SINK_OK,               /* data sunk, ready to poll */
    HSDR_SINK_FULL,             /* out of space in internal buffer */
    HSDR_SINK_ERROR_NULL=-1,    /* NULL argument */
} HSD_sink_res;

typedef enum {
    HSDR_POLL_EMPTY,            /* input exhausted */
    HSDR_POLL_MORE,             /* more data remaining, call again w/ fresh output buffer */
    HSDR_POLL_ERROR_NULL=-1,    /* NULL arguments */
    HSDR_POLL_ERROR_UNKNOWN=-2,
} HSD_poll_res;

typedef enum {
    HSDR_FINISH_DONE,           /* output is done */
    HSDR_FINISH_MORE,           /* more output remains */
    HSDR_FINISH_ERROR_NULL=-1,  /* NULL arguments */
} HSD_finish_res;

#if HEATSHRINK_DYNAMIC_ALLOC
#define HEATSHRINK_DECODER_INPUT_BUFFER_SIZE(BUF) \
    ((BUF)->input_buffer_size)
#define HEATSHRINK_DECODER_WINDOW_BITS(BUF) \
    ((BUF)->window_sz2)
#define HEATSHRINK_DECODER_LOOKAHEAD_BITS(BUF) \
    ((BUF)->lookahead_sz2)
#else
#define HEATSHRINK_DECODER_INPUT_BUFFER_SIZE(_) \
    HEATSHRINK_STATIC_INPUT_BUFFER_SIZE
#define HEATSHRINK_DECODER_WINDOW_BITS(_) \
    (HEATSHRINK_STATIC_WINDOW_BITS)
#define HEATSHRINK_DECODER_LOOKAHEAD_BITS(BUF) \
    (HEATSHRINK_STATIC_LOOKAHEAD_BITS)
#endif

typedef struct {
    uint16_t input_size;        /* bytes in input buffer */
    uint16_t input_index;       /* offset to next unprocessed input byte */
    uint16_t output_count;      /* how many bytes to output */
    uint16_t output_index;      /* index for bytes to output */
    uint16_t head_index;        /* head of window buffer */
    uint8_t state;              /* current state machine node */
    uint8_t current_byte;       /* current byte of input */
    uint8_t bit_index;          /* current bit index */

#if HEATSHRINK_DYNAMIC_ALLOC
    /* Fields that are only used if dynamically allocated. */
    uint8_t window_sz2;         /* window buffer bits */
    uint8_t lookahead_sz2;      /* lookahead bits */
    uint16_t input_buffer_size; /* input buffer size */

    /* Input buffer, then expansion window buffer */
    uint8_t buffers[];
#else
    /* Input buffer, then expansion window buffer */
    uint8_t buffers[(1 << HEATSHRINK_DECODER_WINDOW_BITS(_))
        + HEATSHRINK_DECODER_INPUT_BUFFER_SIZE(_)];
#endif
} heatshrink_decoder;

#if HEATSHRINK_DYNAMIC_ALLOC
/* Allocate a decoder with an input buffer of INPUT_BUFFER_SIZE bytes,
 * an expansion buffer size of 2^WINDOW_SZ2, and a lookahead
 * size of 2^lookahead_sz2. (The window buffer and lookahead sizes
 * must match the settings used when the data was compressed.)
 * Returns NULL on error. */
heatshrink_decoder *heatshrink_decoder_alloc(uint16_t input_buffer_size,
    uint8_t expansion_buffer_sz2, uint8_t lookahead_sz2);

/* Free a decoder. */
void heatshrink_decoder_free(heatshrink_decoder *hsd);
#endif

/* Reset a decoder. */
void heatshrink_decoder_reset(heatshrink_decoder *hsd);

/* Sink at most SIZE bytes from IN_BUF into the decoder. *INPUT_SIZE is set to
 * indicate how many bytes were actually sunk (in case a buffer was filled). */
HSD_sink_res heatshrink_decoder_sink(heatshrink_decoder *hsd,
    uint8_t *in_buf, size_t size, size_t *input_size);

/* Poll for output from the decoder, copying at most OUT_BUF_SIZE bytes into
 * OUT_BUF (setting *OUTPUT_SIZE to the actual amount copied). */
HSD_poll_res heatshrink_decoder_poll(heatshrink_decoder *hsd,
    uint8_t *out_buf, size_t out_buf_size, size_t *output_size);

/* Notify the dencoder that the input stream is finished.
 * If the return value is HSDR_FINISH_MORE, there is still more output, so
 * call heatshrink_decoder_poll and repeat. */
HSD_finish_res heatshrink_decoder_finish(heatshrink_decoder *hsd);

#endif


/* ir_remote_heatshrink_encoder.h */
#ifndef HEATSHRINK_ENCODER_H
#define HEATSHRINK_ENCODER_H

#include <stdint.h>
#include <stddef.h>



typedef enum {
    HSER_SINK_OK,               /* data sunk into input buffer */
    HSER_SINK_ERROR_NULL=-1,    /* NULL argument */
    HSER_SINK_ERROR_MISUSE=-2,  /* API misuse */
} HSE_sink_res;

typedef enum {
    HSER_POLL_EMPTY,            /* input exhausted */
    HSER_POLL_MORE,             /* poll again for more output  */
    HSER_POLL_ERROR_NULL=-1,    /* NULL argument */
    HSER_POLL_ERROR_MISUSE=-2,  /* API misuse */
} HSE_poll_res;

typedef enum {
    HSER_FINISH_DONE,           /* encoding is complete */
    HSER_FINISH_MORE,           /* more output remaining; use poll */
    HSER_FINISH_ERROR_NULL=-1,  /* NULL argument */
} HSE_finish_res;

#if HEATSHRINK_DYNAMIC_ALLOC
#define HEATSHRINK_ENCODER_WINDOW_BITS(HSE) \
    ((HSE)->window_sz2)
#define HEATSHRINK_ENCODER_LOOKAHEAD_BITS(HSE) \
    ((HSE)->lookahead_sz2)
#define HEATSHRINK_ENCODER_INDEX(HSE) \
    ((HSE)->search_index)
struct hs_index {
    uint16_t size;
    int16_t index[];
};
#else
#define HEATSHRINK_ENCODER_WINDOW_BITS(_) \
    (HEATSHRINK_STATIC_WINDOW_BITS)
#define HEATSHRINK_ENCODER_LOOKAHEAD_BITS(_) \
    (HEATSHRINK_STATIC_LOOKAHEAD_BITS)
#define HEATSHRINK_ENCODER_INDEX(HSE) \
    (&(HSE)->search_index)
struct hs_index {
    uint16_t size;
    int16_t index[2 << HEATSHRINK_STATIC_WINDOW_BITS];
};
#endif

typedef struct {
    uint16_t input_size;        /* bytes in input buffer */
    uint16_t match_scan_index;
    uint16_t match_length;
    uint16_t match_pos;
    uint16_t outgoing_bits;     /* enqueued outgoing bits */
    uint8_t outgoing_bits_count;
    uint8_t flags;
    uint8_t state;              /* current state machine node */
    uint8_t current_byte;       /* current byte of output */
    uint8_t bit_index;          /* current bit index */
#if HEATSHRINK_DYNAMIC_ALLOC
    uint8_t window_sz2;         /* 2^n size of window */
    uint8_t lookahead_sz2;      /* 2^n size of lookahead */
#if HEATSHRINK_USE_INDEX
    struct hs_index *search_index;
#endif
    /* input buffer and / sliding window for expansion */
    uint8_t buffer[];
#else
    #if HEATSHRINK_USE_INDEX
        struct hs_index search_index;
    #endif
    /* input buffer and / sliding window for expansion */
    uint8_t buffer[2 << HEATSHRINK_ENCODER_WINDOW_BITS(_)];
#endif
} heatshrink_encoder;

#if HEATSHRINK_DYNAMIC_ALLOC
/* Allocate a new encoder struct and its buffers.
 * Returns NULL on error. */
heatshrink_encoder *heatshrink_encoder_alloc(uint8_t window_sz2,
    uint8_t lookahead_sz2);

/* Free an encoder. */
void heatshrink_encoder_free(heatshrink_encoder *hse);
#endif

/* Reset an encoder. */
void heatshrink_encoder_reset(heatshrink_encoder *hse);

/* Sink up to SIZE bytes from IN_BUF into the encoder.
 * INPUT_SIZE is set to the number of bytes actually sunk (in case a
 * buffer was filled.). */
HSE_sink_res heatshrink_encoder_sink(heatshrink_encoder *hse,
    uint8_t *in_buf, size_t size, size_t *input_size);

/* Poll for output from the encoder, copying at most OUT_BUF_SIZE bytes into
 * OUT_BUF (setting *OUTPUT_SIZE to the actual amount copied). */
HSE_poll_res heatshrink_encoder_poll(heatshrink_encoder *hse,
    uint8_t *out_buf, size_t out_buf_size, size_t *output_size);

/* Notify the encoder that the input stream is finished.
 * If the return value is HSER_FINISH_MORE, there is still more output, so
 * call heatshrink_encoder_poll and repeat. */
HSE_finish_res heatshrink_encoder_finish(heatshrink_encoder *hse);

#endif


/* ir_remote_compress.h */
/**
 * @file compress.h
 * LZSS based compression HAL API
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Compress Icon control structure */
typedef struct CompressIcon CompressIcon;

/** Initialize icon compressor
 *
 * @param[in]  decode_buf_size  The icon buffer size for decoding. Ensure that
 *                              it's big enough for any icons that you are
 *                              planning to decode with it.
 *
 * @return     Compress Icon instance
 */
CompressIcon* compress_icon_alloc(size_t decode_buf_size);

/** Free icon compressor
 *
 * @param      instance  The Compress Icon instance
 */
void compress_icon_free(CompressIcon* instance);

/** Decompress icon
 *
 * @warning    output pointer set by this function is valid till next
 *             `compress_icon_decode` or `compress_icon_free` call
 *
 * @param      instance   The Compress Icon instance
 * @param      icon_data  pointer to icon data.
 * @param[in]  output     pointer to decoded buffer pointer. Data in buffer is
 *                        valid till next call. If icon data was not compressed,
 *                        pointer within icon_data is returned
 */
void compress_icon_decode(CompressIcon* instance, const uint8_t* icon_data, uint8_t** output);

//////////////////////////////////////////////////////////////////////////

/** Compress control structure */
typedef struct Compress Compress;

/** Supported compression types */
typedef enum {
    CompressTypeHeatshrink = 0,
} CompressType;

/** Configuration for heatshrink compression */
typedef struct {
    uint16_t window_sz2;
    uint16_t lookahead_sz2;
    uint16_t input_buffer_sz;
} CompressConfigHeatshrink;

/** Default configuration for heatshrink compression. Used for image assets. */
extern const CompressConfigHeatshrink compress_config_heatshrink_default;

/** Allocate encoder and decoder
 *
 * @param      type     Compression type
 * @param[in]  config   Configuration for compression, specific to type
 *
 * @return     Compress instance
 */
Compress* compress_alloc(CompressType type, const void* config);

/** Free encoder and decoder
 *
 * @param      compress  Compress instance
 */
void compress_free(Compress* compress);

/** Encode data
 *
 * @param      compress       Compress instance
 * @param      data_in        pointer to input data
 * @param      data_in_size   size of input data
 * @param      data_out       maximum size of output data
 * @param[in]  data_out_size  The data out size
 * @param      data_res_size  pointer to result output data size
 *
 * @note       Prepends compressed stream with a header. If data is not compressible,
 *             it will be stored as is after the header.
 * @return     true on success
 */
bool compress_encode(
    Compress* compress,
    uint8_t* data_in,
    size_t data_in_size,
    uint8_t* data_out,
    size_t data_out_size,
    size_t* data_res_size);

/** Decode data
 *
 * @param      compress       Compress instance
 * @param      data_in        pointer to input data
 * @param      data_in_size   size of input data
 * @param      data_out       maximum size of output data
 * @param[in]  data_out_size  The data out size
 * @param      data_res_size  pointer to result output data size
 *
 * @note       Expects compressed stream with a header, as produced by `compress_encode`.
 * @return     true on success
 */
bool compress_decode(
    Compress* compress,
    uint8_t* data_in,
    size_t data_in_size,
    uint8_t* data_out,
    size_t data_out_size,
    size_t* data_res_size);

/** I/O callback for streamed compression/decompression
 *
 * @param context user context
 * @param buffer buffer to read/write
 * @param size size of buffer
 *
 * @return number of bytes read/written, 0 on end of stream, negative on error
 */
typedef int32_t (*CompressIoCallback)(void* context, uint8_t* buffer, size_t size);

/** Decompress streamed data
 *
 * @param      compress       Compress instance
 * @param      read_cb        read callback
 * @param      read_context   read callback context
 * @param      write_cb       write callback
 * @param      write_context  write callback context
 *
 * @note       Does not expect a header, just compressed data stream.
 * @return     true on success
 */
bool compress_decode_streamed(
    Compress* compress,
    CompressIoCallback read_cb,
    void* read_context,
    CompressIoCallback write_cb,
    void* write_context);

//////////////////////////////////////////////////////////////////////////

/** CompressStreamDecoder control structure */
typedef struct CompressStreamDecoder CompressStreamDecoder;

/** Allocate stream decoder
 *
 * @param      type          Compression type
 * @param[in]  config        Configuration for compression, specific to type
 * @param      read_cb       The read callback for input (compressed) data
 * @param      read_context  The read context
 *
 * @return     CompressStreamDecoder instance
 */
CompressStreamDecoder* compress_stream_decoder_alloc(
    CompressType type,
    const void* config,
    CompressIoCallback read_cb,
    void* read_context);

/** Free stream decoder
 *
 * @param      instance  The CompressStreamDecoder instance
 */
void compress_stream_decoder_free(CompressStreamDecoder* instance);

/** Read uncompressed data chunk from stream decoder
 *
 * @param      instance       The CompressStreamDecoder instance
 * @param      data_out       The data out
 * @param[in]  data_out_size  The data out size
 *
 * @return     true on success
 */
bool compress_stream_decoder_read(
    CompressStreamDecoder* instance,
    uint8_t* data_out,
    size_t data_out_size);

/** Seek to position in uncompressed data stream
 *
 * @param      instance   The CompressStreamDecoder instance
 * @param[in]  position   The position
 *
 * @return     true on success
 * @warning    Backward seeking is not supported
 */
bool compress_stream_decoder_seek(CompressStreamDecoder* instance, size_t position);

/** Get current position in uncompressed data stream
 *
 * @param      instance  The CompressStreamDecoder instance
 *
 * @return     current position
 */
size_t compress_stream_decoder_tell(CompressStreamDecoder* instance);

/** Reset stream decoder to the beginning
 * @warning    Read callback must be repositioned by caller separately
 *
 * @param      instance  The CompressStreamDecoder instance
 *
 * @return     true on success
 */
bool compress_stream_decoder_rewind(CompressStreamDecoder* instance);

#ifdef __cplusplus
}
#endif


/* ir_remote_assets.h */
#pragma once



extern const Icon A_Levelup1_128x64;
extern const Icon A_Levelup2_128x64;
extern const Icon I_125_10px;
extern const Icon I_Apps_10px;
extern const Icon I_Nfc_10px;
extern const Icon I_back_10px;
extern const Icon I_badusb_10px;
extern const Icon I_dir_10px;
extern const Icon I_file_10px;
extern const Icon I_ibutt_10px;
extern const Icon I_ir_10px;
extern const Icon I_js_script_10px;
extern const Icon I_keyboard_10px;
extern const Icon I_loading_10px;
extern const Icon I_music_10px;
extern const Icon I_settings_10px;
extern const Icon I_sub1_10px;
extern const Icon I_subrem_10px;
extern const Icon I_u2f_10px;
extern const Icon I_unknown_10px;
extern const Icon I_update_10px;
extern const Icon I_BLE_Pairing_128x64;
extern const Icon I_Ble_connected_15x15;
extern const Icon I_Ble_disconnected_15x15;
extern const Icon I_Button_18x18;
extern const Icon I_Circles_47x47;
extern const Icon I_Left_mouse_icon_9x9;
extern const Icon I_Ok_btn_9x9;
extern const Icon I_Ok_btn_pressed_13x13;
extern const Icon I_Pressed_Button_13x13;
extern const Icon I_Right_mouse_icon_9x9;
extern const Icon I_Space_65x18;
extern const Icon I_Voldwn_6x6;
extern const Icon I_Volup_8x6;
extern const Icon I_Bad_BLE_48x22;
extern const Icon I_Clock_18x18;
extern const Icon I_Error_18x18;
extern const Icon I_EviSmile1_18x21;
extern const Icon I_EviSmile2_18x21;
extern const Icon I_EviWaiting1_18x21;
extern const Icon I_EviWaiting2_18x21;
extern const Icon I_Percent_10x14;
extern const Icon I_Smile_18x18;
extern const Icon I_UsbTree_48x22;
extern const Icon I_ActiveConnection_50x64;
extern const Icon I_ButtonCenter_7x7;
extern const Icon I_ButtonDown_7x4;
extern const Icon I_ButtonLeftSmall_3x5;
extern const Icon I_ButtonLeft_4x7;
extern const Icon I_ButtonRightSmall_3x5;
extern const Icon I_ButtonRight_4x7;
extern const Icon I_ButtonUp_7x4;
extern const Icon I_DFU_128x50;
extern const Icon I_Hashmark_7x7;
extern const Icon I_More_data_placeholder_5x7;
extern const Icon I_Warning_30x23;
extern const Icon I_arrow_nano_down;
extern const Icon I_arrow_nano_up;
extern const Icon A_Loading_24;
extern const Icon A_Round_loader_8x8;
extern const Icon I_DolphinDone_80x58;
extern const Icon I_DolphinMafia_119x62;
extern const Icon I_DolphinReadingSuccess_59x63;
extern const Icon I_DolphinSaved_92x58;
extern const Icon I_DolphinSuccess_91x55;
extern const Icon I_DolphinWait_59x54;
extern const Icon I_WarningDolphinFlip_45x42;
extern const Icon I_WarningDolphin_45x42;
extern const Icon I_Erase_pin_128x64;
extern const Icon I_ArrowUpEmpty_14x15;
extern const Icon I_ArrowUpFilled_14x15;
extern const Icon I_InfraredArrowDown_4x8;
extern const Icon I_InfraredArrowUp_4x8;
extern const Icon I_InfraredLearnShort_128x31;
extern const Icon I_blue_19x20;
extern const Icon I_blue_hover_19x20;
extern const Icon I_brightness_text_40x5;
extern const Icon I_celsius_24x23;
extern const Icon I_celsius_hover_24x23;
extern const Icon I_ch_down_24x21;
extern const Icon I_ch_down_hover_24x21;
extern const Icon I_ch_text_31x34;
extern const Icon I_ch_up_24x21;
extern const Icon I_ch_up_hover_24x21;
extern const Icon I_color_text_24x5;
extern const Icon I_cool_30x51;
extern const Icon I_dry_19x20;
extern const Icon I_dry_hover_19x20;
extern const Icon I_dry_text_15x5;
extern const Icon I_fahren_24x23;
extern const Icon I_fahren_hover_24x23;
extern const Icon I_green_19x20;
extern const Icon I_green_hover_19x20;
extern const Icon I_heat_30x51;
extern const Icon I_hourglass0_24x24;
extern const Icon I_hourglass1_24x24;
extern const Icon I_hourglass2_24x24;
extern const Icon I_hourglass3_24x24;
extern const Icon I_hourglass4_24x24;
extern const Icon I_hourglass5_24x24;
extern const Icon I_hourglass6_24x24;
extern const Icon I_max_24x23;
extern const Icon I_max_hover_24x23;
extern const Icon I_minus_19x20;
extern const Icon I_minus_hover_19x20;
extern const Icon I_mode_19x20;
extern const Icon I_mode_hover_19x20;
extern const Icon I_mode_text_20x5;
extern const Icon I_mute_19x20;
extern const Icon I_mute_hover_19x20;
extern const Icon I_mute_text_19x5;
extern const Icon I_next_19x20;
extern const Icon I_next_hover_19x20;
extern const Icon I_next_text_19x6;
extern const Icon I_off_19x20;
extern const Icon I_off_hover_19x20;
extern const Icon I_off_text_12x5;
extern const Icon I_on_text_9x5;
extern const Icon I_pause_19x20;
extern const Icon I_pause_hover_19x20;
extern const Icon I_pause_text_23x5;
extern const Icon I_play_19x20;
extern const Icon I_play_hover_19x20;
extern const Icon I_play_text_19x5;
extern const Icon I_plus_19x20;
extern const Icon I_plus_hover_19x20;
extern const Icon I_power_19x20;
extern const Icon I_power_hover_19x20;
extern const Icon I_power_text_24x5;
extern const Icon I_prev_19x20;
extern const Icon I_prev_hover_19x20;
extern const Icon I_prev_text_19x5;
extern const Icon I_red_19x20;
extern const Icon I_red_hover_19x20;
extern const Icon I_rotate_19x20;
extern const Icon I_rotate_hover_19x20;
extern const Icon I_rotate_text_24x5;
extern const Icon I_speed_text_30x30;
extern const Icon I_timer_19x20;
extern const Icon I_timer_hover_19x20;
extern const Icon I_timer_text_23x5;
extern const Icon I_vol_ac_text_30x30;
extern const Icon I_vol_tv_text_29x34;
extern const Icon I_voldown_24x21;
extern const Icon I_voldown_hover_24x21;
extern const Icon I_volup_24x21;
extern const Icon I_volup_hover_24x21;
extern const Icon I_white_19x20;
extern const Icon I_white_hover_19x20;
extern const Icon I_DoorLeft_70x55;
extern const Icon I_DoorRight_70x55;
extern const Icon I_SmallArrowDown_3x5;
extern const Icon I_SmallArrowDown_4x7;
extern const Icon I_SmallArrowUp_3x5;
extern const Icon I_SmallArrowUp_4x7;
extern const Icon I_KeyBackspaceSelected_16x9;
extern const Icon I_KeyBackspace_16x9;
extern const Icon I_KeyKeyboardSelected_10x11;
extern const Icon I_KeyKeyboard_10x11;
extern const Icon I_KeySaveBlockedSelected_24x11;
extern const Icon I_KeySaveBlocked_24x11;
extern const Icon I_KeySaveSelected_24x11;
extern const Icon I_KeySave_24x11;
extern const Icon I_KeySignSelected_21x11;
extern const Icon I_KeySign_21x11;
extern const Icon I_err_01;
extern const Icon I_err_02;
extern const Icon I_err_03;
extern const Icon I_err_04;
extern const Icon I_err_05;
extern const Icon A_125khz_14;
extern const Icon A_BadUsb_14;
extern const Icon A_Clock_14;
extern const Icon A_Debug_14;
extern const Icon A_FileManager_14;
extern const Icon A_GPIO_14;
extern const Icon A_Infrared_14;
extern const Icon A_NFC_14;
extern const Icon A_Plugins_14;
extern const Icon A_Settings_14;
extern const Icon A_Sub1ghz_14;
extern const Icon A_SubGHzRemote_14;
extern const Icon A_U2F_14;
extern const Icon A_iButton_14;
extern const Icon I_ArrowC_1_36x36;
extern const Icon I_Detailed_chip_17x13;
extern const Icon I_Keychain_39x36;
extern const Icon I_MFKey_qr_25x25;
extern const Icon I_Medium_chip_22x21;
extern const Icon I_Modern_reader_18x34;
extern const Icon I_Move_flipper_26x39;
extern const Icon I_NFC_dolphin_emulation_51x64;
extern const Icon I_NFC_manual_60x50;
extern const Icon I_NFC_manual_chameleon_60x50;
extern const Icon I_Release_arrow_18x15;
extern const Icon I_check_big_20x17;
extern const Icon I_Pin_arrow_up_7x9;
extern const Icon I_Pin_attention_dpad_29x29;
extern const Icon I_Pin_back_arrow_10x8;
extern const Icon I_Pin_cell_13x13;
extern const Icon I_Pin_pointer_5x3;
extern const Icon I_Pin_star_7x7;
extern const Icon I_passport_bad1_46x49;
extern const Icon I_passport_bad2_46x49;
extern const Icon I_passport_bad3_46x49;
extern const Icon I_passport_bottom_128x18;
extern const Icon I_passport_happy1_46x49;
extern const Icon I_passport_happy2_46x49;
extern const Icon I_passport_happy3_46x49;
extern const Icon I_passport_left_6x46;
extern const Icon I_passport_okay1_46x49;
extern const Icon I_passport_okay2_46x49;
extern const Icon I_passport_okay3_46x49;
extern const Icon I_BatteryBody_52x28;
extern const Icon I_Battery_16x16;
extern const Icon I_FaceCharging_29x14;
extern const Icon I_FaceConfused_29x14;
extern const Icon I_FaceNopower_29x14;
extern const Icon I_FaceNormal_29x14;
extern const Icon I_Health_16x16;
extern const Icon I_Temperature_16x16;
extern const Icon I_Unplug_bg_bottom_128x10;
extern const Icon I_Unplug_bg_top_128x14;
extern const Icon I_Voltage_16x16;
extern const Icon I_RFIDDolphinReceive_97x61;
extern const Icon I_RFIDDolphinSend_97x61;
extern const Icon I_RFIDSmallChip_14x14;
extern const Icon I_SDQuestion_35x43;
extern const Icon I_LoadingHourglass_24x24;
extern const Icon I_dolph_cry_49x54;
extern const Icon I_qr_benchmark_25x25;
extern const Icon A_Alarm_47x39;
extern const Icon I_Alert_9x8;
extern const Icon I_Attention_5x8;
extern const Icon I_BLE_beacon_7x8;
extern const Icon I_Background_128x11;
extern const Icon I_Battery_26x8;
extern const Icon I_Bluetooth_Connected_16x8;
extern const Icon I_Bluetooth_Idle_5x8;
extern const Icon I_Charging_lightning_9x10;
extern const Icon I_Charging_lightning_mask_9x10;
extern const Icon I_Exp_module_connected_12x8;
extern const Icon I_GameMode_11x8;
extern const Icon I_Hidden_window_9x8;
extern const Icon I_Muted_8x8;
extern const Icon I_Rpc_active_7x8;
extern const Icon I_SDcardFail_11x8;
extern const Icon I_SDcardMounted_11x8;
extern const Icon I_Cos_9x7;
extern const Icon I_Dynamic_9x7;
extern const Icon I_Fishing_123x52;
extern const Icon I_Lock_7x8;
extern const Icon I_MHz_25x11;
extern const Icon I_Quest_7x8;
extern const Icon I_Raw_9x7;
extern const Icon I_Scanning_123x52;
extern const Icon I_Static_9x7;
extern const Icon I_Unlock_7x8;
extern const Icon I_Auth_62x31;
extern const Icon I_Connect_me_62x31;
extern const Icon I_Connected_62x31;
extern const Icon I_Drive_112x35;
extern const Icon I_Error_62x31;
extern const Icon I_Updating_32x40;
extern const Icon I_iButtonDolphinVerySuccess_92x55;
extern const Icon I_iButtonKey_49x44;
extern const Icon I_Message_8x7;
extern const Icon I_pocsag_pager_10px;
extern const Icon I_tetris_10px;
extern const Icon I_metronome_icon;
extern const Icon I_tone_gen;
extern const Icon A_play_button;
extern const Icon A_settings_button;
extern const Icon I_menu;
extern const Icon I_playback;
extern const Icon I_settings;


