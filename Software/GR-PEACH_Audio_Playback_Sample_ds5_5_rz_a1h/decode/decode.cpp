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

#include "mbed.h"
#include "rtos.h"
#include "misratypes.h"
#include "r_errno.h"
#include "system.h"
#include "decode.h"
#include "audio_out.h"
#include "dec_flac.h"

/*--- Macro definition of mbed-rtos mail ---*/
#define MAIL_QUEUE_SIZE     (12)    /* Queue size */
#define MAIL_PARAM_NUM      (2)     /* Elements number of mail parameter array */

/* dec_mail_t */
#define MAIL_PARAM0         (0)     /* Index number of mail parameter array */
#define MAIL_PARAM1         (1)     /* Index number of mail parameter array */

#define MAIL_PARAM_NON      (0u)    /* Value of unused element of mail parameter array */

/* mail_id = DEC_MAILID_OPEN */
#define MAIL_OPEN_CB        (MAIL_PARAM0)   /* Callback function */
#define MAIL_OPEN_FILE      (MAIL_PARAM1)   /* File handle */

/* mail_id = DEC_MAILID_PLAY : No parameter */

/* mail_id = DEC_MAILID_PAUSE_ON : No parameter */

/* mail_id = DEC_MAILID_PAUSE_OFF : No parameter */

/* mail_id = DEC_MAILID_STOP : No parameter */

/* mail_id = DEC_MAILID_CLOSE */
#define MAIL_CLOSE_CB               (MAIL_PARAM0)   /* Callback function */

/* mail_id = DEC_MAILID_CB_AUD_DATA_OUT */
#define MAIL_DATA_OUT_RESULT        (MAIL_PARAM0)   /* Result of the process */

/* mail_id = DEC_MAILID_SCUX_WRITE_FIN */
#define MAIL_SCUX_WRITE_RESULT      (MAIL_PARAM0)   /* Result of the process */
#define MAIL_SCUX_WRITE_BUF_INDEX   (MAIL_PARAM1)   /* Index number of PCM buffer */

/* mail_id = DEC_MAILID_SCUX_FLUSH_FIN */
#define MAIL_SCUX_FLUSH_RESULT      (MAIL_PARAM0)   /* Result of the process */


/*--- Macro definition of PCM buffer ---*/
#define UNIT_TIME_MS                (50u)   /* Unit time of PCM data processing (ms) */
#define SEC_TO_MSEC                 (1000u)

/* Sample number per uint time (ms) */
#define SAMPLE_NUM_PER_UNIT_MS      (((UNIT_TIME_MS * DEC_INPUT_MAX_SAMPLE_RATE) / SEC_TO_MSEC) * DEC_OUTPUT_CHANNEL_NUM)
/* Max number of samples per 1 block */
#define MAX_SAMPLE_PER_1BLOCK       (DEC_MAX_BLOCK_SIZE * DEC_OUTPUT_CHANNEL_NUM)

#define PCM_BUF_NUM                 (3u)
#define TOTAL_SAMPLE_NUM            (MAX_SAMPLE_PER_1BLOCK + SAMPLE_NUM_PER_UNIT_MS)

#define PCM_BUF_SINGLE              (1)
#define PCM_BUF_TOP_ID              (0)

/*--- Macro definition of R_BSP_Scux ---*/
#define SCUX_INT_LEVEL              (0x80)
#define SCUX_READ_NUM               (DEC_SCUX_READ_NUM)
#define SCUX_WRITE_NUM              (PCM_BUF_NUM)

/* 4 bytes aligned. No cache memory. */
#if defined(__ICCARM__)
#define NC_BSS_SECT                 @ ".mirrorram"
#else
#define NC_BSS_SECT                 __attribute__((section("NC_BSS"),aligned(4)))
#endif

/*--- User defined types of mbed-rtos mail ---*/
typedef enum {
    DEC_MAILID_DUMMY = 0,
    DEC_MAILID_OPEN,            /* Requests the opening of the decoder. */
    DEC_MAILID_PLAY,            /* Requests the starting of the playback. */
    DEC_MAILID_PAUSE_ON,        /* Requests the starting of the pause. */
    DEC_MAILID_PAUSE_OFF,       /* Requests the stopping of the pause. */
    DEC_MAILID_STOP,            /* Requests the stopping of the playback. */
    DEC_MAILID_CLOSE,           /* Requests the closing of the decoder. */
    DEC_MAILID_CB_AUD_DATA_OUT, /* Finished the preparation for the audio output. */
    DEC_MAILID_SCUX_WRITE_FIN,  /* Finished the writing process of SCUX. */
    DEC_MAILID_SCUX_FLUSH_FIN,  /* Finished the flush process of SCUX. */
    DEC_MAILID_NUM
} DEC_MAIL_ID;

typedef struct {
    DEC_MAIL_ID     mail_id;
    uint32_t        param[MAIL_PARAM_NUM];
} dec_mail_t;

/*--- User defined types of decode thread ---*/
/* The playback information of the playback file */
typedef struct {
    SYS_PlayStat    play_stat;  /* Playback status */
    uint32_t        play_time;  /* Playback start time */
    uint32_t        total_time; /* Total playback time */
} play_info_t;

/* Control data of Decode thread */
typedef struct {
    play_info_t     play_info;
    flac_ctrl_t     flac_ctrl;
} dec_ctrl_t;

/* Status of Decode thread */
typedef enum {
    DEC_ST_IDLE = 0,            /* Idle */
    DEC_ST_META_FIN,            /* Finished the decoding until a metadata */
    DEC_ST_PLAY,                /* Decoder start */
    DEC_ST_PAUSE,               /* Decoder pause */
    DEC_ST_STOP_PREPARE,        /* Preparing of decoder stop */
    DEC_ST_STOP,                /* Decoder stop */
    DEC_ST_NUM
} DEC_STATE;

static Mail<dec_mail_t, MAIL_QUEUE_SIZE> mail_box;
static R_BSP_Scux scux(SCUX_CH_0, SCUX_INT_LEVEL, SCUX_WRITE_NUM, SCUX_READ_NUM);

static bool open_proc(flac_ctrl_t * const p_ctrl, 
        FILE * const p_handle, const DEC_CbOpen p_cb);
static void close_proc(flac_ctrl_t * const p_ctrl, const DEC_CbClose p_cb);
static bool play_proc(flac_ctrl_t * const p_ctrl, const uint32_t buf_id,
        int32_t (* const p_buf)[TOTAL_SAMPLE_NUM], const uint32_t element_num);
static bool pause_proc(const uint32_t buf_id,
        int32_t (* const p_buf)[TOTAL_SAMPLE_NUM], const uint32_t element_num);
static uint32_t get_audio_data(flac_ctrl_t * const p_ctrl, 
                                int32_t * const p_buf, const uint32_t buf_num);
static void data_out_callback(const bool result);
static void write_callback(void * p_data, int32_t result, void * p_app_data);
static void flush_callback(int32_t result);
static bool send_mail(const DEC_MAIL_ID mail_id, 
                            const uint32_t param0, const uint32_t param1);
static bool recv_mail(DEC_MAIL_ID * const p_mail_id, 
                        uint32_t * const p_param0, uint32_t * const p_param1);
static void update_decode_stat(const SYS_PlayStat stat, play_info_t * const p_play_info);
static void update_decode_playtime(const uint32_t play_time, play_info_t * const p_play_info);
static void init_decode_playinfo(const uint32_t total_time, play_info_t * const p_play_info);
static void notify_decode_stat(const play_info_t * const p_play_info);

void dec_thread(void const *argument)
{
    dec_ctrl_t                  dec_ctrl;   /* Control data of Decode thread */
    DEC_STATE                   dec_stat;   /* Status of Decode thread */
    DEC_MAIL_ID                 mail_type;
    uint32_t                    mail_param[MAIL_PARAM_NUM];
    uint32_t                    buf_id;
    uint32_t                    buf_num;
    uint32_t                    time_code;
    bool                        result;
#if defined(__ICCARM__)
    static int32_t pcm_buf[PCM_BUF_NUM][TOTAL_SAMPLE_NUM] NC_BSS_SECT;
#else
    static int32_t NC_BSS_SECT  pcm_buf[PCM_BUF_NUM][TOTAL_SAMPLE_NUM];
#endif

    UNUSED_ARG(argument);
    dec_stat = DEC_ST_IDLE;
    while (1) {
        result = recv_mail(&mail_type, &mail_param[MAIL_PARAM0], &mail_param[MAIL_PARAM1]);
        if (result == true) {
            /* State transition processing */
            switch (dec_stat) {
                case DEC_ST_META_FIN:       /* Finished the decoding until a metadata */
                    if (mail_type == DEC_MAILID_PLAY) {
                        time_code = flac_get_total_time(&dec_ctrl.flac_ctrl);
                        init_decode_playinfo(time_code, &dec_ctrl.play_info);
                        update_decode_stat(SYS_PLAYSTAT_PLAY, &dec_ctrl.play_info);
                        (void) aud_req_data_out(&data_out_callback);
                        dec_stat = DEC_ST_PLAY;
                    } else if (mail_type == DEC_MAILID_CLOSE) {
                        scux.ClearStop();
                        close_proc(&dec_ctrl.flac_ctrl, (DEC_CbClose)mail_param[MAIL_CLOSE_CB]);
                        dec_stat = DEC_ST_IDLE;
                    } else {
                        /* DO NOTHING */
                    }
                    break;
                case DEC_ST_PLAY:           /* Decoder start */
                    if (mail_type == DEC_MAILID_PAUSE_ON) {
                        update_decode_stat(SYS_PLAYSTAT_PAUSE, &dec_ctrl.play_info);
                        dec_stat = DEC_ST_PAUSE;
                    } else if (mail_type == DEC_MAILID_STOP) {
                        scux.ClearStop();
                        (void) aud_req_zero_out();
                        update_decode_stat(SYS_PLAYSTAT_STOP, &dec_ctrl.play_info);
                        dec_stat = DEC_ST_STOP;
                    } else if ((mail_type == DEC_MAILID_CB_AUD_DATA_OUT) || 
                               (mail_type == DEC_MAILID_SCUX_WRITE_FIN)) {
                        if (mail_type == DEC_MAILID_SCUX_WRITE_FIN) {
                            buf_id = mail_param[MAIL_SCUX_WRITE_BUF_INDEX];
                            buf_num = PCM_BUF_SINGLE;
                        } else {
                            buf_id = PCM_BUF_TOP_ID;
                            buf_num = PCM_BUF_NUM;
                        }
                        result = play_proc(&dec_ctrl.flac_ctrl, buf_id, &pcm_buf[buf_id], buf_num);
                        if (result == true) {
                            time_code = flac_get_play_time(&dec_ctrl.flac_ctrl);
                            update_decode_playtime(time_code, &dec_ctrl.play_info);
                            /* "dec_stat" variable does not change. */
                        } else {
                            result = scux.FlushStop(&flush_callback);
                            if (result == true) {
                                dec_stat = DEC_ST_STOP_PREPARE;
                            } else {
                                /* Error occurred by SCUX driver. */
                                (void) aud_req_zero_out();
                                update_decode_stat(SYS_PLAYSTAT_STOP, &dec_ctrl.play_info);
                                dec_stat = DEC_ST_STOP;
                            }
                        }
                    } else {
                        /* DO NOTHING */
                    }
                    break;
                case DEC_ST_PAUSE:          /* Decoder pause */
                    if (mail_type == DEC_MAILID_PAUSE_OFF) {
                        update_decode_stat(SYS_PLAYSTAT_PLAY, &dec_ctrl.play_info);
                        dec_stat = DEC_ST_PLAY;
                    } else if (mail_type == DEC_MAILID_STOP) {
                        scux.ClearStop();
                        (void) aud_req_zero_out();
                        update_decode_stat(SYS_PLAYSTAT_STOP, &dec_ctrl.play_info);
                        dec_stat = DEC_ST_STOP;
                    } else if ((mail_type == DEC_MAILID_CB_AUD_DATA_OUT) || 
                               (mail_type == DEC_MAILID_SCUX_WRITE_FIN)) {
                        if (mail_type == DEC_MAILID_SCUX_WRITE_FIN) {
                            buf_id = mail_param[MAIL_SCUX_WRITE_BUF_INDEX];
                            buf_num = PCM_BUF_SINGLE;
                        } else {
                            buf_id = PCM_BUF_TOP_ID;
                            buf_num = PCM_BUF_NUM;
                        }
                        result = pause_proc(buf_id, &pcm_buf[buf_id], buf_num);
                        if (result == true) {
                            /* "dec_stat" variable does not change. */
                        } else {
                            result = scux.FlushStop(&flush_callback);
                            if (result == true) {
                                dec_stat = DEC_ST_STOP_PREPARE;
                            } else {
                                /* Error occurred by SCUX driver. */
                                (void) aud_req_zero_out();
                                update_decode_stat(SYS_PLAYSTAT_STOP, &dec_ctrl.play_info);
                                dec_stat = DEC_ST_STOP;
                            }
                        }
                    } else {
                        /* DO NOTHING */
                    }
                    break;
                case DEC_ST_STOP_PREPARE:   /* Preparing of decoder stop */
                    if (mail_type == DEC_MAILID_SCUX_FLUSH_FIN) {
                        (void) aud_req_zero_out();
                        update_decode_stat(SYS_PLAYSTAT_STOP, &dec_ctrl.play_info);
                        dec_stat = DEC_ST_STOP;
                    } else {
                        /* DO NOTHING */
                    }
                    break;
                case DEC_ST_STOP:           /* Decoder stop */
                    if (mail_type == DEC_MAILID_CLOSE) {
                        close_proc(&dec_ctrl.flac_ctrl, (DEC_CbClose)mail_param[MAIL_CLOSE_CB]);
                        dec_stat = DEC_ST_IDLE;
                    } else {
                        /* DO NOTHING */
                    }
                    break;
                case DEC_ST_IDLE:           /* Idle */
                default:
                    if (mail_type == DEC_MAILID_OPEN) {
                        result = open_proc(&dec_ctrl.flac_ctrl, 
                                           (FILE*)mail_param[MAIL_OPEN_FILE], 
                                           (DEC_CbOpen)mail_param[MAIL_OPEN_CB]);
                        if (result == true) {
                            dec_stat = DEC_ST_META_FIN;
                        } else {
                            /* "dec_stat" variable does not change. */
                        }
                    } else {
                        dec_stat = DEC_ST_IDLE; /* This is fail-safe processing. */
                    }
                    break;
            }
        }
    }
}

bool dec_open(FILE * const p_handle, const DEC_CbOpen p_cb)
{
    bool    ret = false;

    if ((p_handle != NULL) && (p_cb != NULL)) {
        ret = send_mail(DEC_MAILID_OPEN, (uint32_t)p_cb, (uint32_t)p_handle);
    }
    return ret;
}

bool dec_play(void)
{
    bool    ret = false;

    ret = send_mail(DEC_MAILID_PLAY, MAIL_PARAM_NON, MAIL_PARAM_NON);

    return ret;
}

bool dec_pause_on(void)
{
    bool    ret = false;

    ret = send_mail(DEC_MAILID_PAUSE_ON, MAIL_PARAM_NON, MAIL_PARAM_NON);

    return ret;
}

bool dec_pause_off(void)
{
    bool    ret = false;

    ret = send_mail(DEC_MAILID_PAUSE_OFF, MAIL_PARAM_NON, MAIL_PARAM_NON);

    return ret;
}

bool dec_stop(void)
{
    bool    ret;

    ret = send_mail(DEC_MAILID_STOP, MAIL_PARAM_NON, MAIL_PARAM_NON);

    return ret;
}

bool dec_close(const DEC_CbClose p_cb)
{
    bool    ret = false;

    if (p_cb != NULL) {
        ret = send_mail(DEC_MAILID_CLOSE, (uint32_t)p_cb, MAIL_PARAM_NON);
    }
    return ret;
}

bool dec_scux_read(void * const p_data, const uint32_t data_size, 
                            const rbsp_data_conf_t * const p_data_conf)
{
    bool            ret = false;
    int32_t         result;

    result = scux.read(p_data, data_size, p_data_conf);
    if (result == ESUCCESS) {
        ret = true;
    }
    return ret;
}

/** Executes the opening process of the decoder
 *
 *  @param p_ctrl Pointer to the control data of FLAC module.
 *  @param p_handle Pointer to the handle of FLAC file.
 *  @param p_cb Pointer to the callback for notification of the process result.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool open_proc(flac_ctrl_t * const p_ctrl, FILE * const p_handle, const DEC_CbOpen p_cb)
{
    bool                ret = false;
    bool                result;
    scux_src_usr_cfg_t  conf;

    if ((p_ctrl != NULL) && (p_handle != NULL) && (p_cb != NULL)) {
        result = flac_open(p_handle, p_ctrl);
        if (result == true) {
            /* Sets SCUX config */
            conf.src_enable           = true;
            conf.word_len             = SCUX_DATA_LEN_24;
            conf.mode_sync            = true;
            conf.input_rate           = p_ctrl->sample_rate;
            conf.output_rate          = DEC_OUTPUT_SAMPLE_RATE;
            conf.select_in_data_ch[0] = SELECT_IN_DATA_CH_0;
            conf.select_in_data_ch[1] = SELECT_IN_DATA_CH_1;
            result = scux.SetSrcCfg(&conf);
            if (result == true) {
                ret = scux.TransStart();
            }
        }
        p_cb(ret, p_ctrl->sample_rate, p_ctrl->channel_num);
    }
    return ret;
}

/** Executes the closing process of the decoder
 *
 *  @param p_ctrl Pointer to the control data of FLAC module.
 *  @param p_cb Pointer to the callback for notification of the process result.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static void close_proc(flac_ctrl_t * const p_ctrl, const DEC_CbClose p_cb)
{
    if ((p_ctrl != NULL) && (p_cb != NULL)) {
        flac_close(p_ctrl);
        p_cb();
    }
}

/** Executes the starting process of the pause
 *
 *  @param p_ctrl Pointer to the control data of FLAC module.
 *  @param buf_id Index of PCM buffer array.
 *  @param p_buf Pointer to PCM buffer array to use in this process.
 *  @param element_num Elements number of PCM buffer array.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool pause_proc(const uint32_t buf_id,
        int32_t (* const p_buf)[TOTAL_SAMPLE_NUM], const uint32_t element_num)
{
    bool                ret = false;
    uint32_t            i;
    int32_t             result;
    const uint32_t      pause_data_size = SAMPLE_NUM_PER_UNIT_MS * sizeof(*p_buf[0]);
    rbsp_data_conf_t    cb_conf = {
        &write_callback,
        NULL
    };

    if ((p_buf != NULL) && (element_num > 0u) && 
        (element_num <= PCM_BUF_NUM) && ((buf_id + element_num) <= PCM_BUF_NUM)) {
        /* Audio output process */
        result = ESUCCESS;
        for (i = 0; (i < element_num) && (result == ESUCCESS); i++) {
            (void) memset(&p_buf[i], 0, pause_data_size);
            cb_conf.p_app_data = (void *)(buf_id + i);
            result = scux.write(&p_buf[i], pause_data_size, &cb_conf);
        }
        if (result == ESUCCESS) {
            ret = true;
        }
    }
    return ret;
}

/** Executes the starting process of the playback
 *
 *  @param p_ctrl Pointer to the control data of FLAC module.
 *  @param buf_id Index of PCM buffer array.
 *  @param p_buf Pointer to PCM buffer array to use in this process.
 *  @param element_num Elements number of PCM buffer array.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool play_proc(flac_ctrl_t * const p_ctrl, const uint32_t buf_id,
        int32_t (* const p_buf)[TOTAL_SAMPLE_NUM], const uint32_t element_num)
{
    bool                ret = false;
    int32_t             result;
    uint32_t            i;
    uint32_t            decoded_cnt;
    uint32_t            num;
    uint32_t            read_byte[PCM_BUF_NUM];
    rbsp_data_conf_t    cb_conf = {
        &write_callback,
        NULL
    };

    if ((p_ctrl != NULL) && (p_buf != NULL) && (element_num > 0u) && 
        (element_num <= PCM_BUF_NUM) && ((buf_id + element_num) <= PCM_BUF_NUM)) {
        /* FLAC decoder process */
        decoded_cnt = 0u;
        do {
            num = get_audio_data(p_ctrl, p_buf[decoded_cnt], sizeof(p_buf[0])/sizeof(*p_buf[0]));
            read_byte[decoded_cnt] = num * sizeof(num);
            if (num > 0u) {
                decoded_cnt++;
            }
        } while ((decoded_cnt < element_num) &&  (num > 0u));
        /* Audio output process */
        if (decoded_cnt > 0u) {
            result = ESUCCESS;
            for (i = 0; (i < decoded_cnt) && (result == ESUCCESS); i++) {
                cb_conf.p_app_data = (void *)(buf_id + i);
                result = scux.write(&p_buf[i], read_byte[i], &cb_conf);
            }
            if (result == ESUCCESS) {
                ret = true;
            }
        }
    }
    return ret;
}

/** Gets the decoded data from FLAC decoder library
 *
 *  @param p_ctrl Pointer to the control data of FLAC module.
 *  @param p_buf Pointer to PCM buffer array to store the decoded data.
 *  @param buf_num Elements number of PCM buffer array.
 *
 *  @returns 
 *    Elements number of decoded data.
 */
static uint32_t get_audio_data(flac_ctrl_t * const p_ctrl, 
                            int32_t * const p_buf, const uint32_t buf_num)
{
    uint32_t    read_cnt = 0u;
    bool        result;

    if ((p_ctrl != NULL) && (p_buf != NULL) && (buf_num > 0u)) {
        result = flac_set_pcm_buf(p_ctrl, p_buf, buf_num);
        while ((result == true) && ((read_cnt + MAX_SAMPLE_PER_1BLOCK) <= buf_num)) {
            result = flac_decode(p_ctrl);
            read_cnt = flac_get_pcm_cnt(p_ctrl);
        }
    }
    return read_cnt;
}

/** Callback function of Audio Out Thread
 *
 *  @param result Result of the process of Audio Out Thread
 */
static void data_out_callback(const bool result)
{
    (void) send_mail(DEC_MAILID_CB_AUD_DATA_OUT, (uint32_t)result, MAIL_PARAM_NON);
}

/** Callback function of SCUX driver
 *
 *  @param p_data Pointer to PCM byffer array.
 *  @param result Result of the process.
 *  @param p_app_data The control ID of PCM buffer.
 */
static void write_callback(void * p_data, int32_t result, void * p_app_data)
{
    const uint32_t  buf_id = (uint32_t)p_app_data;
    bool            flag_result;

    UNUSED_ARG(p_data);
    if (result > 0) {
        flag_result = true;
    } else {
        flag_result = false;
    }
    (void) send_mail(DEC_MAILID_SCUX_WRITE_FIN, (uint32_t)flag_result, buf_id);
}

/** Callback function of SCUX driver
 *
 *  @param result Result of the process.
 */
static void flush_callback(int32_t result)
{
    bool        flag_result;

    if (result > 0) {
        flag_result = true;
    } else {
        flag_result = false;
    }
    (void) send_mail(DEC_MAILID_SCUX_FLUSH_FIN, (uint32_t)flag_result, MAIL_PARAM_NON);
}

/** Sends the mail to Decode thread
 *
 *  @param mail_id Mail ID
 *  @param param0 Parameter 0 of this mail
 *  @param param1 Parameter 1 of this mail
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool send_mail(const DEC_MAIL_ID mail_id, 
                            const uint32_t param0, const uint32_t param1)
{
    bool            ret = false;
    osStatus        stat;
    dec_mail_t      * const p_mail = mail_box.alloc();

    if (p_mail != NULL) {
        p_mail->mail_id = mail_id;
        p_mail->param[MAIL_PARAM0] = param0;
        p_mail->param[MAIL_PARAM1] = param1;
        stat = mail_box.put(p_mail);
        if (stat == osOK) {
            ret = true;
        } else {
            (void) mail_box.free(p_mail);
        }
    }
    return ret;
}

/** Receives the mail to Decode thread
 *
 *  @param p_mail_id Pointer to the variable to store the mail ID
 *  @param p_param0 Pointer to the variable to store the parameter 0 of this mail
 *  @param p_param1 Pointer to the variable to store the parameter 1 of this mail
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool recv_mail(DEC_MAIL_ID * const p_mail_id, 
                        uint32_t * const p_param0, uint32_t * const p_param1)
{
    bool            ret = false;
    osEvent         evt;
    dec_mail_t      *p_mail;
    
    if ((p_mail_id != NULL) && (p_param0 != NULL) && (p_param1 != NULL)) {
        evt = mail_box.get();
        if (evt.status == osEventMail) {
            p_mail = (dec_mail_t *)evt.value.p;
            if (p_mail != NULL) {
                *p_mail_id = p_mail->mail_id;
                *p_param0 = p_mail->param[MAIL_PARAM0];
                *p_param1 = p_mail->param[MAIL_PARAM1];
                ret = true;
            }
            (void) mail_box.free(p_mail);
        }
    }
    return ret;
}

/** Updates the status of Decode thread
 *
 *  @param stat New status of Decode thread
 *  @param p_play_info Pointer to the playback information of the playback file
 */
static void update_decode_stat(const SYS_PlayStat stat, play_info_t * const p_play_info)
{
    if (p_play_info != NULL) {
        if (p_play_info->play_stat != stat) {
            p_play_info->play_stat = stat;
            notify_decode_stat(p_play_info);
        }
    }
}

/** Updates the playback time
 *
 *  @param play_time Current playback time
 *  @param p_play_info Pointer to the playback information of the playback file
 */
static void update_decode_playtime(const uint32_t play_time, play_info_t * const p_play_info)
{
    if (p_play_info != NULL) {
        if (p_play_info->play_time != play_time) {
            p_play_info->play_time = play_time;
            notify_decode_stat(p_play_info);
        }
    }
}

/** Initialises the playback information
 *
 *  @param total_time Total playback time
 *  @param p_play_info Pointer to the playback information of the playback file
 */
static void init_decode_playinfo(const uint32_t total_time, play_info_t * const p_play_info)
{
    if (p_play_info != NULL) {
        p_play_info->play_stat  = SYS_PLAYSTAT_STOP;
        p_play_info->play_time  = 0u;
        p_play_info->total_time = total_time;
    }
}

/** Notifies Main thread of the status of Decode thread
 *
 *  @param p_play_info Pointer to the playback information of the playback file
 */
static void notify_decode_stat(const play_info_t * const p_play_info)
{
    if (p_play_info != NULL) {
        (void) sys_notify_play_time(p_play_info->play_stat, 
                    p_play_info->play_time, p_play_info->total_time);
    }
}
