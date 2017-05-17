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
#include "display.h"
#include "disp_term.h"

/*--- Macro definition of mbed-rtos mail ---*/
#define MAIL_QUEUE_SIZE             (12)    /* Queue size */
#define MAIL_PARAM_NUM              (64)    /* Elements number of mail parameter array */

/* dsp_mail_t */
/* mail_id = DSP_MAILID_CYCLE_IND : No parameter */

/* mail_id = DSP_MAILID_CMD_STR */
#define MAIL_CMDSTR_FIN_FLG         (0)     /* Completion status of the input by the command-line */
#define MAIL_CMDSTR_STR_START       (1)     /* Start position of input string by the command-line */
#define MAIL_CMDSTR_STR_SIZE        (DSP_CMD_INPT_STR_MAX_LEN)
                                            /* Size of input string by the command-line */

/* mail_id = DSP_MAILID_PRINT_STR */
#define MAIL_DISPSTR_STR_START      (0)     /* Start position of display string */
#define MAIL_DISPSTR_STR_SIZE       (DSP_DISP_STR_MAX_LEN)  /* Size of display string */

/* mail_id = DSP_MAILID_PLAY_TIME */
#define MAIL_PLAYTIME_STAT          (0)     /* Playback status */
#define MAIL_PLAYTIME_TRACK_L       (1)     /* Track number */
#define MAIL_PLAYTIME_TRACK_H       (2)     /* Track number */
#define MAIL_PLAYTIME_PLAYTIME_L    (3)     /* Playback time */
#define MAIL_PLAYTIME_PLAYTIME_M    (4)     /* Playback time */
#define MAIL_PLAYTIME_PLAYTIME_H    (5)     /* Playback time */
#define MAIL_PLAYTIME_TOTALTIME_L   (6)     /* Total playback time */
#define MAIL_PLAYTIME_TOTALTIME_M   (7)     /* Total playback time */
#define MAIL_PLAYTIME_TOTALTIME_H   (8)     /* Total playback time */

/* mail_id = DSP_MAILID_PLAY_INFO */
#define MAIL_PLAYINFO_TRACK_L       (0)     /* Track number */
#define MAIL_PLAYINFO_TRACK_H       (1)     /* Track number */
#define MAIL_PLAYINFO_SAMPFREQ_L    (2)     /* Sampling frequency */
#define MAIL_PLAYINFO_SAMPFREQ_M    (3)     /* Sampling frequency */
#define MAIL_PLAYINFO_SAMPFREQ_H    (4)     /* Sampling frequency */
#define MAIL_PLAYINFO_CHANNEL       (5)     /* Channel structure */

/* mail_id = DSP_MAILID_PLAY_MODE */
#define MAIL_PLAYMODE_REPEAT        (0)     /* Repeat mode */

/* mail_id = DSP_MAILID_FILE_NAME */
#define MAIL_FILENAME_STR_START     (0)     /* Start position of file name string */
#define MAIL_FILENAME_STR_SIZE      (64)    /* Size of file name string */

/* mail_id = DSP_MAILID_HELP */
#define MAIL_HELP_PARAM             (0)     /* No mail parameter to be used */

#define MAIL_PARAM_NON              (0u)    /* Value of unused element of mail parameter array */

#define BYTE_SHIFT                  (8u)
#define WORD_SHIFT                  (16u)

/*--- User defined types of mbed-rtos mail ---*/
typedef struct {
    DSP_MAIL_ID     mail_id;
    uint8_t         param[MAIL_PARAM_NUM];
} dsp_mail_t;

/* Control data of display thread */
typedef struct {
    dsp_com_ctrl_t          com;            /* Common data */
    dsp_trm_ctrl_t          trm;            /* Terminal output module only */
    dsp_tft_ctrl_t          tft;            /* TFT module only */
} dsp_ctrl_t;

static Mail<dsp_mail_t, MAIL_QUEUE_SIZE> mail_box;

static void init_ctrl_data(dsp_ctrl_t * const p_ctrl);
static bool send_mail(const dsp_mail_t * const p_data);
static bool recv_mail(dsp_mail_t * const p_data);
static bool decode_mail(const dsp_mail_t * const p_mail, dsp_ctrl_t * const p_ctrl);
static void clear_one_shot_data(dsp_ctrl_t * const p_ctrl);

void dsp_thread(void const *argument)
{
    dsp_mail_t              recv_data;
    bool                    result;
    static dsp_ctrl_t       dsp_ctrl;
    
    UNUSED_ARG(argument);

    init_ctrl_data(&dsp_ctrl);
    dsp_init_term();
    while (1) {
        result = recv_mail(&recv_data);
        if (result == true) {
            result = decode_mail(&recv_data, &dsp_ctrl);
            if (result == true) {
                /* Executes main function of terminal output module */
                dsp_output_term(recv_data.mail_id, &dsp_ctrl.com, &dsp_ctrl.trm);
                /* Clears the one shot data */
                clear_one_shot_data(&dsp_ctrl);
            }
        }
    }
}

bool dsp_notify_play_time(const SYS_PlayStat play_stat, const uint32_t file_no, 
                            const uint32_t play_time, const uint32_t total_time)
{
    bool            ret;
    dsp_mail_t      data;

    data.mail_id                          = DSP_MAILID_PLAY_TIME;
    data.param[MAIL_PLAYTIME_STAT]        = (uint8_t)play_stat;
    data.param[MAIL_PLAYTIME_TRACK_L]     = (uint8_t)file_no;
    data.param[MAIL_PLAYTIME_TRACK_H]     = (uint8_t)(file_no >> BYTE_SHIFT);
    data.param[MAIL_PLAYTIME_PLAYTIME_L]  = (uint8_t)play_time;
    data.param[MAIL_PLAYTIME_PLAYTIME_M]  = (uint8_t)(play_time >> BYTE_SHIFT);
    data.param[MAIL_PLAYTIME_PLAYTIME_H]  = (uint8_t)(play_time >> WORD_SHIFT);
    data.param[MAIL_PLAYTIME_TOTALTIME_L] = (uint8_t)total_time;
    data.param[MAIL_PLAYTIME_TOTALTIME_M] = (uint8_t)(total_time >> BYTE_SHIFT);
    data.param[MAIL_PLAYTIME_TOTALTIME_H] = (uint8_t)(total_time >> WORD_SHIFT);
    ret = send_mail(&data);

    return ret;
}

bool dsp_notify_play_info(const uint32_t file_no, 
                        const uint32_t sample_freq, const uint32_t channel_num)
{
    bool            ret;
    dsp_mail_t      data;

    data.mail_id                          = DSP_MAILID_PLAY_INFO;
    data.param[MAIL_PLAYINFO_TRACK_L]     = (uint8_t)file_no;
    data.param[MAIL_PLAYINFO_TRACK_H]     = (uint8_t)(file_no >> BYTE_SHIFT);
    data.param[MAIL_PLAYINFO_SAMPFREQ_L]  = (uint8_t)sample_freq;
    data.param[MAIL_PLAYINFO_SAMPFREQ_M]  = (uint8_t)(sample_freq >> BYTE_SHIFT);
    data.param[MAIL_PLAYINFO_SAMPFREQ_H]  = (uint8_t)(sample_freq >> WORD_SHIFT);
    data.param[MAIL_PLAYINFO_CHANNEL]     = (uint8_t)channel_num;
    ret = send_mail(&data);

    return ret;
}

bool dsp_notify_play_mode(const bool rep_mode)
{
    bool            ret;
    dsp_mail_t      data;

    data.mail_id                     = DSP_MAILID_PLAY_MODE;
    data.param[MAIL_PLAYMODE_REPEAT] = (uint8_t)rep_mode;
    ret = send_mail(&data);

    return ret;
}

bool dsp_notify_file_name(const char_t * const p_str)
{
    bool            ret = false;
    dsp_mail_t      data;

    if (p_str != NULL) {
        data.mail_id = DSP_MAILID_FILE_NAME;
        (void) memcpy(&data.param[MAIL_FILENAME_STR_START], p_str, MAIL_FILENAME_STR_SIZE);
        data.param[(MAIL_FILENAME_STR_START + MAIL_FILENAME_STR_SIZE) - 1] = '\0';
        ret = send_mail(&data);
    }

    return ret;
}

bool dsp_notify_print_string(const char_t * const p_str)
{
    bool            ret = false;
    dsp_mail_t      data;

    if (p_str != NULL) {
        data.mail_id = DSP_MAILID_PRINT_STR;
        (void) memcpy(&data.param[MAIL_DISPSTR_STR_START], p_str, MAIL_DISPSTR_STR_SIZE);
        data.param[(MAIL_DISPSTR_STR_START + MAIL_DISPSTR_STR_SIZE) - 1] = '\0';
        ret = send_mail(&data);
    }

    return ret;
}

bool dsp_notify_input_string(const char_t * const p_str, const bool flag_fin)
{
    bool            ret = false;
    dsp_mail_t      data;

    if (p_str != NULL) {
        data.mail_id = DSP_MAILID_CMD_STR;
        data.param[MAIL_CMDSTR_FIN_FLG] = (uint8_t)flag_fin;
        (void) memcpy(&data.param[MAIL_CMDSTR_STR_START], p_str, MAIL_CMDSTR_STR_SIZE);
        data.param[(MAIL_CMDSTR_STR_START + MAIL_CMDSTR_STR_SIZE) - 1] = '\0';
        ret = send_mail(&data);
    }

    return ret;
}

bool dsp_req_help(void)
{
    bool            ret = false;
    dsp_mail_t      data;

    data.mail_id = DSP_MAILID_HELP;
    data.param[MAIL_HELP_PARAM] = (uint8_t)MAIL_PARAM_NON;
    ret = send_mail(&data);
    return ret;
}

/** initialises the control data of the display module
 *
 *  @param p_ctrl Pointer to control data of display module.
 */
static void init_ctrl_data(dsp_ctrl_t * const p_ctrl)
{
    if (p_ctrl != NULL) {
        /* Initialises the common data of the display module. */
        p_ctrl->com.disp_mode       = 0u;
        p_ctrl->com.play_stat       = SYS_PLAYSTAT_STOP;
        p_ctrl->com.track_id        = 0u;
        p_ctrl->com.play_time       = 0u;
        p_ctrl->com.total_time      = 0u;
        p_ctrl->com.samp_freq       = 0u;
        p_ctrl->com.channel         = 0u;
        p_ctrl->com.repeat_mode     = false;
        p_ctrl->com.file_name[0]    = '\0';
        p_ctrl->com.dspl_str[0]     = '\0';
        
        /* Initialises the data of the terminal output module. */
        p_ctrl->trm.edge_fin_inpt = false;
        p_ctrl->trm.inpt_str[0] = '\0';

        /* Does not initialize the data of TFT module. */
        /* Initial values are different by the display mode. */
        /* Therefore the data of TFT module is initialized by tft_init_proc function. */
    }
}

/** Sends the mail to main thread
 *
 *  @param p_data Pointer to the structure of the data
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool send_mail(const dsp_mail_t * const p_data)
{
    bool            ret = false;
    osStatus        stat;
    dsp_mail_t      *p_mail;

    if (p_data != NULL) {
        p_mail = mail_box.alloc();
        if (p_mail != NULL) {
            *p_mail = *p_data;
            stat = mail_box.put(p_mail);
            if (stat == osOK) {
                ret = true;
            } else {
                (void) mail_box.free(p_mail);
            }
        }
    }
    return ret;
}

/** Receives the mail to main thread
 *
 *  @param p_data Pointer to the structure of the data
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool recv_mail(dsp_mail_t * const p_data)
{
    bool            ret = false;
    osEvent         evt;
    dsp_mail_t      *p_mail;

    if (p_data != NULL) {
        evt = mail_box.get();
        if (evt.status == osEventMail) {
            p_mail = (dsp_mail_t *)evt.value.p;
            if (p_mail != NULL) {
                *p_data = *p_mail;
                ret = true;
            }
            (void) mail_box.free(p_mail);
        }
    }
    return ret;
}

/** Decodes the display thread mail
 *
 *  @param p_mail Pointer to the structure of the data
 *  @param p_ctrl Pointer to control data of display module.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool decode_mail(const dsp_mail_t * const p_mail, dsp_ctrl_t * const p_ctrl)
{
    bool            ret = false;
    
    if ((p_mail != NULL) && (p_ctrl != NULL)) {
        /* Decodes the received mail */
        switch(p_mail->mail_id) {
            case DSP_MAILID_CYCLE_IND:       /* Cyclic notice */
                ret = true;
                break;
            case DSP_MAILID_CMD_STR:         /* Input character string by the command-line */
                ret = true;
                if ((int32_t)p_mail->param[MAIL_CMDSTR_FIN_FLG] == true) {
                    p_ctrl->trm.edge_fin_inpt = true;
                } else {
                    p_ctrl->trm.edge_fin_inpt = false;
                }
                (void) memcpy(&p_ctrl->trm.inpt_str[0], 
                              &p_mail->param[MAIL_CMDSTR_STR_START], 
                              sizeof(p_ctrl->trm.inpt_str));
                p_ctrl->trm.inpt_str[DSP_CMD_INPT_STR_MAX_LEN - 1] = '\0';
                break;
            case DSP_MAILID_PRINT_STR:       /* Character string for the status indication */
                ret = true;
                (void) memcpy(&p_ctrl->com.dspl_str[0], 
                              &p_mail->param[MAIL_DISPSTR_STR_START], 
                              sizeof(p_ctrl->com.dspl_str));
                p_ctrl->com.dspl_str[DSP_DISP_STR_MAX_LEN - 1] = '\0';
                break;
            case DSP_MAILID_PLAY_TIME:       /* Playback time */
                ret = true;
                p_ctrl->com.play_stat   = (SYS_PlayStat)p_mail->param[MAIL_PLAYTIME_STAT];
                p_ctrl->com.track_id    = (((uint32_t)p_mail->param[MAIL_PLAYTIME_TRACK_H] << BYTE_SHIFT) |
                                            (uint32_t)p_mail->param[MAIL_PLAYTIME_TRACK_L]);
                p_ctrl->com.play_time   = (((uint32_t)p_mail->param[MAIL_PLAYTIME_PLAYTIME_H] << WORD_SHIFT) |
                                           ((uint32_t)p_mail->param[MAIL_PLAYTIME_PLAYTIME_M] << BYTE_SHIFT) |
                                           ((uint32_t)p_mail->param[MAIL_PLAYTIME_PLAYTIME_L]));
                p_ctrl->com.total_time  = (((uint32_t)p_mail->param[MAIL_PLAYTIME_TOTALTIME_H] << WORD_SHIFT) |
                                           ((uint32_t)p_mail->param[MAIL_PLAYTIME_TOTALTIME_M] << BYTE_SHIFT) |
                                           ((uint32_t)p_mail->param[MAIL_PLAYTIME_TOTALTIME_L]));
                break;
            case DSP_MAILID_PLAY_INFO:       /* Music information */
                ret = true;
                p_ctrl->com.track_id    = (((uint32_t)p_mail->param[MAIL_PLAYINFO_TRACK_H] << BYTE_SHIFT) |
                                            (uint32_t)p_mail->param[MAIL_PLAYINFO_TRACK_L]);
                p_ctrl->com.samp_freq   = (((uint32_t)p_mail->param[MAIL_PLAYINFO_SAMPFREQ_H] << WORD_SHIFT) |
                                           ((uint32_t)p_mail->param[MAIL_PLAYINFO_SAMPFREQ_M] << BYTE_SHIFT) |
                                           ((uint32_t)p_mail->param[MAIL_PLAYINFO_SAMPFREQ_L]));
                p_ctrl->com.channel     = p_mail->param[MAIL_PLAYINFO_CHANNEL];
                break;
            case DSP_MAILID_PLAY_MODE:       /* Repeat mode */
                ret = true;
                if ((int32_t)p_mail->param[MAIL_PLAYMODE_REPEAT] == true) {
                    p_ctrl->com.repeat_mode = true;
                } else {
                    p_ctrl->com.repeat_mode = false;
                }
                break;
            case DSP_MAILID_FILE_NAME:       /* File name */
                ret = true;
                (void) memcpy(&p_ctrl->com.file_name[0], 
                              &p_mail->param[MAIL_FILENAME_STR_START], 
                              sizeof(p_ctrl->com.file_name));
                p_ctrl->com.file_name[DSP_DISP_STR_MAX_LEN - 1] = '\0';
                break;
            case DSP_MAILID_HELP:            /* Help information */
                ret = true; 
                break;
            default:
                /* Unexpected cases : mail id was illegal. */
                ret = false;
                break;
        }
    }
    return ret;
}

/** Clears the one shot data in the control data of display module
 *
 *  @param p_ctrl Pointer to control data of display module.
 */
static void clear_one_shot_data(dsp_ctrl_t * const p_ctrl)
{
    if (p_ctrl != NULL) {
        if (p_ctrl->trm.edge_fin_inpt == true) {
            /* Clears data of input character string. */
            p_ctrl->trm.edge_fin_inpt = false;
            p_ctrl->trm.inpt_str[0] = '\0';
        }
    }
    return;
}
