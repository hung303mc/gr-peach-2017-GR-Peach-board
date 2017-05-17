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
#include "FATFileSystem.h"
#include "USBHostMSD.h"

#include "system.h"
#include "sys_scan_folder.h"
#include "decode.h"
#include "display.h"

#if defined(TARGET_RZ_A1H)
#include "usb_host_setting.h"
#else
#define USB_HOST_CH         (0)
#endif

/*--- Macro definition of mbed-rtos mail ---*/
#define MAIL_QUEUE_SIZE     (12)    /* Queue size */
#define MAIL_PARAM_NUM      (3)     /* Elements number of mail parameter array */

/* sys_mail_t */
#define MAIL_PARAM0         (0)     /* Index number of mail parameter array */
#define MAIL_PARAM1         (1)     /* Index number of mail parameter array */
#define MAIL_PARAM2         (2)     /* Index number of mail parameter array */

#define MAIL_PARAM_NON      (0u)    /* Value of unused element of mail parameter array */

/* mail_id = SYS_MAILID_KEYCODE */
#define MAIL_KEYCODE_CODE   (MAIL_PARAM0)   /* Key code */

/* mail_id = SYS_MAILID_PLAY_TIME */
#define MAIL_PLAYTIME_STAT  (MAIL_PARAM0)   /* Playback status */
#define MAIL_PLAYTIME_TIME  (MAIL_PARAM1)   /* Playback time */
#define MAIL_PLAYTIME_TOTAL (MAIL_PARAM2)   /* Total playback time */

/* mail_id = SYS_MAILID_DEC_OPEN_FIN */
#define MAIL_DECOPEN_RESULT (MAIL_PARAM0)   /* Result of the process */
#define MAIL_DECOPEN_FREQ   (MAIL_PARAM1)   /* Sampling rate in Hz of FLAC file */
#define MAIL_DECOPEN_CH     (MAIL_PARAM2)   /* Number of channel */

#define RECV_MAIL_TIMEOUT_MS    (10)

#define USB1_WAIT_TIME_MS       (5)
#define TRACK_ID_MIN            (0u)
#define TRACK_ID_ERR            (0xFFFFFFFFu)

#define PRINT_MSG_USB_CONNECT   "USB connection was detected."
#define PRINT_MSG_OPEN_ERR      "Could not play this file."
#define PRINT_MSG_DECODE_ERR    "This file format is not supported."

/*--- User defined types of mbed-rtos mail ---*/
typedef enum {
    SYS_MAILID_DUMMY = 0,
    SYS_MAILID_KEYCODE,         /* Notifies main thread of key code. */
    SYS_MAILID_PLAY_TIME,       /* Notifies main thread of playback time. */
    SYS_MAILID_DEC_OPEN_FIN,    /* Finished the opening process of Decode Thread. */
    SYS_MAILID_DEC_CLOSE_FIN,   /* Finished the closing process of Decode Thread. */
    SYS_MAILID_NUM
} SYS_MAIL_ID;

typedef struct {
    SYS_MAIL_ID     mail_id;
    uint32_t        param[MAIL_PARAM_NUM];
} sys_mail_t;

/*--- User defined types of main thread ---*/
/* Status of main thread */
typedef enum {
    SYS_ST_WAIT_USB_CONNECT = 0,/* Wait the USB connection */
    SYS_ST_STOP,                /* Stop the playback */
    SYS_ST_PLAY_PREPARE,        /* Preparation of the playback */
    SYS_ST_PLAY_PREPARE_REQ,    /* Holds the stop request until completion */
                                /* of the playback preparation */
    SYS_ST_PLAY,                /* Play */
    SYS_ST_PAUSE,               /* Pause */
    SYS_ST_STOP_PREPARE,        /* Preparation of the stop */
    SYS_ST_STOP_PREPARE_REQ,    /* Holds the playback request until completion */
                                /* of the stop preparation */
    SYS_ST_NUM
} SYS_STATE;

/* Event of the state transition */
typedef enum {
    SYS_EV_NON = 0,
    /* Notification of pushed key. */
    SYS_EV_KEY_STOP,            /* "STOP" key */
    SYS_EV_KEY_PLAY_PAUSE,      /* "PLAY/PAUSE" key */
    SYS_EV_KEY_NEXT,            /* "NEXT" key */
    SYS_EV_KEY_PREV,            /* "PREV" key */
    SYS_EV_KEY_PLAYINFO,        /* "PLAYINFO" key */
    SYS_EV_KEY_REPEAT,          /* "REPEAT" key */
    SYS_EV_KEY_HELP,            /* "HELP" key */
    /* Notification of decoder process */
    SYS_EV_DEC_OPEN_COMP,       /* Finished the opening process */
    SYS_EV_DEC_OPEN_COMP_ERR,   /* Finished the opening process (An error occured)*/
    SYS_EV_DEC_CLOSE_COMP,      /* Finished the closing process */
    /* Notification of the playback status */
    SYS_EV_STAT_STOP,           /* Stop */
    SYS_EV_STAT_PLAY,           /* Play */
    SYS_EV_STAT_PAUSE,          /* Pause */
    /* Notification of USB connection */
    SYS_EV_USB_CONNECT,         /* Connect */
    SYS_EV_USB_DISCONNECT,      /* Disconnect  */
    SYS_EV_NUM
} SYS_EVENT;

/* Control data of USB memory */
typedef struct {
    bool            usb_flag_detach;/* Detected the disconnection of USB memory */
} usb_ctrl_t;

/* The playback information of the playback file */
typedef struct {
    SYS_PlayStat    play_stat;      /* Playback status */
    bool            repeat_mode;    /* Repeat mode */
    uint32_t        track_id;       /* Number of the selected track */
    uint32_t        open_track_id;  /* Number of the track during the open processing */
    FILE            *p_file_handle; /* Handle of the track */
    uint32_t        play_time;      /* Playback start time */
    uint32_t        total_time;     /* Total playback time */
    uint32_t        sample_rate;    /* Sampling rate in Hz of FLAC file */
    uint32_t        channel_num;    /* Number of channel */
} play_info_t;

/* Control data of main thread */
typedef struct {
    usb_ctrl_t          usb_ctrl;
    fid_scan_folder_t   scan_data;
    play_info_t         play_info;
} sys_ctrl_t;

static Mail<sys_mail_t, MAIL_QUEUE_SIZE> mail_box;

static void open_callback(const bool result, const uint32_t sample_freq, 
                                                const uint32_t channel_num);
static void close_callback(void);
static void init_ctrl_data(sys_ctrl_t * const p_ctrl);
static SYS_EVENT decode_mail(play_info_t * const p_info, 
        const fid_scan_folder_t * const p_data, const SYS_MAIL_ID mail_id, 
        const uint32_t * const p_param);
static SYS_EVENT check_usb_event(const SYS_STATE stat, 
        const usb_ctrl_t * const p_ctrl, USBHostMSD * const p_msd, 
        FATFileSystem * const p_fs);
static SYS_STATE state_trans_proc(const SYS_STATE stat, 
                        const SYS_EVENT event, sys_ctrl_t * const p_ctrl);
static SYS_STATE state_trans_proc(const SYS_STATE stat, 
                        const SYS_EVENT event, sys_ctrl_t * const p_ctrl);
static SYS_STATE state_proc_wait_usb_connect(const SYS_STATE stat, 
                        const SYS_EVENT event, sys_ctrl_t * const p_ctrl);
static SYS_STATE state_proc_stop(const SYS_STATE stat, 
                        const SYS_EVENT event, sys_ctrl_t * const p_ctrl);
static SYS_STATE state_proc_play_prepare(const SYS_STATE stat, 
                        const SYS_EVENT event, sys_ctrl_t * const p_ctrl);
static SYS_STATE state_proc_play_prepare_req(const SYS_STATE stat, 
                        const SYS_EVENT event, sys_ctrl_t * const p_ctrl);
static SYS_STATE state_proc_play_pause(const SYS_STATE stat, 
                        const SYS_EVENT event, sys_ctrl_t * const p_ctrl);
static SYS_STATE state_proc_stop_prepare(const SYS_STATE stat, 
                        const SYS_EVENT event, sys_ctrl_t * const p_ctrl);
static SYS_STATE state_proc_stop_prepare_req(const SYS_STATE stat, 
                        const SYS_EVENT event, sys_ctrl_t * const p_ctrl);
static bool exe_scan_folder_proc(play_info_t * const p_info, 
                                            fid_scan_folder_t * const p_data);
static bool exe_open_proc(play_info_t * const p_info, 
                                            fid_scan_folder_t * const p_data);
static bool exe_play_proc(play_info_t * const p_info, 
                                    const fid_scan_folder_t * const p_data);
static bool exe_pause_on_proc(void);
static bool exe_pause_off_proc(void);
static bool exe_stop_proc(void);
static bool exe_close_proc(void);
static void exe_end_proc(play_info_t * const p_info);
static bool is_track_changed(const play_info_t * const p_info);
static void change_repeat_mode(play_info_t * const p_info);
static bool change_next_track(play_info_t * const p_info, 
                                    const fid_scan_folder_t * const p_data);
static bool change_prev_track(play_info_t * const p_info, 
                                    const fid_scan_folder_t * const p_data);
static void print_play_time(const play_info_t * const p_info);
static void print_play_info(const play_info_t * const p_info);
static void print_file_name(const play_info_t * const p_info, 
                                    const fid_scan_folder_t * const p_data);
static void print_help_info(void);
static uint32_t convert_track_id(const uint32_t trk_id);
static bool send_mail(const SYS_MAIL_ID mail_id, const uint32_t param0,
                            const uint32_t param1, const uint32_t param2);
static bool recv_mail(SYS_MAIL_ID * const p_mail_id, uint32_t * const p_param0, 
                        uint32_t * const p_param1, uint32_t * const p_param2);

void system_main(void)
{
    SYS_STATE           sys_stat;
    SYS_EVENT           sys_ev;
    bool                result;
    SYS_MAIL_ID         mail_type;
    uint32_t            mail_param[MAIL_PARAM_NUM];
    static sys_ctrl_t   sys_ctrl;
    static FATFileSystem fs(SYS_USB_MOUNT_NAME);
    static USBHostMSD msd;
#if (USB_HOST_CH == 1) /* Audio Shield USB1 */
    static DigitalOut   usb1en(P3_8);

    /* Audio Shield USB1 enable */
    usb1en.write(1);    /* Outputs high level */
    Thread::wait(USB1_WAIT_TIME_MS);
    usb1en.write(0);    /* Outputs low level */
#endif /* USB_HOST_CH */

    /* Initializes the control data of main thread. */
    init_ctrl_data(&sys_ctrl);
    sys_stat = SYS_ST_WAIT_USB_CONNECT;
    while (1) {
        sys_ev = check_usb_event(sys_stat, &sys_ctrl.usb_ctrl, &msd, &fs);
        if (sys_ev == SYS_EV_NON) {
            result = recv_mail(&mail_type, &mail_param[MAIL_PARAM0], 
                        &mail_param[MAIL_PARAM1], &mail_param[MAIL_PARAM2]);
            if (result == true) {
                sys_ev = decode_mail(&sys_ctrl.play_info, 
                                &sys_ctrl.scan_data, mail_type, mail_param);
            }
        }
        if (sys_ev != SYS_EV_NON) {
            sys_stat = state_trans_proc(sys_stat, sys_ev, &sys_ctrl);
        }
    }
}

bool sys_notify_key_input(const SYS_KeyCode key_code)
{
    bool    ret = false;

    ret = send_mail(SYS_MAILID_KEYCODE, (uint32_t)key_code, MAIL_PARAM_NON, MAIL_PARAM_NON);

    return ret;
}

bool sys_notify_play_time(const SYS_PlayStat play_stat, 
    const uint32_t play_time, const uint32_t total_time)
{
    bool    ret = false;

    ret = send_mail(SYS_MAILID_PLAY_TIME, (uint32_t)play_stat, play_time, total_time);

    return ret;
}

/** Callback function of Decode Thread
 *
 *  @param result Result of the process.
 *  @param sample_freq Sampling rate in Hz of FLAC file.
 *  @param channel_num Number of channel.
 */
static void open_callback(const bool result, const uint32_t sample_freq, 
                                                const uint32_t channel_num)
{
    (void) send_mail(SYS_MAILID_DEC_OPEN_FIN, (uint32_t)result, sample_freq, channel_num);
}

/** Callback function of Decode Thread
 *
 */
static void close_callback(void)
{
    (void) send_mail(SYS_MAILID_DEC_CLOSE_FIN, MAIL_PARAM_NON, MAIL_PARAM_NON, MAIL_PARAM_NON);
}

/** Initialises the control data of main thread
 *
 *  @param p_ctrl Pointer to the control data of main thread
 */
static void init_ctrl_data(sys_ctrl_t * const p_ctrl)
{
    if (p_ctrl != NULL) {
        /* Initialises the control data of USB memory. */
        p_ctrl->usb_ctrl.usb_flag_detach = false;
        /* Initialises the information of folder scan. */
        fid_init(&p_ctrl->scan_data);
        /* Initialises the playback information of the playback file */
        p_ctrl->play_info.play_stat = SYS_PLAYSTAT_STOP;
        p_ctrl->play_info.repeat_mode = true;
        p_ctrl->play_info.track_id = TRACK_ID_MIN;
        p_ctrl->play_info.open_track_id = TRACK_ID_ERR;
        p_ctrl->play_info.p_file_handle = NULL;
        p_ctrl->play_info.play_time = 0u;
        p_ctrl->play_info.total_time = 0u;
        p_ctrl->play_info.sample_rate = 0u;
        p_ctrl->play_info.channel_num = 0u;
    }
}

/** Converts the code of SYS_MAIL_ID into the code of SYS_EVENT
 *
 *  @param p_info Pointer to the playback information of the playback file
 *  @param p_data Pointer to the control data of folder scan
 *  @param mail_id The event code of SYS_MAIL_ID
 *  @param p_param Pointer to the parameter of the mail
 *
 *  @returns 
 *    The event code of SYS_EVENT
 */
static SYS_EVENT decode_mail(play_info_t * const p_info, 
        const fid_scan_folder_t * const p_data, const SYS_MAIL_ID mail_id, 
        const uint32_t * const p_param)
{
    SYS_EVENT       ret = SYS_EV_NON;

    if ((p_info != NULL) && (p_data != NULL) && (p_param != NULL)) {
        switch (mail_id) {
            case SYS_MAILID_KEYCODE:
                switch (p_param[MAIL_KEYCODE_CODE]) {
                    case SYS_KEYCODE_STOP:
                        ret = SYS_EV_KEY_STOP;
                        break;
                    case SYS_KEYCODE_PLAYPAUSE:
                        ret = SYS_EV_KEY_PLAY_PAUSE;
                        break;
                    case SYS_KEYCODE_NEXT:
                        ret = SYS_EV_KEY_NEXT;
                        break;
                    case SYS_KEYCODE_PREV:
                        ret = SYS_EV_KEY_PREV;
                        break;
                    case SYS_KEYCODE_PLAYINFO:
                        ret = SYS_EV_KEY_PLAYINFO;
                        break;
                    case SYS_KEYCODE_REPEAT:
                        ret = SYS_EV_KEY_REPEAT;
                        break;
                    case SYS_KEYCODE_HELP:
                        ret = SYS_EV_KEY_HELP;
                        break;
                    default:
                        /* Unexpected cases : This is fail-safe processing. */
                        ret = SYS_EV_NON;
                        break;
                }
                break;
            case SYS_MAILID_PLAY_TIME:
                switch (p_param[MAIL_PLAYTIME_STAT]) {
                    case SYS_PLAYSTAT_STOP:
                        ret = SYS_EV_STAT_STOP;
                        break;
                    case SYS_PLAYSTAT_PLAY:
                        ret = SYS_EV_STAT_PLAY;
                        break;
                    case SYS_PLAYSTAT_PAUSE:
                        ret = SYS_EV_STAT_PAUSE;
                        break;
                    default:
                        ret = SYS_EV_NON;
                        break;
                }
                /* Is the playback status correct? */
                if (ret != SYS_EV_NON) {
                    p_info->play_stat  = (SYS_PlayStat)p_param[MAIL_PLAYTIME_STAT];
                    p_info->play_time  = p_param[MAIL_PLAYTIME_TIME];
                    p_info->total_time = p_param[MAIL_PLAYTIME_TOTAL];
                } else {
                    /* Unexpected cases : This is fail-safe processing. */
                    p_info->play_stat  = SYS_PLAYSTAT_STOP;
                    p_info->play_time  = 0u;
                    p_info->total_time = 0u;
                }
                break;
            case SYS_MAILID_DEC_OPEN_FIN:
                if ((int32_t)p_param[MAIL_DECOPEN_RESULT] == true) {
                    ret = SYS_EV_DEC_OPEN_COMP;
                    p_info->sample_rate = p_param[MAIL_DECOPEN_FREQ];
                    p_info->channel_num = p_param[MAIL_DECOPEN_CH];
                } else {
                    /* The opening of the decoder was failure. */
                    ret = SYS_EV_DEC_OPEN_COMP_ERR;
                    p_info->sample_rate = 0u;
                    p_info->channel_num = 0u;
                    /* Tries to open the next file. */
                    print_play_time(p_info);
                    p_info->open_track_id = TRACK_ID_ERR;
                    (void) change_next_track(p_info, p_data);
                }
                break;
            case SYS_MAILID_DEC_CLOSE_FIN:
                ret = SYS_EV_DEC_CLOSE_COMP;
                p_info->play_time  = 0u;
                p_info->total_time = 0u;
                break;
            default:
                /* Unexpected cases : This is fail-safe processing. */
                ret = SYS_EV_NON;
                break;
        }
    }
    return ret;
}

/** Checks the event of USB connection
 *
 *  @param stat Status of main thread
 *  @param p_ctrl Pointer to the control data of USB memory
 *  @param p_msd Pointer to the class object of USBHostMSD
 *  @param P_fs Pointer to the class oblect of FATFileSystem
 *
 *  @returns 
 *    The event code of SYS_EVENT
 */
static SYS_EVENT check_usb_event(const SYS_STATE stat, 
        const usb_ctrl_t * const p_ctrl, 
        USBHostMSD * const p_msd, FATFileSystem * const p_fs)
{
    SYS_EVENT       ret = SYS_EV_NON;
    int             iRet;
    bool            result;

    if ((p_ctrl != NULL) && (p_msd != NULL)) {
        if (stat == SYS_ST_WAIT_USB_CONNECT) {
            /* Mounts the FAT filesystem again. */
            /* Because the connecting USB memory is changed. */
            result = p_msd->connect();
            if (result == true) {
                iRet = p_fs->unmount();
                iRet = p_fs->mount(p_msd);
                ret = SYS_EV_USB_CONNECT;
            }
        } else if (p_ctrl->usb_flag_detach != true) {
            result = p_msd->connected();
            if (result != true) {
                ret = SYS_EV_USB_DISCONNECT;
            }
        } else {
            /* DO NOTHING */
        }
    }
    return ret;
}

/** Executes the state transition processing
 *
 *  @param stat Status of main thread
 *  @param event Event code of main thread
 *  @param p_ctrl Pointer to the control data of main thread
 *
 *  @returns 
 *    Next status of main thread
 */
static SYS_STATE state_trans_proc(const SYS_STATE stat, const SYS_EVENT event, sys_ctrl_t * const p_ctrl)
{
    SYS_STATE       next_stat = stat;

    if (p_ctrl != NULL) {
        /* State transition processing */
        switch (stat) {
            case SYS_ST_WAIT_USB_CONNECT:   /* Wait the USB connection */
                next_stat = state_proc_wait_usb_connect(stat, event, p_ctrl);
                break;
            case SYS_ST_STOP:               /* Stop the playback */
                next_stat = state_proc_stop(stat, event, p_ctrl);
                break;
            case SYS_ST_PLAY_PREPARE:       /* Preparation of the playback */
                next_stat = state_proc_play_prepare(stat, event, p_ctrl);
                break;
            case SYS_ST_PLAY_PREPARE_REQ:   /* Holds the stop request until completion */
                                            /* of the playback preparation */
                next_stat = state_proc_play_prepare_req(stat, event, p_ctrl);
                break;
            case SYS_ST_PLAY:               /* Play */
            case SYS_ST_PAUSE:              /* Pause */
                next_stat = state_proc_play_pause(stat, event, p_ctrl);
                break;
            case SYS_ST_STOP_PREPARE:       /* Preparation of the stop */
                next_stat = state_proc_stop_prepare(stat, event, p_ctrl);
                break;
            case SYS_ST_STOP_PREPARE_REQ:   /* Holds the playback request until completion */
                                            /* of the stop preparation */
                next_stat = state_proc_stop_prepare_req(stat, event, p_ctrl);
                break;
            default:
                /* Unexpected cases : This is fail-safe processing. */
                next_stat = SYS_ST_WAIT_USB_CONNECT;
                break;
        }
    }
    return next_stat;
}

/** Executes the state transition processing in the state of SYS_ST_WAIT_USB_CONNECT.
 *
 *  @param stat Status of main thread
 *  @param event Event code of main thread
 *  @param p_ctrl Pointer to the control data of main thread
 *
 *  @returns 
 *    Next status of main thread
 */
static SYS_STATE state_proc_wait_usb_connect(const SYS_STATE stat, const SYS_EVENT event, sys_ctrl_t * const p_ctrl)
{
    SYS_STATE       next_stat = stat;

    if (p_ctrl != NULL) {
        if (event == SYS_EV_USB_CONNECT) {
            p_ctrl->usb_ctrl.usb_flag_detach = false;
            (void) exe_scan_folder_proc(&p_ctrl->play_info, &p_ctrl->scan_data);
            (void) dsp_notify_print_string(PRINT_MSG_USB_CONNECT);
            next_stat = SYS_ST_STOP;
        } else if (event == SYS_EV_KEY_HELP) {
            print_help_info();
        } else {
            /* DO NOTHING */
        }
    }
    return next_stat;
}

/** Executes the state transition processing in the state of SYS_ST_STOP.
 *
 *  @param stat Status of main thread
 *  @param event Event code of main thread
 *  @param p_ctrl Pointer to the control data of main thread
 *
 *  @returns 
 *    Next status of main thread
 */
static SYS_STATE state_proc_stop(const SYS_STATE stat, const SYS_EVENT event, sys_ctrl_t * const p_ctrl)
{
    SYS_STATE       next_stat = stat;
    bool            result;

    if (p_ctrl != NULL) {
        switch (event) {
            case SYS_EV_KEY_PLAY_PAUSE:
                print_file_name(&p_ctrl->play_info, &p_ctrl->scan_data);
                result = exe_open_proc(&p_ctrl->play_info, &p_ctrl->scan_data);
                if (result == true) {
                    next_stat = SYS_ST_PLAY_PREPARE;
                }
                break;
            case SYS_EV_KEY_REPEAT:
                change_repeat_mode(&p_ctrl->play_info);
                break;
            case SYS_EV_KEY_HELP:
                print_help_info();
                break;
            case SYS_EV_USB_DISCONNECT:
                p_ctrl->usb_ctrl.usb_flag_detach = true;
                next_stat = SYS_ST_WAIT_USB_CONNECT;
                break;
            default:
                /* Does not change the state. There is not the action. */
                next_stat = stat;
                break;
        }
    }
    return next_stat;
}

/** Executes the state transition processing in the state of SYS_ST_PLAY_PREPARE.
 *
 *  @param stat Status of main thread
 *  @param event Event code of main thread
 *  @param p_ctrl Pointer to the control data of main thread
 *
 *  @returns 
 *    Next status of main thread
 */
static SYS_STATE state_proc_play_prepare(const SYS_STATE stat, const SYS_EVENT event, sys_ctrl_t * const p_ctrl)
{
    SYS_STATE       next_stat = stat;
    bool            result;

    if (p_ctrl != NULL) {
        switch (event) {
            case SYS_EV_KEY_STOP:
                next_stat = SYS_ST_PLAY_PREPARE_REQ;
                break;
            case SYS_EV_KEY_NEXT:
                result = change_next_track(&p_ctrl->play_info, &p_ctrl->scan_data);
                if (result != true) {
                    /* Selected track was out of the playback range. */
                    next_stat = SYS_ST_PLAY_PREPARE_REQ;
                }
                break;
            case SYS_EV_KEY_PREV:
                result = change_prev_track(&p_ctrl->play_info, &p_ctrl->scan_data);
                if (result != true) {
                    next_stat = SYS_ST_PLAY_PREPARE_REQ;
                }
                break;
            case SYS_EV_KEY_REPEAT:
                change_repeat_mode(&p_ctrl->play_info);
                break;
            case SYS_EV_KEY_HELP:
                print_help_info();
                break;
            case SYS_EV_DEC_OPEN_COMP:
                result = is_track_changed(&p_ctrl->play_info);
                if (result == true) {
                    /* Track was changed during the waiting of callback. */
                    print_play_time(&p_ctrl->play_info);
                    result = exe_close_proc();
                    if (result == true) {
                        next_stat = SYS_ST_STOP_PREPARE_REQ;
                    } else {
                        /* Unexpected cases : This is fail-safe processing. */
                        exe_end_proc(&p_ctrl->play_info);
                        next_stat = SYS_ST_STOP;
                    }
                } else {
                    print_play_info(&p_ctrl->play_info);
                    result = exe_play_proc(&p_ctrl->play_info, &p_ctrl->scan_data);
                    if (result == true) {
                        next_stat = SYS_ST_PLAY;
                    } else {
                        result = exe_close_proc();
                        if (result == true) {
                            next_stat = SYS_ST_STOP_PREPARE;
                        } else {
                            /* Unexpected cases : This is fail-safe processing. */
                            exe_end_proc(&p_ctrl->play_info);
                            next_stat = SYS_ST_STOP;
                        }
                    }
                }
                break;
            case SYS_EV_DEC_OPEN_COMP_ERR:
                /* Output error message to PC */
                (void) dsp_notify_print_string(PRINT_MSG_DECODE_ERR);
                exe_end_proc(&p_ctrl->play_info);
                next_stat = SYS_ST_STOP;
                break;
            case SYS_EV_USB_DISCONNECT:
                p_ctrl->usb_ctrl.usb_flag_detach = true;
                next_stat = SYS_ST_PLAY_PREPARE_REQ;
                break;
            default:
                /* Does not change the state. There is not the action. */
                next_stat = stat;
                break;
        }
    }
    return next_stat;
}

/** Executes the state transition processing in the state of SYS_ST_PLAY_PREPARE_REQ.
 *
 *  @param stat Status of main thread
 *  @param event Event code of main thread
 *  @param p_ctrl Pointer to the control data of main thread
 *
 *  @returns 
 *    Next status of main thread
 */
static SYS_STATE state_proc_play_prepare_req(const SYS_STATE stat, const SYS_EVENT event, sys_ctrl_t * const p_ctrl)
{
    SYS_STATE       next_stat = stat;
    bool            result;

    if (p_ctrl != NULL) {
        switch (event) {
            case SYS_EV_KEY_REPEAT:
                change_repeat_mode(&p_ctrl->play_info);
                break;
            case SYS_EV_KEY_HELP:
                print_help_info();
                break;
            case SYS_EV_DEC_OPEN_COMP:
                print_play_time(&p_ctrl->play_info);
                result = exe_close_proc();
                if (result == true) {
                    next_stat = SYS_ST_STOP_PREPARE;
                } else {
                    /* Unexpected cases : This is fail-safe processing. */
                    exe_end_proc(&p_ctrl->play_info);
                    if (p_ctrl->usb_ctrl.usb_flag_detach == true) {
                        next_stat = SYS_ST_WAIT_USB_CONNECT;
                    } else {
                        next_stat = SYS_ST_STOP;
                    }
                }
                break;
            case SYS_EV_DEC_OPEN_COMP_ERR:
                /* Output error message to PC */
                (void) dsp_notify_print_string(PRINT_MSG_DECODE_ERR);
                exe_end_proc(&p_ctrl->play_info);
                if (p_ctrl->usb_ctrl.usb_flag_detach == true) {
                    next_stat = SYS_ST_WAIT_USB_CONNECT;
                } else {
                    next_stat = SYS_ST_STOP;
                }
                break;
            case SYS_EV_USB_DISCONNECT:
                p_ctrl->usb_ctrl.usb_flag_detach = true;
                break;
            default:
                /* Does not change the state. There is not the action. */
                next_stat = stat;
                break;
        }
    }
    return next_stat;
}

/** Executes the state transition processing in the state of SYS_ST_PLAY / SYS_ST_PAUSE.
 *
 *  @param stat Status of main thread
 *  @param event Event code of main thread
 *  @param p_ctrl Pointer to the control data of main thread
 *
 *  @returns 
 *    Next status of main thread
 */
static SYS_STATE state_proc_play_pause(const SYS_STATE stat, const SYS_EVENT event, sys_ctrl_t * const p_ctrl)
{
    SYS_STATE       next_stat = stat;
    bool            result;

    if (p_ctrl != NULL) {
        switch (event) {
            case SYS_EV_KEY_STOP:
                result = exe_stop_proc();
                if (result == true) {
                    next_stat = SYS_ST_STOP_PREPARE;
                }
                break;
            case SYS_EV_KEY_PLAY_PAUSE:
                if (stat == SYS_ST_PLAY) {
                    (void) exe_pause_on_proc();
                } else {
                    (void) exe_pause_off_proc();
                }
                break;
            case SYS_EV_KEY_NEXT:
            case SYS_EV_KEY_PREV:
                result = exe_stop_proc();
                if (result == true) {
                    if (event == SYS_EV_KEY_PREV) {
                        result = change_prev_track(&p_ctrl->play_info, &p_ctrl->scan_data);
                    } else {
                        result = change_next_track(&p_ctrl->play_info, &p_ctrl->scan_data);
                    }
                    if (result == true) {
                        next_stat = SYS_ST_STOP_PREPARE_REQ;
                    } else {
                        next_stat = SYS_ST_STOP_PREPARE;
                    }
                }
                break;
            case SYS_EV_KEY_PLAYINFO:
                print_play_info(&p_ctrl->play_info);
                break;
            case SYS_EV_KEY_REPEAT:
                change_repeat_mode(&p_ctrl->play_info);
                break;
            case SYS_EV_KEY_HELP:
                print_help_info();
                break;
            case SYS_EV_STAT_STOP:
                print_play_time(&p_ctrl->play_info);
                result = exe_close_proc();
                if (result == true) {
                    result = change_next_track(&p_ctrl->play_info, &p_ctrl->scan_data);
                    if (result == true) {
                        next_stat = SYS_ST_STOP_PREPARE_REQ;
                    } else {
                        next_stat = SYS_ST_STOP_PREPARE;
                    }
                } else {
                    /* Unexpected cases : This is fail-safe processing. */
                    exe_end_proc(&p_ctrl->play_info);
                    if (p_ctrl->usb_ctrl.usb_flag_detach == true) {
                        next_stat = SYS_ST_WAIT_USB_CONNECT;
                    } else {
                        next_stat = SYS_ST_STOP;
                    }
                }
                break;
            case SYS_EV_STAT_PLAY:
                print_play_time(&p_ctrl->play_info);
                next_stat = SYS_ST_PLAY;
                break;
            case SYS_EV_STAT_PAUSE:
                print_play_time(&p_ctrl->play_info);
                next_stat = SYS_ST_PAUSE;
                break;
            case SYS_EV_USB_DISCONNECT:
                p_ctrl->usb_ctrl.usb_flag_detach = true;
                result = exe_stop_proc();
                if (result == true) {
                    next_stat = SYS_ST_STOP_PREPARE;
                } else {
                    /* Unexpected cases : This is fail-safe processing. */
                    /* In this case, main thread wait the notification of the stop status. */
                    /* An error will occur by the disconnection of USB memory.  */
                }
                break;
            default:
                /* Does not change the state. There is not the action. */
                next_stat = stat;
                break;
        }
    }
    return next_stat;
}

/** Executes the state transition processing in the state of SYS_ST_STOP_PREPARE.
 *
 *  @param stat Status of main thread
 *  @param event Event code of main thread
 *  @param p_ctrl Pointer to the control data of main thread
 *
 *  @returns 
 *    Next status of main thread
 */
static SYS_STATE state_proc_stop_prepare(const SYS_STATE stat, const SYS_EVENT event, sys_ctrl_t * const p_ctrl)
{
    SYS_STATE       next_stat = stat;
    bool            result;

    if (p_ctrl != NULL) {
        switch (event) {
            case SYS_EV_KEY_REPEAT:
                change_repeat_mode(&p_ctrl->play_info);
                break;
            case SYS_EV_KEY_HELP:
                print_help_info();
                break;
            case SYS_EV_DEC_CLOSE_COMP:
                exe_end_proc(&p_ctrl->play_info);
                if (p_ctrl->usb_ctrl.usb_flag_detach == true) {
                    next_stat = SYS_ST_WAIT_USB_CONNECT;
                } else {
                    next_stat = SYS_ST_STOP;
                }
                break;
            case SYS_EV_STAT_STOP:
                result = exe_close_proc();
                if (result != true) {
                    /* Unexpected cases : This is fail-safe processing. */
                    exe_end_proc(&p_ctrl->play_info);
                    if (p_ctrl->usb_ctrl.usb_flag_detach == true) {
                        next_stat = SYS_ST_WAIT_USB_CONNECT;
                    } else {
                        next_stat = SYS_ST_STOP;
                    }
                }
                break;
            case SYS_EV_STAT_PLAY:
            case SYS_EV_STAT_PAUSE:
                break;
            case SYS_EV_USB_DISCONNECT:
                p_ctrl->usb_ctrl.usb_flag_detach = true;
                break;
            default:
                /* Does not change the state. There is not the action. */
                next_stat = stat;
                break;
        }
    }
    return next_stat;
}

/** Executes the state transition processing in the state of SYS_ST_STOP_PREPARE_REQ.
 *
 *  @param stat Status of main thread
 *  @param event Event code of main thread
 *  @param p_ctrl Pointer to the control data of main thread
 *
 *  @returns 
 *    Next status of main thread
 */
static SYS_STATE state_proc_stop_prepare_req(const SYS_STATE stat, const SYS_EVENT event, sys_ctrl_t * const p_ctrl)
{
    SYS_STATE       next_stat = stat;
    bool            result;

    if (p_ctrl != NULL) {
        switch (event) {
            case SYS_EV_KEY_STOP:
                next_stat = SYS_ST_STOP_PREPARE;
                break;
            case SYS_EV_KEY_NEXT:
                result = change_next_track(&p_ctrl->play_info, &p_ctrl->scan_data);
                if (result != true) {
                    /* Selected track was out of the playback range. */
                    next_stat = SYS_ST_STOP_PREPARE;
                }
                break;
            case SYS_EV_KEY_PREV:
                result = change_prev_track(&p_ctrl->play_info, &p_ctrl->scan_data);
                if (result != true) {
                    next_stat = SYS_ST_STOP_PREPARE;
                }
                break;
            case SYS_EV_KEY_REPEAT:
                change_repeat_mode(&p_ctrl->play_info);
                break;
            case SYS_EV_KEY_HELP:
                print_help_info();
                break;
            case SYS_EV_DEC_CLOSE_COMP:
                exe_end_proc(&p_ctrl->play_info);
                print_file_name(&p_ctrl->play_info, &p_ctrl->scan_data);
                result = exe_open_proc(&p_ctrl->play_info, &p_ctrl->scan_data);
                if (result == true) {
                    next_stat = SYS_ST_PLAY_PREPARE;
                } else {
                    next_stat = SYS_ST_STOP;
                }
                break;
            case SYS_EV_STAT_STOP:
                result = exe_close_proc();
                if (result != true) {
                    /* Unexpected cases : This is fail-safe processing. */
                    exe_end_proc(&p_ctrl->play_info);
                    next_stat = SYS_ST_STOP;
                }
                break;
            case SYS_EV_STAT_PLAY:
            case SYS_EV_STAT_PAUSE:
                break;
            case SYS_EV_USB_DISCONNECT:
                p_ctrl->usb_ctrl.usb_flag_detach = true;
                next_stat = SYS_ST_STOP_PREPARE;
                break;
            default:
                /* Does not change the state. There is not the action. */
                next_stat = stat;
                break;
        }
    }
    return next_stat;
}

/** Executes the scan of the folder structure
 *
 *  @param p_info Pointer to the playback information of the playback file
 *  @param p_data Pointer to the control data of folder scan
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool exe_scan_folder_proc(play_info_t * const p_info, fid_scan_folder_t * const p_data)
{
    bool        ret = false;

    if ((p_info != NULL) && (p_data != NULL)) {
        (void) fid_scan_folder_struct(p_data);
        p_info->track_id = TRACK_ID_MIN;
        ret = true;
    }
    return ret;
}

/** Executes the opening process
 *
 *  @param p_info Pointer to the playback information of the playback file
 *  @param p_data Pointer to the control data of folder scan
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool exe_open_proc(play_info_t * const p_info, fid_scan_folder_t * const p_data)
{
    bool        ret = false;
    bool        result;
    FILE        *fp;
    uint32_t    total_trk;

    if ((p_info != NULL) && (p_data != NULL)) {
        total_trk = fid_get_total_track(p_data);
        if (p_info->track_id < total_trk) {
            fp = fid_open_track(p_data, p_info->track_id);
            if (fp != NULL) {
                result = dec_open(fp, &open_callback);
                if (result == true) {
                    /* Executes fid_close_track() in exe_end_proc(). */
                    p_info->p_file_handle = fp;
                    p_info->open_track_id = p_info->track_id;
                    ret = true;
                } else {
                    /* The opening of the decoder was failure. */
                    fid_close_track(fp);
                    p_info->p_file_handle = NULL;
                }
            }
            if (ret != true) {
                print_play_time(p_info);
                (void) dsp_notify_print_string(PRINT_MSG_OPEN_ERR);
                /* Tries to open the next file. */
                p_info->open_track_id = TRACK_ID_ERR;
                (void) change_next_track(p_info, p_data);
            }
        }
    }
    return ret;
}

/** Executes the starting process of the playback
 *
 *  @param p_info Pointer to the playback information of the playback file
 *  @param p_data Pointer to the control data of folder scan
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool exe_play_proc(play_info_t * const p_info, const fid_scan_folder_t * const p_data)
{
    bool        ret = false;
    bool        result;

    if (p_info != NULL) {
        result = dec_play();
        if (result == true) {
            ret = true;
        } else {
            /* The starting of the playback was failure. */
            print_play_time(p_info);
            /* Tries to open the next file. */
            p_info->open_track_id = TRACK_ID_ERR;
            (void) change_next_track(p_info, p_data);
        }
    }
    return ret;
}

/** Executes the starting process of the pause
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool exe_pause_on_proc(void)
{
    bool    ret;

    ret = dec_pause_on();
    return ret;
}

/** Executes the cancel process of the pause
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool exe_pause_off_proc(void)
{
    bool    ret;

    ret = dec_pause_off();
    return ret;
}

/** Executes the stop process of the playback
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool exe_stop_proc(void)
{
    bool    ret;

    ret = dec_stop();
    return ret;
}

/** Executes the closing process
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool exe_close_proc(void)
{
    bool    ret;

    ret = dec_close(&close_callback);
    return ret;
}

/** Executes the end process
 *
 *  @param p_info Pointer to the playback information of the playback file
 */
static void exe_end_proc(play_info_t * const p_info)
{
    if (p_info != NULL) {
        fid_close_track(p_info->p_file_handle);
        p_info->p_file_handle = NULL;
        p_info->open_track_id = TRACK_ID_ERR;
    }
}

/** Checks the track change
 *
 *  @param p_info Pointer to the playback information of the playback file
 *
 *  @returns 
 *    Results of check.
 */
static bool is_track_changed(const play_info_t * const p_info)
{
    bool        ret = false;

    if (p_info != NULL) {
        if (p_info->track_id != p_info->open_track_id) {
            ret = true;
        }
    }
    return ret;
}

/** Changes the repeat mode
 *
 *  @param p_info Pointer to the playback information of the playback file
 */
static void change_repeat_mode(play_info_t * const p_info)
{
    if (p_info != NULL) {
        if (p_info->repeat_mode == true) {
            p_info->repeat_mode = false;
        } else {
            p_info->repeat_mode = true;
        }
        (void) dsp_notify_play_mode(p_info->repeat_mode);
    }
}

/** Changes the next track
 *
 *  @param p_info Pointer to the playback information of the playback file
 *  @param p_data Pointer to the control data of folder scan
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool change_next_track(play_info_t * const p_info, const fid_scan_folder_t * const p_data)
{
    bool        ret = false;
    uint32_t    next_trk;
    uint32_t    total_trk;

    if ((p_info != NULL) && (p_data != NULL)) {
        next_trk = p_info->track_id + 1u;
        total_trk = fid_get_total_track(p_data);
        if (next_trk < total_trk) {
            ret = true;
        } else {
            next_trk = 0;
            if (p_info->repeat_mode == true) {
                ret = true;
            }
        }
        p_info->track_id = next_trk;
    }
    return ret;
}

/** Changes the previous track
 *
 *  @param p_info Pointer to the playback information of the playback file
 *  @param p_data Pointer to the control data of folder scan
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool change_prev_track(play_info_t * const p_info, const fid_scan_folder_t * const p_data)
{
    bool        ret = false;
    uint32_t    prev_trk;
    uint32_t    total_trk;

    if ((p_info != NULL) && (p_data != NULL)) {
        prev_trk = p_info->track_id - 1u;
        total_trk = fid_get_total_track(p_data);
        if (prev_trk < total_trk) {
            ret = true;
        } else {
            if (p_info->repeat_mode == true) {
                if (total_trk > 0u) {
                    ret = true;
                    prev_trk = total_trk - 1u;
                } else {
                    prev_trk = 0u;
                }
            } else {
                prev_trk = 0u;
            }
        }
        p_info->track_id = prev_trk;
    }
    return ret;
}
/** Prints the playback information of the playback file
 *
 *  @param p_info Pointer to the playback information of the playback file
 */
static void print_play_info(const play_info_t * const p_info)
{
    uint32_t        trk_id;

    if (p_info != NULL) {
        trk_id = convert_track_id(p_info->track_id);
        (void) dsp_notify_play_info(trk_id, p_info->sample_rate, p_info->channel_num);
    }
}

/** Prints the information of the playback time
 *
 *  @param p_info Pointer to the playback information of the playback file
 */
static void print_play_time(const play_info_t * const p_info)
{
    uint32_t        trk_id;

    if (p_info != NULL) {
        trk_id = convert_track_id(p_info->track_id);
        (void) dsp_notify_play_time(p_info->play_stat, trk_id, 
                                    p_info->play_time, p_info->total_time);
    }
}

/** Prints the file name of the playback file
 *
 *  @param p_info Pointer to the playback information of the playback file
 *  @param p_data Pointer to the control data of folder scan
 */
static void print_file_name(const play_info_t * const p_info, const fid_scan_folder_t * const p_data)
{
    const char_t    *p_path;
    uint32_t        trk_id;
    uint32_t        trk_total;

    if ((p_info != NULL) && (p_data != NULL)) {
        trk_id = p_info->track_id;
        trk_total = fid_get_total_track(p_data);
        if (trk_id < trk_total) {
            p_path = fid_get_track_name(p_data, trk_id);
            (void) dsp_notify_file_name(p_path);
        }
    }
}

/** Prints the command help information
 *
 */
static void print_help_info(void)
{
    (void) dsp_req_help();
}

/** Converts the track ID of main thread into the track ID of display thread
 *
 *  @param trk_id Track ID of main thread
 *
 *  @returns 
 *    Track ID of display thread
 */
static uint32_t convert_track_id(const uint32_t trk_id)
{
    uint32_t        ret = 0u;

    /* Track ID of main thread begins with 0. */
    /* Converts the track ID of main thread into the track ID */
    /* of the display thread beginning with 1. */
    if (trk_id < SYS_MAX_TRACK_NUM) {
        ret = trk_id + 1u;
    }
    return ret;
}

/** Sends the mail to main thread
 *
 *  @param mail_id Mail ID
 *  @param param0 Parameter 0 of this mail
 *  @param param1 Parameter 1 of this mail
 *  @param param2 Parameter 2 of this mail
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool send_mail(const SYS_MAIL_ID mail_id, const uint32_t param0,
                            const uint32_t param1, const uint32_t param2)
{
    bool            ret = false;
    osStatus        stat;
    sys_mail_t      * const p_mail = mail_box.alloc();

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

/** Receives the mail to main thread
 *
 *  @param p_mail_id Pointer to the variable to store the mail ID
 *  @param p_param0 Pointer to the variable to store the parameter 0 of this mail
 *  @param p_param1 Pointer to the variable to store the parameter 1 of this mail
 *  @param p_param2 Pointer to the variable to store the parameter 2 of this mail
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool recv_mail(SYS_MAIL_ID * const p_mail_id, uint32_t * const p_param0, 
                        uint32_t * const p_param1, uint32_t * const p_param2)
{
    bool            ret = false;
    osEvent         evt;
    sys_mail_t      *p_mail;

    if ((p_mail_id != NULL) && (p_param0 != NULL) && 
        (p_param1 != NULL) && (p_param2 != NULL)) {
        evt = mail_box.get(RECV_MAIL_TIMEOUT_MS);
        if (evt.status == osEventMail) {
            p_mail = (sys_mail_t *)evt.value.p;
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
