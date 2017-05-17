/*******************************************************************************
* DISCLAIMER
* This software is supplied by Renesas Electronics Corporation and is only
* intended for use with Renesas products. No other uses are authorized. This
* software is owned by Renesas Electronics Corporation and is protected under
* all applicable laws, including copyright laws.
* THIS SOFTWARE IS PROVIDED "AS IS" AND RENESAS MAKES NO WARRANTIES REGARDING
* THIS SOFTWARE, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING BUT NOT
* LIMITED TO WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE
* AND NON-INFRINGEMENT. ALL SUCH WARRANTIES ARE EXPRESSLY DISCLAIMED.
* TO THE MAXIMUM EXTENT PERMITTED NOT PROHIBITED BY LAW, NEITHER RENESAS
* ELECTRONICS CORPORATION NOR ANY OF ITS AFFILIATED COMPANIES SHALL BE LIABLE
* FOR ANY DIRECT, INDIRECT, SPECIAL, INCIDENTAL OR CONSEQUENTIAL DAMAGES FOR
* ANY REASON RELATED TO THIS SOFTWARE, EVEN IF RENESAS OR ITS AFFILIATES HAVE
* BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.
* Renesas reserves the right, without notice, to make changes to this software
* and to discontinue the availability of this software. By using this software,
* you agree to the additional terms and conditions found by accessing the
* following link:
* http://www.renesas.com/disclaimer*
* Copyright (C) 2015 Renesas Electronics Corporation. All rights reserved.
*******************************************************************************/

#include "decode.h"
#include "misratypes.h"
#include "dec_flac.h"

static FLAC__StreamDecoderReadStatus read_cb(const FLAC__StreamDecoder *decoder, 
                            FLAC__byte buffer[], size_t *bytes, void *client_data);
static FLAC__StreamDecoderWriteStatus write_cb (const FLAC__StreamDecoder *decoder,
    const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data);
static void meta_cb(const FLAC__StreamDecoder *decoder, 
                            const FLAC__StreamMetadata *metadata, void *client_data);
static void error_cb(const FLAC__StreamDecoder *decoder, 
                            FLAC__StreamDecoderErrorStatus status, void *client_data);
static void init_ctrl_data(flac_ctrl_t * const p_ctrl);
static bool check_file_spec(const flac_ctrl_t * const p_ctrl);
static bool check_end_of_stream(const flac_ctrl_t * const p_flac_ctrl);

bool flac_set_pcm_buf(flac_ctrl_t * const p_flac_ctrl, 
                        int32_t * const p_buf_addr, const uint32_t buf_num)
{
    bool        ret = false;
    if ((p_flac_ctrl != NULL) && (p_buf_addr != NULL) && (buf_num > 0u)) {
        p_flac_ctrl->p_pcm_buf = p_buf_addr;
        p_flac_ctrl->pcm_buf_num = buf_num;
        p_flac_ctrl->pcm_buf_used_cnt = 0u;
        ret = true;
    }
    return ret;
}

uint32_t flac_get_pcm_cnt(const flac_ctrl_t * const p_flac_ctrl)
{
    uint32_t    ret = 0u;
    if (p_flac_ctrl != NULL) {
        ret = p_flac_ctrl->pcm_buf_used_cnt;
    }
    return ret;
}

uint32_t flac_get_play_time(const flac_ctrl_t * const p_flac_ctrl)
{
    uint32_t    play_time = 0u;
    if (p_flac_ctrl != NULL) {
        if (p_flac_ctrl->sample_rate > 0u) {    /* Prevents division by 0 */
            play_time = (uint32_t)(p_flac_ctrl->decoded_sample / p_flac_ctrl->sample_rate);
        }
    }
    return play_time;
}

uint32_t flac_get_total_time(const flac_ctrl_t * const p_flac_ctrl)
{
    uint32_t    total_time = 0u;
    if (p_flac_ctrl != NULL) {
        if (p_flac_ctrl->sample_rate > 0u) {    /* Prevents division by 0 */
            total_time = (uint32_t)(p_flac_ctrl->total_sample / p_flac_ctrl->sample_rate);
        }
    }
    return total_time;
}

bool flac_open(FILE * const p_handle, flac_ctrl_t * const p_flac_ctrl)
{
    bool                            ret = false;
    FLAC__bool                      result;
    FLAC__StreamDecoderInitStatus   result_init;
    FLAC__StreamDecoder             *p_dec;
    
    if ((p_handle != NULL) && (p_flac_ctrl != NULL)) {
        /* Initialises Internal memory */
        init_ctrl_data(p_flac_ctrl);
        p_flac_ctrl->p_file_handle = p_handle;
        /* Creates the instance of flac decoder. */
        p_dec = FLAC__stream_decoder_new();
        if (p_dec != NULL) {
            /* Sets the MD5 check. */
            (void) FLAC__stream_decoder_set_md5_checking(p_dec, true);
            /* Initialises the instance of flac decoder. */
            result_init = FLAC__stream_decoder_init_stream(p_dec, &read_cb, NULL, NULL, 
                            NULL, NULL, &write_cb, &meta_cb, &error_cb, (void *)p_flac_ctrl);
            if (result_init == FLAC__STREAM_DECODER_INIT_STATUS_OK) {
                /* Decodes until end of metadata. */
                result = FLAC__stream_decoder_process_until_end_of_metadata(p_dec);
                if (result == true) {
                    if (check_file_spec(p_flac_ctrl) == true) {
                        p_flac_ctrl->p_decoder = p_dec;
                        ret = true;
                    }
                }
            }
            if (ret != true) {
                FLAC__stream_decoder_delete(p_dec);
            }
        }
    }
    return ret;
}

bool flac_decode(const flac_ctrl_t * const p_flac_ctrl)
{
    bool            ret = false;
    bool            eos;
    FLAC__bool      result;
    uint32_t        used_cnt;

    if (p_flac_ctrl != NULL) {
        eos = check_end_of_stream(p_flac_ctrl);
        if (eos != true) {
            /* Decoding position is not end of stream. */
            used_cnt = p_flac_ctrl->pcm_buf_used_cnt;
            result = FLAC__stream_decoder_process_single (p_flac_ctrl->p_decoder);
            if (result == true) {
                /* Did a decoded data increase? */
                if (p_flac_ctrl->pcm_buf_used_cnt > used_cnt) {
                    /* FLAC decoder process succeeded. */
                    ret = true;
                }
            }
        }
    }
    return ret;
}

void flac_close(flac_ctrl_t * const p_flac_ctrl)
{
    if (p_flac_ctrl != NULL) {
        FLAC__stream_decoder_delete(p_flac_ctrl->p_decoder);
        p_flac_ctrl->p_decoder = NULL;
    }
}

/** Read callback function of FLAC decoder library
 *
 *  @param decoder Decoder instance.
 *  @param buffer Pointer to the buffer to store the data of FLAC file.
 *  @param bytes Pointer to the size of the buffer.
 *  @param client_data Pointer to the control data of FLAC module.
 *
 *  @returns 
 *    Results of process. Returns the following status.
 *    FLAC__STREAM_DECODER_READ_STATUS_CONTINUE
 *    FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM
 *    FLAC__STREAM_DECODER_READ_STATUS_ABORT
 */
static FLAC__StreamDecoderReadStatus read_cb(const FLAC__StreamDecoder *decoder, 
                            FLAC__byte buffer[], size_t *bytes, void *client_data)
{
    FLAC__StreamDecoderReadStatus   ret = FLAC__STREAM_DECODER_READ_STATUS_ABORT;
    flac_ctrl_t                     * const p_ctrl = (flac_ctrl_t*)client_data;
    size_t                          read_size;

    UNUSED_ARG(decoder);
    if ((buffer != NULL) && (bytes != NULL) && (p_ctrl != NULL)) {
        if (*bytes > 0u) {
            read_size = fread(&buffer[0], sizeof(FLAC__byte), *bytes, p_ctrl->p_file_handle);
            if (read_size > 0u) {
                ret = FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
                *bytes = read_size;
            } else {
                ret = FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
                *bytes = 0;
            }
        }
    }
    return ret;
}

/** Write callback function of FLAC decoder library
 *
 *  @param decoder Decoder instance.
 *  @param frame The description of the decoded frame.
 *  @param buffer Pointer to the decoded data.
 *  @param client_data Pointer to the control data of FLAC module.
 *
 *  @returns 
 *    Results of process. Returns the following status.
 *    FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE
 *    FLAC__STREAM_DECODER_WRITE_STATUS_ABORT
 */
static FLAC__StreamDecoderWriteStatus write_cb (const FLAC__StreamDecoder *decoder,
        const FLAC__Frame *frame, const FLAC__int32 *const buffer[], void *client_data)
{
    FLAC__StreamDecoderWriteStatus  ret = FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
    const FLAC__int32               *p_ch0;
    const FLAC__int32               *p_ch1;
    flac_ctrl_t                     *const p_ctrl = (flac_ctrl_t*)client_data;
    uint32_t                        bit_shift_num;
    uint32_t                        i;

    UNUSED_ARG(decoder);
    if ((frame != NULL) && (buffer != NULL) && (p_ctrl != NULL)) {
        if (frame->header.number_type == FLAC__FRAME_NUMBER_TYPE_SAMPLE_NUMBER) {
            p_ctrl->decoded_sample = frame->header.number.sample_number + frame->header.blocksize;
        } else {
            p_ctrl->decoded_sample += frame->header.blocksize;
        }
        if (p_ctrl->p_pcm_buf == NULL) {
            /* Error */
        } else if (frame->header.blocksize > DEC_MAX_BLOCK_SIZE) {
            /* Error : Block size is illegal specification */
        } else {
            bit_shift_num = (DEC_OUTPUT_BITS_PER_SAMPLE + DEC_OUTPUT_PADDING_BITS)- p_ctrl->bits_per_sample;
            if (p_ctrl->channel_num == DEC_MAX_CHANNEL_NUM) {
                /* stereo */
                p_ch0 = buffer[0];
                p_ch1 = buffer[1];
            } else {
                /* mono */
                p_ch0 = buffer[0];
                p_ch1 = buffer[0];
            }
            if ((p_ctrl->pcm_buf_num - p_ctrl->pcm_buf_used_cnt) >= (frame->header.blocksize * DEC_MAX_CHANNEL_NUM)) {
                for (i = 0; i < frame->header.blocksize; i++) {
                    /* ch 0 */
                    p_ctrl->p_pcm_buf[p_ctrl->pcm_buf_used_cnt] = (int32_t)((uint32_t)p_ch0[i] << bit_shift_num);
                    p_ctrl->pcm_buf_used_cnt++;
                    /* ch 1 */
                    p_ctrl->p_pcm_buf[p_ctrl->pcm_buf_used_cnt] = (int32_t)((uint32_t)p_ch1[i] << bit_shift_num);
                    p_ctrl->pcm_buf_used_cnt++;
                }
                ret = FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
            }
        }
    }
    return ret;
}

/** Metadata callback function of FLAC decoder library
 *
 *  @param decoder Decoder instance.
 *  @param metadata Block of the decoded metadata.
 *  @param client_data Pointer to the control data of FLAC module.
 */
static void meta_cb(const FLAC__StreamDecoder *decoder, 
            const FLAC__StreamMetadata *metadata, void *client_data)
{
    flac_ctrl_t     *const p_ctrl = (flac_ctrl_t*)client_data;

    UNUSED_ARG(decoder);
    if ((metadata != NULL) && (p_ctrl != NULL)) {
        if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO) {
            p_ctrl->sample_rate = metadata->data.stream_info.sample_rate;
            p_ctrl->channel_num = metadata->data.stream_info.channels;
            p_ctrl->bits_per_sample = metadata->data.stream_info.bits_per_sample;
            p_ctrl->total_sample = metadata->data.stream_info.total_samples;
        }
    }
}

/** Error callback function of FLAC decoder library
 *
 *  @param decoder Decoder instance.
 *  @param client_data Pointer to the control data of FLAC module.
 */
static void error_cb(const FLAC__StreamDecoder *decoder, 
            FLAC__StreamDecoderErrorStatus status, void *client_data)
{
    /* DO NOTHING */
    UNUSED_ARG(decoder);
    UNUSED_ARG(status);
    UNUSED_ARG(client_data);
}

/** Initialises the control data of FLAC module
 *
 *  @param p_ctrl Pointer to the control data of FLAC module.
 */
static void init_ctrl_data(flac_ctrl_t * const p_ctrl)
{
    if (p_ctrl != NULL) {
        p_ctrl->p_decoder        = NULL;    /* Handle of flac decoder */
        p_ctrl->p_file_handle    = NULL;    /* Handle of flac file */
        p_ctrl->decoded_sample   = 0uLL;    /* Number of a decoded sample */
        p_ctrl->total_sample     = 0uLL;    /* Total number of sample */
        p_ctrl->sample_rate      = 0u;      /* Sample rate in Hz */
        p_ctrl->channel_num      = 0u;      /* Number of channels */
        p_ctrl->bits_per_sample  = 0u;      /* bit per sample */
        p_ctrl->p_pcm_buf        = NULL;    /* Pointer of PCM buffer */
        p_ctrl->pcm_buf_num      = 0u;      /* Number of elements in PCM buffer */
        p_ctrl->pcm_buf_used_cnt = 0u;      /* Counter of used elements in PCM buffer */
    }
}

/** Checks the playable file of the playback
 *
 *  @param client_data Pointer to the control data of FLAC module.
 */
static bool check_file_spec(const flac_ctrl_t * const p_ctrl)
{
    bool    ret = false;

    if (p_ctrl == NULL) {
        /* Error : NULL pointer */
    } else if ((p_ctrl->channel_num <= 0u) || 
               (p_ctrl->channel_num > DEC_MAX_CHANNEL_NUM)) {
        /* Error : Channel number is illegal specification */
    } else if ((p_ctrl->bits_per_sample != DEC_16BITS_PER_SAMPLE) && 
               (p_ctrl->bits_per_sample != DEC_24BITS_PER_SAMPLE)) {
        /* Error : Bit per sample is illegal specification */
    } else if ((p_ctrl->sample_rate < DEC_INPUT_MIN_SAMPLE_RATE) || 
               (p_ctrl->sample_rate > DEC_INPUT_MAX_SAMPLE_RATE)) {
        /* Error : Sample rate is illegal specification */
    } else {
        /* OK */
        ret = true;
    }
    return ret;
}

/** Checks the state of the FLAC decoder
 *
 *  @param p_flac_ctrl Pointer to the control data of FLAC module.
 *
 *  @returns 
 *    The state of FLAC decoder. true is end of stream. false is other state.
 */
static bool check_end_of_stream(const flac_ctrl_t * const p_flac_ctrl)
{
    bool                        ret = false;
    FLAC__StreamDecoderState    state;

    if (p_flac_ctrl != NULL) {
        state = FLAC__stream_decoder_get_state(p_flac_ctrl->p_decoder);
        if (state == FLAC__STREAM_DECODER_END_OF_STREAM) {
            ret = true;
        }
    }
    return ret;
}
