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

#ifndef SYSTEM_H
#define SYSTEM_H

#include "r_typedefs.h"
#include "USBHostMSD.h"

/*--- Macro definition of folder scan in USB memory ---*/
#define SYS_MAX_FOLDER_NUM      (99u)       /* Supported number of folders */
#define SYS_MAX_TRACK_NUM       (999u)      /* Supported number of tracks  */
#define SYS_MAX_FOLDER_DEPTH    (8u)        /* Supported folder levels */
#define SYS_MAX_NAME_LENGTH     (NAME_MAX)  /* Maximum length of track name and folder name */
#define SYS_MAX_PATH_LENGTH     (511)       /* Maximum length of the full path */

/* It is the name to mount the file system of the USBHostMSD class. */
#define SYS_USB_MOUNT_NAME      "usb"

/*--- User defined types of main thread ---*/
/* Key code */
typedef enum {
    SYS_KEYCODE_NON = 0,
    SYS_KEYCODE_STOP,           /* Stop */
    SYS_KEYCODE_PLAYPAUSE,      /* Play / Pause */
    SYS_KEYCODE_NEXT,           /* Next track */
    SYS_KEYCODE_PREV,           /* Previous track */
    SYS_KEYCODE_PLAYINFO,       /* Play info */
    SYS_KEYCODE_REPEAT,         /* Repeat */
    SYS_KEYCODE_HELP,           /* Help */
    SYS_KEYCODE_NUM
} SYS_KeyCode;

/* Playback status */
typedef enum {
    SYS_PLAYSTAT_STOP = 0,      /* Stop */
    SYS_PLAYSTAT_PLAY,          /* Play */
    SYS_PLAYSTAT_PAUSE,         /* Pause */
    SYS_PLAYSTAT_NUM
} SYS_PlayStat;

/** Main Thread
 *
 *  @param argument Pointer to the thread function as start argument.
 */
void system_main(void);

/** Notifies the main thread of the key input information.
 *
 *  @param key_code key code
 *                    Stop : SYS_KEYCODE_STOP
 *                    Play/pause : SYS_KEYCODE_PLAYPAUSE
 *                    Play next song : SYS_KEYCODE_NEXT
 *                    Play previous song : SYS_KEYCODE_PREV
 *                    Show song information : SYS_KEYCODE_PLAYINFO
 *                    Switch repeat mode : SYS_KEYCODE_REPEAT
 *                    Show help message: SYS_KEYCODE_HELP
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool sys_notify_key_input(const SYS_KeyCode key_code);

/** Notifies the main thread of the play time, total play time, and play state.
 *
 *  @param play_stat Playback state
 *                     Stopped : SYS_PLAYSTAT_STOP
 *                     Playing : SYS_PLAYSTAT_PLAY
 *                     Paused : SYS_PLAYSTAT_PAUSE
 *  @param play_time Playback time (in seconds)
 *                     0 to 359999
 *                     * 0 hour, 0 minute, 0 second to 99 hours, 59 minutes, 59 seconds
 *  @param total_time Total play time (in seconds)
 *                      0 to 359999
 *                      * 0 hour, 0 minute, 0 second to 99 hours, 59 minutes, 59 seconds
 *
 *  @returns 
 *    Returns true if the API is successful. Returns false if the API fails.
 *    This function fails when:
 *     Failed to secure memory for mailbox communication.
 *     Failed to perform transmit processing for mailbox communication.
 */
bool sys_notify_play_time(const SYS_PlayStat play_stat, 
    const uint32_t play_time, const uint32_t total_time);

#endif /* SYSTEM_H */
