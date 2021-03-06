/* $Id$ */
/*
 * Copyright (C) 2010-2011 Teluu Inc. (http://www.teluu.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <pjmedia/types.h>
#include <pj/errno.h>
#include <pj/log.h>
#include <pj/string.h>

#if defined(PJMEDIA_HAS_VIDEO) && (PJMEDIA_HAS_VIDEO != 0) && \
    defined(PJMEDIA_HAS_LIBAVFORMAT) && (PJMEDIA_HAS_LIBAVFORMAT != 0)

#include "include/ps_util.h"
#include <libavformat/avformat.h>

/* Conversion table between pjmedia_format_id and AVPixelFormat */
static const struct ps_fmt_table_t
{
    pjmedia_format_id         id;
    enum AVPixelFormat        pf;
} ps_fmt_table[] =
{
    { PJMEDIA_FORMAT_RGBA, AV(PIX_FMT_RGBA)},
    { PJMEDIA_FORMAT_RGB24,AV(PIX_FMT_BGR24)},
    { PJMEDIA_FORMAT_BGRA, AV(PIX_FMT_BGRA)},
    { PJMEDIA_FORMAT_GBRP, AV(PIX_FMT_GBRP)},

    { PJMEDIA_FORMAT_AYUV, AV(PIX_FMT_NONE)},
    { PJMEDIA_FORMAT_YUY2, AV(PIX_FMT_YUYV422)},
    { PJMEDIA_FORMAT_UYVY, AV(PIX_FMT_UYVY422)},
    { PJMEDIA_FORMAT_I420, AV(PIX_FMT_YUV420P)},
    //{ PJMEDIA_FORMAT_YV12, AV(PIX_FMT_YUV420P)},
    { PJMEDIA_FORMAT_I422, AV(PIX_FMT_YUV422P)},
    { PJMEDIA_FORMAT_I420JPEG, AV(PIX_FMT_YUVJ420P)},
    { PJMEDIA_FORMAT_I422JPEG, AV(PIX_FMT_YUVJ422P)},
    { PJMEDIA_FORMAT_NV12, AV(PIX_FMT_NV12)},
    { PJMEDIA_FORMAT_NV21, AV(PIX_FMT_NV21)},
};

/* Conversion table between pjmedia_format_id and CodecID */
static const struct ps_codec_table_t
{
    pjmedia_format_id        id;
    unsigned                 codec_id;
} ps_codec_table[] =
{
    {PJMEDIA_FORMAT_H261,           AV(CODEC_ID_H261)},
    {PJMEDIA_FORMAT_H263,           AV(CODEC_ID_H263)},
    {PJMEDIA_FORMAT_H263P,          AV(CODEC_ID_H263P)},
    {PJMEDIA_FORMAT_H264,           AV(CODEC_ID_H264)},
    {PJMEDIA_FORMAT_MPEG1VIDEO,     AV(CODEC_ID_MPEG1VIDEO)},
    {PJMEDIA_FORMAT_MPEG2VIDEO,     AV(CODEC_ID_MPEG2VIDEO)},
    {PJMEDIA_FORMAT_MPEG4,          AV(CODEC_ID_MPEG4)},
    {PJMEDIA_FORMAT_MJPEG,          AV(CODEC_ID_MJPEG)}
};

static const struct ps_psm_codec_id_table_t
{
    pj_uint8_t	stream_id;
    pj_uint8_t  stream_type;
    enum AVCodecID   codec_id;
} ps_psm_codec_id_table[] =
{
    { 0xE0, 0x1B, AV_CODEC_ID_H264},
    { 0xE0, 0x10, AV_CODEC_ID_MPEG4},
    { 0xE0, 0x80, AV_CODEC_ID_NONE}, // SVAC

    { 0xC0, 0x90, AV_CODEC_ID_NONE}, // 711
    { 0xC0, 0x92, AV_CODEC_ID_ADPCM_G722},
    { 0xC0, 0x93, AV_CODEC_ID_G723_1},
    { 0xC0, 0x99, AV_CODEC_ID_G729},
    { 0xC0, 0x9B, AV_CODEC_ID_NONE}, // SVAC
};

static int ps_ref_cnt;

static void ps_log_cb(void* ptr, int level, const char* fmt, va_list vl);

void ps_add_ref()
{
    if (ps_ref_cnt++ == 0) {
        av_log_set_level(AV_LOG_ERROR);
        av_log_set_callback(&ps_log_cb);
//        av_register_all();
    }
}

void ps_dec_ref()
{
    if (ps_ref_cnt-- == 1) {
        /* How to shutdown ps? */
    }

    if (ps_ref_cnt < 0) {
        ps_ref_cnt = 0;
    }
}

static void ps_log_cb(void* ptr, int level, const char* fmt, va_list vl)
{
    const char *LOG_SENDER = "ps";
    enum { LOG_LEVEL = 5 };
    char buf[100];
    pj_size_t bufsize = sizeof(buf), len;
    pj_str_t fmt_st;

    /* Custom callback needs to filter log level by itself */
    if (level > av_log_get_level()) {
        return;
    }

    /* Add original ps sender to log format */
    if (ptr) {
        AVClass* avc = *(AVClass**)ptr;
        len = pj_ansi_snprintf(buf, bufsize, "%s: ", avc->item_name(ptr));
        if (len < 1 || len >= bufsize) {
            len = bufsize - 1;
        }
        bufsize -= len;
    }

    /* Copy original log format */
    len = pj_ansi_strlen(fmt);
    if (len > bufsize-1) {
        len = bufsize-1;
    }
    pj_memcpy(buf+sizeof(buf)-bufsize, fmt, len);
    bufsize -= len;

    /* Trim log format */
    pj_strset(&fmt_st, buf, sizeof(buf)-bufsize);
    pj_strrtrim(&fmt_st);
    buf[fmt_st.slen] = '\0';

    pj_log(LOG_SENDER, LOG_LEVEL, buf, vl);
}


pj_status_t ps_format_id_to_PixelFormat(pjmedia_format_id fmt_id,
                                             enum AVPixelFormat *pixel_format)
{
    unsigned i;
    for (i=0; i<PJ_ARRAY_SIZE(ps_fmt_table); ++i) {
        const struct ps_fmt_table_t *t = &ps_fmt_table[i];
        if (t->id==fmt_id && t->pf != AV(PIX_FMT_NONE)) {
            *pixel_format = t->pf;
            return PJ_SUCCESS;
        }
    }

    *pixel_format = AV(PIX_FMT_NONE);
    return PJ_ENOTFOUND;
}

pj_status_t PixelFormat_to_ps_format_id(enum AVPixelFormat pf,
                                             pjmedia_format_id *fmt_id)
{
    unsigned i;
    for (i=0; i<PJ_ARRAY_SIZE(ps_fmt_table); ++i) {
        const struct ps_fmt_table_t *t = &ps_fmt_table[i];
        if (t->pf == pf) {
            if (fmt_id) *fmt_id = t->id;
            return PJ_SUCCESS;
        }
    }

    return PJ_ENOTFOUND;
}

pj_status_t ps_format_id_to_CodecID(pjmedia_format_id fmt_id, unsigned *codec_id)
{
    unsigned i;
    for (i=0; i<PJ_ARRAY_SIZE(ps_codec_table); ++i) {
        const struct ps_codec_table_t *t = &ps_codec_table[i];
        if (t->id==fmt_id && t->codec_id != AV(PIX_FMT_NONE)) {
            *codec_id = t->codec_id;
            return PJ_SUCCESS;
        }
    }

    *codec_id = (unsigned)AV(PIX_FMT_NONE);
    return PJ_ENOTFOUND;
}

pj_status_t CodecID_to_ps_format_id(unsigned codec_id,
                                         pjmedia_format_id *fmt_id)
{
    unsigned i;
    for (i=0; i<PJ_ARRAY_SIZE(ps_codec_table); ++i) {
        const struct ps_codec_table_t *t = &ps_codec_table[i];
        if ((unsigned)t->codec_id == codec_id) {
            if (fmt_id) *fmt_id = t->id;
            return PJ_SUCCESS;
        }
    }

    return PJ_ENOTFOUND;
}

pj_status_t set_ps_codec_id_from_psm_info(ps_codec *ppc, pj_uint8_t *buf) {
    unsigned i;
    for (i = 0; i < PJ_ARRAY_SIZE(ps_psm_codec_id_table); ++i) {
        const struct ps_psm_codec_id_table_t *t = &ps_psm_codec_id_table[i];
        if (t->stream_id == *(buf+1) && t->stream_type == *buf) {
            if (t->stream_id == 0xE0) {
                ppc->video_codec_id = t->codec_id;
            } else {
                ppc->audio_codec_id = t->codec_id;
            }
            return PJ_SUCCESS;
        }
    }

    return PJ_ENOTFOUND;
}


#ifdef _MSC_VER
#   pragma comment( lib, "avformat.lib")
#   pragma comment( lib, "avutil.lib")
#endif

#endif        /* PJMEDIA_HAS_LIBAVFORMAT */
