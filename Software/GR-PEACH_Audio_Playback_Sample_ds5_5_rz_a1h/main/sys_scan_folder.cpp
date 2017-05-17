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
#include "FATFileSystem.h"
#include "USBHostMSD.h"
#include "sys_scan_folder.h"

/*--- Macro definition of folder structure scan. ---*/
/* The character string to identify root directory. */
#define STR_ROOT_FOR_F_OPENDIR  ""                      /* to use f_opendir() */
#define STR_ROOT_FOR_FOPEN      "/" SYS_USB_MOUNT_NAME  /* to use fopen()  */

/* The file extension of FLAC. */
#define FILE_EXT_FLAC           ".flac"
#define FILE_EXT_FLA            ".fla"

#define CHR_FULL_STOP           '.'         /* 0x2E: FULL STOP */
#define CHR_SOLIDUS             '/'         /* 0x2F: SOLIDUS */
#define FOLD_ID_NOT_EXIST       (0xFFFFFFFFu)
#define OPEN_MODE_READ_ONLY     "r"

/* File path maximum size including the usb mount name size */
#define USB_MOUNT_NAME_SIZE     (sizeof(STR_ROOT_FOR_FOPEN "/"))
#define FILE_PATH_MAX_LEN       (60u)
#define FILE_PATH_MAX_SIZE      (USB_MOUNT_NAME_SIZE + FILE_PATH_MAX_LEN)

static const char_t *get_full_path(fid_scan_folder_t * const p_info, 
                                                const item_t * const p_item);
static bool open_dir(fid_scan_folder_t * const p_info, 
                        const item_t * const p_item, FATFS_DIR * const p_fdir);
static bool read_dir(fid_scan_folder_t * const p_info, FATFS_DIR * const p_fdir, 
                            const char_t ** const p_name, bool * const p_flag_dir);
static bool regist_item(item_t * const p_item, 
                            const char_t * const p_name, const uint32_t parent);
static bool check_extension(const char_t * const p_name);

static bool check_folder_depth(const fid_scan_folder_t * const p_info, 
                                                    const uint32_t folder_id);

void fid_init(fid_scan_folder_t * const p_info)
{
    if (p_info != NULL) {
        p_info->total_folder = 0u;
        p_info->total_track = 0u;
    }
}

bool fid_scan_folder_struct(fid_scan_folder_t * const p_info)
{
    bool            ret = false;
    bool            result;
    bool            chk;
    uint32_t        i;
    item_t          *p_item;
    FATFS_DIR       fdir;
    const char_t    *p_name;
    bool            flg_dir;
    bool            chk_dep;
    
    if (p_info != NULL) {
        /* Initializes the scan data. */
        p_info->total_track = 0u;
        p_info->total_folder = 0u;

        /* Registers the identifier of the root directory to use f_opendir(). */
        (void) regist_item(&p_info->folder_list[0], STR_ROOT_FOR_F_OPENDIR, FOLD_ID_NOT_EXIST);
        p_info->total_folder++;

        /* Checks the item in all registered directory. */
        for (i = 0; i < p_info->total_folder; i++) {
            chk_dep = check_folder_depth(p_info, i);
            result = open_dir(p_info, &p_info->folder_list[i], &fdir);
            while (result == true) {
                result = read_dir(p_info, &fdir, &p_name, &flg_dir);
                if (result == true) {
                    /* Checks the attribute of this item. */
                    if (flg_dir == true) {
                        /* This item is directory. */
                        if ((chk_dep == true) && (p_info->total_folder < SYS_MAX_FOLDER_NUM)) {
                            p_item = &p_info->folder_list[p_info->total_folder];
                            chk = regist_item(p_item, p_name, i);
                            if (chk == true) {
                                /* Register of directory item was success. */
                                p_info->total_folder++;
                            }
                        }
                    } else {
                        /* This item is file. */
                        chk = check_extension(p_name);
                        if ((chk == true) && (p_info->total_track < SYS_MAX_TRACK_NUM)) {
                            /* This item is FLAC file. */
                            p_item = &p_info->track_list[p_info->total_track];
                            chk = regist_item(p_item, p_name, i);
                            if (chk == true) {
                                /* Register of file item was success. */
                                p_info->total_track++;
                            }
                        }
                    }
                }
            }
        }
        /* Changes the identifier of the root directory to use fopen(). */
        (void) regist_item(&p_info->folder_list[0], STR_ROOT_FOR_FOPEN, FOLD_ID_NOT_EXIST);

        if (p_info->total_track > 0u) {
            ret = true;
        }
    }

    return ret;
}

FILE *fid_open_track(fid_scan_folder_t * const p_info, const uint32_t track_id)
{
    FILE            *fp = NULL;
    const char_t    *p_path;
    size_t          path_len;

    if (p_info != NULL) {
        if (track_id < p_info->total_track) {
            p_path = get_full_path(p_info, &p_info->track_list[track_id]);
            if (p_path != NULL) {
                path_len = strlen(p_path); 
                /* File path maximum length is limited by the specification of "fopen". */
                if (path_len < FILE_PATH_MAX_SIZE) {
                    fp = fopen(p_path, OPEN_MODE_READ_ONLY);
                }
            }
        }
    }
    return fp;
}

void fid_close_track(FILE * const fp)
{
    if (fp != NULL) {
        (void) fclose(fp);
    }
}

const char_t *fid_get_track_name(const fid_scan_folder_t * const p_info, 
                                                const uint32_t track_id)
{
    const char_t    *p_name = NULL;

    if (p_info != NULL) {
        if (track_id < p_info->total_track) {
            p_name = &p_info->track_list[track_id].name[0];
        }
    }
    return p_name;
}

uint32_t fid_get_total_track(const fid_scan_folder_t * const p_info)
{
    uint32_t        ret = 0u;

    if (p_info != NULL) {
        ret = p_info->total_track;
    }
    return ret;
}

/** Gets the full path
 *
 *  @param p_info Pointer to the control data of folder scan module.
 *  @param p_item Pointer to the item structure of the folder / track.
 *
 *  @returns 
 *    Pointer to the full path.
 */
static const char_t *get_full_path(fid_scan_folder_t * const p_info, 
                                        const item_t * const p_item)
{
    const char_t    *p_path = NULL;
    const item_t    *p;
    const item_t    *item_list[SYS_MAX_FOLDER_DEPTH];
    uint32_t        i;
    uint32_t        item_cnt;
    uint32_t        buf_cnt;
    uint32_t        len;
    bool            err;

    if ((p_info != NULL) && (p_item != NULL)) {
        for (i = 0; i < SYS_MAX_FOLDER_DEPTH; i++) {
            item_list[i] = NULL;
        }
        p_info->work_buf[0] = '\0';

        /* Stores the item name until the root folder. */
        p = p_item;
        item_cnt = 0;
        while ((item_cnt < SYS_MAX_FOLDER_DEPTH) && 
               (p->parent_number < p_info->total_folder)) {
            item_list[item_cnt] = p;
            item_cnt++;
            p = &p_info->folder_list[p->parent_number];
        } 
        if (p->parent_number == FOLD_ID_NOT_EXIST) {
            buf_cnt = strlen(p->name);
            (void) strncpy(&p_info->work_buf[0], p->name, sizeof(p_info->work_buf));
            err = false;
            while ((item_cnt > 0u) && (err != true)) {
                item_cnt--;
                p = item_list[item_cnt];
                /* Concatenates SOLIDUS character to the "work_buf" variable. */
                if ((buf_cnt + 1u) < sizeof(p_info->work_buf)) {
                    p_info->work_buf[buf_cnt] = CHR_SOLIDUS;
                    buf_cnt++;
                } else {
                    err = true;
                }
                /* Concatenates the item name to the "work_buf" variable. */
                if (p != NULL) {
                    len = strlen(p->name);
                    if ((buf_cnt + len) < sizeof(p_info->work_buf)) {
                        (void) strncpy(&p_info->work_buf[buf_cnt], p->name, len);
                        buf_cnt += len;
                    } else {
                        err = true;
                    }
                }
            }
            if (err != true) {
                p_info->work_buf[buf_cnt] = '\0';
                p_path = &p_info->work_buf[0];
            }
        }
    }
    return p_path;
}

/** Opens the directory
 *
 *  @param p_info Pointer to the control data of folder scan module.
 *  @param p_item Pointer to the item structure of the folder / track.
 *  @param p_fdir Pointer to the structure to store the directory object.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool open_dir(fid_scan_folder_t * const p_info, 
                        const item_t * const p_item, FATFS_DIR * const p_fdir)
{
    bool            ret = false;
    const char_t    *p_path;
    FRESULT         ferr;

    if ((p_info != NULL) && (p_item != NULL) && (p_fdir != NULL)) {
        p_path = get_full_path(p_info, p_item);
        if (p_path != NULL) {
            ferr = f_opendir(p_fdir, p_path);
            if (ferr == FR_OK) {
                ret = true;
            }
        }
    }
    return ret;
}

/** Reads the directory
 *
 *  @param p_info Pointer to the control data of folder scan module.
 *  @param p_fdir Pointer to the structure of the directory object.
 *  @param p_flag_dir Pointer to the variable to store the directory flag.
 *
 *  @returns 
 *    Pointer to the name.
 */
static bool read_dir(fid_scan_folder_t * const p_info, FATFS_DIR * const p_fdir, 
                            const char_t ** const p_name, bool * const p_flag_dir)
{
    bool            ret = false;
    FRESULT         ferr;
    FILINFO         finfo;

    if ((p_info != NULL) && (p_fdir != NULL) && 
        (p_name != NULL) && (p_flag_dir != NULL)) {
        /* Sets the buffer to store the long file name. */
        finfo.lfname = &p_info->work_buf[0];
        finfo.lfsize = sizeof(p_info->work_buf);
        ferr = f_readdir(p_fdir, &finfo);
        if ((ferr == FR_OK) && ((int32_t)finfo.fname[0] != '\0')) {
            if (finfo.lfname != NULL) {
                if ((int32_t)finfo.lfname[0] == '\0') {
                    /* Long file name does not exist. */
                    (void) strncpy(finfo.lfname, finfo.fname, finfo.lfsize);
                }
                /* Adds the NULL terminal character. */
                /* This is fail-safe processing. */
                finfo.lfname[finfo.lfsize - 1u] = '\0';

                ret = true;
                *p_name = finfo.lfname;
                if ((finfo.fattrib & AM_DIR) != 0) {
                    /* This item is directory. */
                    *p_flag_dir = true;
                } else {
                    /* This item is file. */
                    *p_flag_dir = false;
                }
            }
        }
    }
    return ret;
}

/** Registers the item of the folder / track
 *
 *  @param p_item Pointer to the structure to store the item of the folder / track.
 *  @param p_name Pointer to the name of the item.
 *  @param parent Number of the parent folder of the item.
 *
 *  @returns 
 *    Results of process. true is success. false is failure.
 */
static bool regist_item(item_t * const p_item, 
                            const char_t * const p_name, const uint32_t parent)
{
    bool        ret = false;
    uint32_t    len;

    if ((p_item != NULL) && (p_name != NULL)) {
        len = strlen(p_name);
        if ((len + 1u) < sizeof(p_item->name)) {
            (void) strncpy(p_item->name, p_name, sizeof(p_item->name));
            p_item->name[sizeof(p_item->name) - 1u] = '\0';
            p_item->parent_number = parent;
            ret = true;
        }
    }
    return ret;
}

/** Checks the extension of the track name
 *
 *  @param p_name Pointer to the name of the track.
 *
 *  @returns 
 *    Results of the checking. true is FLAC file. false is other file.
 */
static bool check_extension(const char_t * const p_name)
{
    bool            ret = false;
    const char_t    *p;

    if (p_name != NULL) {
        p = strrchr(p_name, CHR_FULL_STOP);
        if (p != NULL) {
            if (strncasecmp(p, FILE_EXT_FLAC, sizeof(FILE_EXT_FLAC)) == 0) {
                ret = true;
            } else if (strncasecmp(p, FILE_EXT_FLA, sizeof(FILE_EXT_FLA)) == 0) {
                ret = true;
            } else {
                /* DO NOTHING */
            }
        }
    }
    return ret;
}

/** Checks the folder depth in the scan range
 *
 *  @param p_info Pointer to the control data of folder scan module.
 *  @param folder_id Folder ID [0 - (total folder - 1)]
 *
 *  @returns 
 *    Results of the checking. true is the scan range. false is out of a scan range.
 */
static bool check_folder_depth(const fid_scan_folder_t * const p_info, 
                                                    const uint32_t folder_id)
{
    bool            ret = false;
    uint32_t        depth;
    uint32_t        parent_id;

    if (p_info != NULL) {
        /* Counts the folder depth. */
        parent_id = folder_id;
        depth = 0u;
        while ((depth < SYS_MAX_FOLDER_DEPTH) && 
               (parent_id < p_info->total_folder)) {
            depth++;
            parent_id = p_info->folder_list[parent_id].parent_number;
        } 
        if (parent_id == FOLD_ID_NOT_EXIST) {
            /* Found the root folder. */
            if (depth < SYS_MAX_FOLDER_DEPTH) {
                ret = true;
            }
        }
    }
    return ret;
}
