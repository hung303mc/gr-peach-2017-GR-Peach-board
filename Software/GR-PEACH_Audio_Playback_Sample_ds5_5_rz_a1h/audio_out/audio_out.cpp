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
#include "decode.h"
#include "audio_out.h"
#include "display.h"
#include "TLV320_RBSP.h"

/*--- Macro definition of mbed-rtos mail ---*/
#define MAIL_QUEUE_SIZE     (12)    /* Queue size */
#define MAIL_PARAM_NUM      (3)     /* Elements number of mail parameter array */

/* aud_mail_t */
#define MAIL_PARAM0         (0)     /* Index number of mail parameter array */
#define MAIL_PARAM1         (1)     /* Index number of mail parameter array */
#define MAIL_PARAM2         (2)     /* Index number of mail parameter array */

#define MAIL_PARAM_NON      (0u)    /* Value of unused element of mail parameter array */

/* mail_id = AUD_MAILID_DATA_OUT */
#define MAIL_DATA_OUT_CB            (MAIL_PARAM0)   /* Callback function */

/* mail_id = AUD_MAILID_ZERO_OUT : No parameter */

/* mail_id = AUD_MAILID_SCUX_READ_FIN */
#define MAIL_SCUX_READ_RESULT       (MAIL_PARAM0)   /* Result of the process */
#define MAIL_SCUX_READ_BUF_INDEX    (MAIL_PARAM1)   /* Index number of PCM buffer */
#define MAIL_SCUX_READ_BYTE_NUM     (MAIL_PARAM2)   /* Byte number of PCM data */

/* mail_id = AUD_MAILID_PCM_OUT_FIN */
#define MAIL_PCM_OUT_RESULT         (MAIL_PARAM0)   /* Result of the process */
#define MAIL_PCM_OUT_BUF_INDEX      (MAIL_PARAM1)   /* Index number of PCM buffer */

/*--- Macro definition of PCM buffer ---*/
#define UNIT_TIME_MS                (10u)   /* Unit time of PCM data processing (ms) */
#define SEC_TO_MSEC                 (1000u)

/* Sample number per unit time (ms) */
#define SAMPLE_PER_UNIT_MS          ((UNIT_TIME_MS * DEC_OUTPUT_SAMPLE_RATE) / SEC_TO_MSEC)

#define OUTPUT_START_TRIGGER        (3)     /* Numerical value of the trigger */
                                            /* to start the output of PCM data */
#define OUTPUT_UPDATE_TRIGGER       (1)     /* Numerical value of the trigger */
                                            /* to update the output of PCM data */

#define PCM_BUF_NUM                 (DEC_SCUX_READ_NUM)
#define TOTAL_SAMPLE_NUM            (SAMPLE_PER_UNIT_MS * DEC_OUTPUT_CHANNEL_NUM)

/*--- Macro definition of TLV320_RBSP ---*/
#define AUDIO_POWER_MIC_OFF         (0x02)  /* Microphone input :OFF */
#define AUDIO_INT_LEVEL             (0x80)
#define AUDIO_READ_NUM              (0)
#define AUDIO_WRITE_NUM             (PCM_BUF_NUM)
#define ERR_MSG_TLV320_RBSP_WRITE   "\nError: TLV320_RBSP::write()\n"

/* 4 bytes aligned. No cache memory. */
#if defined(__ICCARM__)
#define NC_BSS_SECT                 @ ".mirrorram"
#else
#define NC_BSS_SECT                 __attribute__((section("NC_BSS"),aligned(4)))
#endif
#define ERR_MSG_RECV_ILLEGAL_MAIL   "\nError: aud_thread function received illegal mail.\n"

/*--- User defined types of mbed-rtos mail ---*/
typedef enum {
    AUD_MAILID_DUMMY = 0,
    AUD_MAILID_DATA_OUT,            /* Requests the output of PCM data. */
    AUD_MAILID_ZERO_OUT,            /* Requests the output of zero data. */
    AUD_MAILID_SCUX_READ_FIN,       /* Finished the reading process of SCUX. */
    AUD_MAILID_PCM_OUT_FIN,         /* Finished the output of data. */
    AUD_MAILID_NUM
} AUD_MAIL_ID;

typedef struct {
    AUD_MAIL_ID     mail_id;
    uint32_t        param[MAIL_PARAM_NUM];
} aud_mail_t;

/*--- User defined types of audio output thread ---*/
/* The control data of PCM buffer. */
typedef struct {
    uint32_t    pcm_buf_index;      /* Index of PCM buffer array */
    uint32_t    pcm_buf_remain_cnt; /* Counter of the remain elements in PCM buffer array */
    uint32_t    pcm_stock_cnt;      /* Counter of the stock elements in PCM buffer array */
    uint32_t    output_trg_cnt;     /* Number of the trigger to start the output of PCM data */
} pcm_buf_ctrl_t;

static Mail<aud_mail_t, MAIL_QUEUE_SIZE> mail_box;
static TLV320_RBSP audio(P10_13, I2C_SDA, I2C_SCL, P4_4, P4_5, P4_7,
                     P4_6, AUDIO_INT_LEVEL, AUDIO_WRITE_NUM, AUDIO_READ_NUM);

static void init_pcm_buf(pcm_buf_ctrl_t * const p_ctrl);
static bool read_scux(int32_t (* const p_buf)[TOTAL_SAMPLE_NUM], const uint32_t buf_id);
static bool write_audio(int32_t (* const p_buf)[TOTAL_SAMPLE_NUM], const uint32_t buf_id);
static void read_callback(void * p_data, int32_t result, void * p_app_data);
static void pcm_out_callback(void * p_data, int32_t result, void * p_app_data);
static bool send_mail(const AUD_MAIL_ID mail_id, const uint32_t param0, 
                            const uint32_t param1, const uint32_t param2);
static bool recv_mail(AUD_MAIL_ID * const p_mail_id, uint32_t * const p_param0, 
                        uint32_t * const p_param1, uint32_t * const p_param2);

void aud_thread(void const *argument)
{
    pcm_buf_ctrl_t              buf_ctrl;
    pcm_buf_ctrl_t              * const p_ctrl = &buf_ctrl;
    bool                        scux_read_enable = false;
    AUD_MAIL_ID                 mail_type;
    uint32_t                    mail_param[MAIL_PARAM_NUM];
    bool                        result;
    AUD_CbDataOut               cb_data_out;
    uint32_t                    i;
    uint32_t                    buf_id;
    int32_t                     *p_buf;
    uint32_t                    byte_cnt;
#if defined(__ICCARM__)
    static int32_t pcm_buf[PCM_BUF_NUM][TOTAL_SAMPLE_NUM] NC_BSS_SECT;
#else
    static int32_t NC_BSS_SECT  pcm_buf[PCM_BUF_NUM][TOTAL_SAMPLE_NUM];
#endif

    UNUSED_ARG(argument);

    /* Initializes the control data of PCM buffer. */
    init_pcm_buf(p_ctrl);

    /* Sets the output of PCM data using TLV320_RBSP. */
    (void) audio.format(DEC_OUTPUT_BITS_PER_SAMPLE);
    (void) audio.frequency(DEC_OUTPUT_SAMPLE_RATE);
    audio.power(AUDIO_POWER_MIC_OFF);
    while (1) {
        result = recv_mail(&mail_type, &mail_param[MAIL_PARAM0], 
                    &mail_param[MAIL_PARAM1], &mail_param[MAIL_PARAM2]);
        if (result == true) {
            switch (mail_type) {
                case AUD_MAILID_DATA_OUT:        /* Requests the output of PCM data. */
                    cb_data_out = (AUD_CbDataOut)mail_param[MAIL_DATA_OUT_CB];
                    if (scux_read_enable != true) {
                        scux_read_enable = true;
                        result = true;
                        for (i = 0; (i < p_ctrl->pcm_buf_remain_cnt) && (result == true); i++) {
                            buf_id = (p_ctrl->pcm_buf_index + i) % PCM_BUF_NUM;
                            result = read_scux(&pcm_buf[buf_id], buf_id);
                        }
                        if (result == true) {
                            p_ctrl->output_trg_cnt = OUTPUT_START_TRIGGER;
                        }
                    } else {
                        result = false;
                    }
                    cb_data_out(result);
                    break;
                case AUD_MAILID_ZERO_OUT:        /* Requests the output of zero data. */
                    scux_read_enable = false;
                    break;
                case AUD_MAILID_SCUX_READ_FIN:   /* Finished the reading process of SCUX. */
                    buf_id = mail_param[MAIL_SCUX_READ_BUF_INDEX];
                    byte_cnt = mail_param[MAIL_SCUX_READ_BYTE_NUM];
                    if ((buf_id < PCM_BUF_NUM) && (byte_cnt <= sizeof(pcm_buf[0]))) {
                        if (byte_cnt < sizeof(pcm_buf[0])) {
                            /* End of stream */
                            /* Fills the remain area of PCM buffer with 0. */
                            p_buf = &pcm_buf[buf_id][byte_cnt/sizeof(pcm_buf[0][0])];
                            (void) memset(p_buf, 0, sizeof(pcm_buf[0]) - byte_cnt);
                            p_ctrl->output_trg_cnt = OUTPUT_UPDATE_TRIGGER;
                        }
                        p_ctrl->pcm_stock_cnt++;
                        if (p_ctrl->pcm_stock_cnt >= p_ctrl->output_trg_cnt) {
                            /* Starts the output of PCM data. */
                            result = true;
                            for (i = 0; (i < p_ctrl->pcm_stock_cnt) && (result == true); i++) {
                                buf_id = (p_ctrl->pcm_buf_index + i) % PCM_BUF_NUM;
                                result = write_audio(&pcm_buf[buf_id], buf_id);
                                if (result == true) {
                                    p_ctrl->pcm_buf_remain_cnt--;
                                }
                            }
                            if (result != true) {
                                /* Unexpected cases : Output error message to PC */
                                (void) dsp_notify_print_string(ERR_MSG_TLV320_RBSP_WRITE);
                            }
                            p_ctrl->pcm_buf_index  = 
                                        (p_ctrl->pcm_buf_index + p_ctrl->pcm_stock_cnt) % PCM_BUF_NUM;
                            p_ctrl->pcm_stock_cnt  = 0u;
                            p_ctrl->output_trg_cnt = OUTPUT_UPDATE_TRIGGER;
                        }
                    } else {
                        /* Unexpected cases : This is fail-safe processing. */
                        scux_read_enable = false;
                    }
                    break;
                case AUD_MAILID_PCM_OUT_FIN:     /* Finished the output of data. */
                    p_ctrl->pcm_buf_remain_cnt++;
                    if ((int32_t)mail_param[MAIL_PCM_OUT_RESULT] == true) {
                        if (scux_read_enable == true) {
                            buf_id = mail_param[MAIL_PCM_OUT_BUF_INDEX];
                            (void) read_scux(&pcm_buf[buf_id], buf_id);
                        }
                    } else {
                        /* Unexpected cases : Output error message to PC */
                        (void) dsp_notify_print_string(ERR_MSG_TLV320_RBSP_WRITE);
                    }
                    break;
                default:
                    /* Unexpected cases : Output error message to PC */
                    (void) dsp_notify_print_string(ERR_MSG_RECV_ILLEGAL_MAIL);
                    break;
            }
        }
    }
}

bool aud_req_data_out(const AUD_CbDataOut p_cb)
{
    bool    ret = false;

    if (p_cb != NULL) {
        ret = send_mail(AUD_MAILID_DATA_OUT, (uint32_t)p_cb, MAIL_PARAM_NON, MAIL_PARAM_NON);
    }
    return ret;
}

bool aud_req_zero_out(void)
{
    bool    ret = false;

    ret = send_mail(AUD_MAILID_ZERO_OUT, MAIL_PARAM_NON, MAIL_PARAM_NON, MAIL_PARAM_NON);
    return ret;
}

bool aud_get_audio_data(const AUD_CbAudioData p_cb, uint16_t * const p_buf)
{
    UNUSED_ARG(p_cb);
    UNUSED_ARG(p_buf);
    return true;
}

/** Initialises the control data of PCM buffer
 *
 *  @param p_ctrl Pointer to the control data of PCM buffer.
 */
static void init_pcm_buf(pcm_buf_ctrl_t * const p_ctrl)
{
    if (p_ctrl != NULL) {
        p_ctrl->pcm_buf_index = 0u;
        p_ctrl->pcm_buf_remain_cnt = PCM_BUF_NUM;
        p_ctrl->pcm_stock_cnt = 0u;
        p_ctrl->output_trg_cnt = OUTPUT_START_TRIGGER;
    }
}

/** Gets PCM data from SCUX driver
 *
 *  @param p_buf Pointer to PCM buffer array to store the data.
 *  @param buf_id The control ID of PCM buffer.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool read_scux(int32_t (* const p_buf)[TOTAL_SAMPLE_NUM], const uint32_t buf_id)
{
    bool                ret = false;
    rbsp_data_conf_t    cb_conf = {
        &read_callback,
        NULL
    };

    if ((p_buf != NULL) && (buf_id < PCM_BUF_NUM)) {
        cb_conf.p_app_data = (void *)buf_id;
        ret = dec_scux_read(p_buf, sizeof(p_buf[0]), &cb_conf);
    }
    return ret;
}

/** Writes PCM data to TLV320_RBSP driver
 *
 *  @param p_buf Pointer to PCM buffer array.
 *  @param buf_id The control ID of PCM buffer.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool write_audio(int32_t (* const p_buf)[TOTAL_SAMPLE_NUM], const uint32_t buf_id)
{
    bool                ret = false;
    int32_t             result;
    rbsp_data_conf_t    cb_conf = {
        &pcm_out_callback,
        NULL
    };

    if ((p_buf != NULL) && (buf_id < PCM_BUF_NUM)) {
        cb_conf.p_app_data = (void *)buf_id;
        result = audio.write(p_buf, sizeof(p_buf[0]), &cb_conf);
        if (result == ESUCCESS) {
            ret = true;
        }
    }
    return ret;
}

/** Callback function of SCUX driver
 *
 *  @param p_data Pointer to PCM byffer array.
 *  @param result Result of the process.
 *  @param p_app_data The control ID of PCM buffer.
 */
static void read_callback(void * p_data, int32_t result, void * p_app_data)
{
    const uint32_t  buf_id = (uint32_t)p_app_data;
    uint32_t        read_byte;
    bool            flag_result;

    UNUSED_ARG(p_data);
    if (result > 0) {
        flag_result = true;
        read_byte = (uint32_t)result;
    } else {
        flag_result = false;
        read_byte = 0u;
    }
    (void) send_mail(AUD_MAILID_SCUX_READ_FIN, (uint32_t)flag_result, buf_id, read_byte);
}

/** Callback function of TLV320_RBSP driver
 *
 *  @param p_data Pointer to PCM byffer array.
 *  @param result Result of the process.
 *  @param p_app_data The control ID of PCM buffer.
 */
static void pcm_out_callback(void * p_data, int32_t result, void * p_app_data)
{
    const uint32_t  buf_id = (uint32_t)p_app_data;
    bool            flag_result;

    UNUSED_ARG(p_data);
    if (result > 0) {
        flag_result = true;
    } else {
        flag_result = false;
    }
    (void) send_mail(AUD_MAILID_PCM_OUT_FIN, (uint32_t)flag_result, buf_id, MAIL_PARAM_NON);
}

/** Sends the mail to Decode thread
 *
 *  @param mail_id Mail ID
 *  @param param0 Parameter 0 of this mail
 *  @param param1 Parameter 1 of this mail
 *  @param param2 Parameter 2 of this mail
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool send_mail(const AUD_MAIL_ID mail_id, const uint32_t param0, 
                            const uint32_t param1, const uint32_t param2)
{
    bool            ret = false;
    osStatus        stat;
    aud_mail_t      * const p_mail = mail_box.alloc();

    if (p_mail != NULL) {
        p_mail->mail_id = mail_id;
        p_mail->param[MAIL_PARAM0] = param0;
        p_mail->param[MAIL_PARAM1] = param1;
        p_mail->param[MAIL_PARAM2] = param2;
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
 *  @param p_param2 Pointer to the variable to store the parameter 2 of this mail
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool recv_mail(AUD_MAIL_ID * const p_mail_id, uint32_t * const p_param0, 
                        uint32_t * const p_param1, uint32_t * const p_param2)
{
    bool            ret = false;
    osEvent         evt;
    aud_mail_t      *p_mail;
    
    if ((p_mail_id != NULL) && (p_param0 != NULL) && 
        (p_param1 != NULL) && (p_param2 != NULL)) {
        evt = mail_box.get();
        if (evt.status == osEventMail) {
            p_mail = (aud_mail_t *)evt.value.p;
            if (p_mail != NULL) {
                *p_mail_id = p_mail->mail_id;
                *p_param0 = p_mail->param[MAIL_PARAM0];
                *p_param1 = p_mail->param[MAIL_PARAM1];
                *p_param2 = p_mail->param[MAIL_PARAM2];
                ret = true;
            }
            (void) mail_box.free(p_mail);
        }
    }
    return ret;
}
