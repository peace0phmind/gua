#ifndef __PS_CODECS_H__
#define __PS_CODECS_H__


#include <pjmedia-codec/types.h>
#include <pjmedia/vid_codec.h>
#include <libavcodec/avcodec.h>
#include <pjsip/sip_uri.h>


PJ_BEGIN_DECL

#define MAX_GET_OR_SKIP_BUF_SIZE       2000

#ifndef PJSIP_MAX_URL_SIZE
#define PJSIP_MAX_URL_SIZE 256
#endif


typedef struct ps_codec {
    pjmedia_frame *packets;
    pj_size_t     pkt_count;
    int           pkt_idx;
    pj_uint8_t*   current_buf;
    pj_size_t     remain_buf_len;
    pj_uint8_t    temp_buf[MAX_GET_OR_SKIP_BUF_SIZE];
    pj_size_t     temp_data_len;
    pj_bool_t     is_i_frame;
    // under is for copy op
    pj_uint8_t    *dec_buf;
    pj_size_t     dec_buf_size;
    unsigned      dec_data_len;
    // out for except pes video buf len
    unsigned      total_video_pes_len;
    // ffmpeg codec
    enum AVCodecID     video_codec_id;
    enum AVCodecID     audio_codec_id;
    // cname
    char    callee_id[PJSIP_MAX_URL_SIZE];
} ps_codec;

/**
 * @defgroup PJMEDIA_CODEC_VID_PS ps Codecs
 * @ingroup PJMEDIA_CODEC_VID_CODECS
 * @{
 */

/**
 * Initialize and register ps video codecs factory to pjmedia endpoint.
 *
 * @param mgr	    The video codec manager instance where this codec will
 * 		    be registered to. Specify NULL to use default instance
 * 		    (in that case, an instance of video codec manager must
 * 		    have been created beforehand).
 * @param pf	    Pool factory.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_codec_ps_vid_init(pjmedia_vid_codec_mgr *mgr,
                                                   pj_pool_factory *pf);


typedef struct pjmedia_ps_codec_callback {
    /**
     * when whole data ready, decode call this cb
     */
    void (*on_decode_cb)(ps_codec *codec);
} pjmedia_ps_codec_callback;

PJ_DECL(pj_status_t) pjmedia_codec_ps_vid_init_cb(pjmedia_ps_codec_callback *cb);

/**
 * Unregister ps video codecs factory from the video codec manager and
 * deinitialize the codecs library.
 *
 * @return	    PJ_SUCCESS on success.
 */
PJ_DECL(pj_status_t) pjmedia_codec_ps_vid_deinit(void);


PJ_END_DECL


/**
 * @}
 */

#endif	/* __PS_CODECS_H__ */

