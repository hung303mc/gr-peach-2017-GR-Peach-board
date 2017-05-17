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

#ifndef SYS_SCAN_FOLDER_H
#define SYS_SCAN_FOLDER_H

#include "r_typedefs.h"
#include "system.h"

/*--- User defined types ---*/
/* Information of a folder / track. */
typedef struct {
    char_t      name[SYS_MAX_NAME_LENGTH + 1];  /* Name of a folder / track. */
    uint32_t    parent_number;                  /* Number of the parent folder. */
} item_t;

/* Information of folder scan in USB memory */
typedef struct {
    item_t      folder_list[SYS_MAX_FOLDER_NUM];    /* Folder list */
    item_t      track_list[SYS_MAX_TRACK_NUM];      /* Track list */
    uint32_t    total_folder;                       /* Total number of folders */
    uint32_t    total_track;                        /* Total number of tracks */
    char_t      work_buf[SYS_MAX_PATH_LENGTH + 1];  /* Work */
                                                    /* (Including the null terminal character.) */
} fid_scan_folder_t;

/** Initializes the folder structure of USB memory
 *
 *  @param p_info Pointer to the control data of folder scan module.
 */
void fid_init(fid_scan_folder_t * const p_info);

/** Scans the folder structure of USB memory
 *
 *  @param p_info Pointer to the control data of folder scan module.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
bool fid_scan_folder_struct(fid_scan_folder_t * const p_info);

/** Gets the total number of detected tracks
 *
 *  @param p_info Pointer to the control data of folder scan module.
 *
 *  @returns 
 *    the total number of tracks
 */
uint32_t fid_get_total_track(const fid_scan_folder_t * const p_info);

/** Gets the track name
 *
 *  @param p_info Pointer to the control data of folder scan module.
 *  @param track_id Track ID [0 - (total track - 1)]
 *
 *  @returns 
 *    Pointer to the track name.
 */
const char_t *fid_get_track_name(const fid_scan_folder_t * const p_info, 
                                                const uint32_t track_id);

/** Opens the track
 *
 *  @param p_info Pointer to the control data of folder scan module.
 *  @param track_id Track ID [0 - (total track - 1)]
 *
 *  @returns 
 *    Pointer to the track handle
 */
FILE *fid_open_track(fid_scan_folder_t * const p_info, const uint32_t track_id);

/** Closes the track
 *
 *  @param fp Pointer to the track handle
 */
void fid_close_track(FILE * const fp);

#endif /* SYS_SCAN_FOLDER_H */
