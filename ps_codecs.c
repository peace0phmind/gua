#include "include/ps_codecs.h"
#include <pjmedia-codec/h264_packetizer.h>
#include <pjmedia/errno.h>
#include <pjmedia/vid_codec_util.h>
#include <pj/assert.h>
#include <pj/list.h>
#include <pj/log.h>
#include <pj/math.h>
#include <pj/pool.h>
#include <pj/string.h>
#include <pj/os.h>

#include "include/pjsua.h"
#include "include/pjsua_internal.h"


#define THIS_FILE   "ps_codecs.c"

#define TRACE_PS   0

#define LIBAVCODEC_VER_AT_LEAST(major,minor)  (LIBAVCODEC_VERSION_MAJOR > major || \
                                              (LIBAVCODEC_VERSION_MAJOR == major && \
                                               LIBAVCODEC_VERSION_MINOR >= minor))

#include "include/ps_util.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#if LIBAVCODEC_VER_AT_LEAST(53,20)
  /* Needed by 264 so far, on libavcodec 53.20 */
# include <libavutil/opt.h>
#endif

/* Various compatibility */

#if LIBAVCODEC_VER_AT_LEAST(53,20)
#  define AVCODEC_OPEN(ctx,c)                avcodec_open2(ctx,c,NULL)
#else
#  define AVCODEC_OPEN(ctx,c)                avcodec_open(ctx,c)
#endif

#if LIBAVCODEC_VER_AT_LEAST(53,61)
#  if LIBAVCODEC_VER_AT_LEAST(54,59)
   /* Not sure when AVCodec::encode is obsoleted/removed. */
#      define AVCODEC_HAS_ENCODE(c)        (c->encode2)
#  else
   /* Not sure when AVCodec::encode2 is introduced. It appears in
    * libavcodec 53.61 where some codecs actually still use AVCodec::encode
    * (e.g: H263, H264).
    */
#      define AVCODEC_HAS_ENCODE(c)        (c->encode || c->encode2)
#  endif
#  define AV_OPT_SET(obj,name,val,opt)        (av_opt_set(obj,name,val,opt)==0)
#  define AV_OPT_SET_INT(obj,name,val)        (av_opt_set_int(obj,name,val,0)==0)
#else
#  define AVCODEC_HAS_ENCODE(c)                (c->encode)
#  define AV_OPT_SET(obj,name,val,opt)        (av_set_string3(obj,name,val,opt,NULL)==0)
#  define AV_OPT_SET_INT(obj,name,val)        (av_set_int(obj,name,val)!=NULL)
#endif
#define AVCODEC_HAS_DECODE(c)                (c->decode)


/* Prototypes for PS codecs factory */
static pj_status_t ps_test_alloc( pjmedia_vid_codec_factory *factory,
                                      const pjmedia_vid_codec_info *id );
static pj_status_t ps_default_attr( pjmedia_vid_codec_factory *factory,
                                        const pjmedia_vid_codec_info *info,
                                        pjmedia_vid_codec_param *attr );
static pj_status_t ps_enum_codecs( pjmedia_vid_codec_factory *factory,
                                       unsigned *count,
                                       pjmedia_vid_codec_info codecs[]);
static pj_status_t ps_alloc_codec( pjmedia_vid_codec_factory *factory,
                                       const pjmedia_vid_codec_info *info,
                                       pjmedia_vid_codec **p_codec);
static pj_status_t ps_dealloc_codec( pjmedia_vid_codec_factory *factory,
                                         pjmedia_vid_codec *codec );

/* Prototypes for PS codecs implementation. */
static pj_status_t  ps_codec_init( pjmedia_vid_codec *codec,
                                       pj_pool_t *pool );
static pj_status_t  ps_codec_open( pjmedia_vid_codec *codec,
                                       pjmedia_vid_codec_param *attr );
static pj_status_t  ps_codec_close( pjmedia_vid_codec *codec );
static pj_status_t  ps_codec_modify(pjmedia_vid_codec *codec,
                                        const pjmedia_vid_codec_param *attr );
static pj_status_t  ps_codec_get_param(pjmedia_vid_codec *codec,
                                           pjmedia_vid_codec_param *param);
static pj_status_t ps_codec_encode_begin(pjmedia_vid_codec *codec,
                                             const pjmedia_vid_encode_opt *opt,
                                             const pjmedia_frame *input,
                                             unsigned out_size,
                                             pjmedia_frame *output,
                                             pj_bool_t *has_more);
static pj_status_t ps_codec_encode_more(pjmedia_vid_codec *codec,
                                            unsigned out_size,
                                            pjmedia_frame *output,
                                            pj_bool_t *has_more);
static pj_status_t ps_codec_decode( pjmedia_vid_codec *codec,
                                        pj_size_t pkt_count,
                                        pjmedia_frame packets[],
                                        unsigned out_size,
                                        pjmedia_frame *output);

/* Definition for PS codecs operations. */
static pjmedia_vid_codec_op ps_op =
{
    &ps_codec_init,
    &ps_codec_open,
    &ps_codec_close,
    &ps_codec_modify,
    &ps_codec_get_param,
    &ps_codec_encode_begin,
    &ps_codec_encode_more,
    &ps_codec_decode,
    NULL
};

/* Definition for PS codecs factory operations. */
static pjmedia_vid_codec_factory_op ps_factory_op =
{
    &ps_test_alloc,
    &ps_default_attr,
    &ps_enum_codecs,
    &ps_alloc_codec,
    &ps_dealloc_codec
};


/* PS codecs factory */
static struct ps_factory {
    pjmedia_vid_codec_factory    base;
    pjmedia_vid_codec_mgr        *mgr;
    pj_pool_factory             *pf;
    pj_pool_t                        *pool;
    pj_mutex_t                        *mutex;
    pjmedia_ps_codec_callback *ps_codec_callback;
} ps_factory;

typedef struct ps_codec_desc ps_codec_desc;

enum ps_codec_op {
	PS_CODEC_OP_GET             = 1,  // get and seek
	PS_CODEC_OP_COPY            = 2,  // copy and seek
	PS_CODEC_OP_SEEK            = 3,  // only seek
};

/* PS codecs private data. */
typedef struct ps_private
{
    const ps_codec_desc                 *desc;
    pjmedia_vid_codec_param             param;        /**< Codec param            */
    pj_pool_t                            *pool;        /**< Pool for each instance */

    /* Format info and apply format param */
    const pjmedia_video_format_info *enc_vfi;
    pjmedia_video_apply_fmt_param    enc_vafp;
    const pjmedia_video_format_info *dec_vfi;
    pjmedia_video_apply_fmt_param    dec_vafp;

    /* Buffers, only needed for multi-packets */
    pj_bool_t                             whole;
    void                                *enc_buf;
    unsigned                             enc_buf_size;
    pj_bool_t                             enc_buf_is_keyframe;
    unsigned                             enc_frame_len;
    unsigned                           enc_processed;
    void                              *dec_buf;
    unsigned                             dec_buf_size;
    pj_timestamp                        last_dec_keyframe_ts;

    /* The ps codec states. */
    AVCodec                             *enc;
    AVCodec                             *dec;
    AVCodecContext                       *enc_ctx;
    AVCodecContext                      *dec_ctx;

    /* The ps decoder cannot set the output format, so format conversion
     * may be needed for post-decoding.
     */
    enum AVPixelFormat                     expected_dec_fmt;
                                                /**< Expected output format of
                                                     ps decoder            */

    void                                   *data;        /**< Codec specific data    */
} ps_private;


/* Shortcuts for packetize & unpacketize function declaration,
 * as it has long params and is reused many times!
 */
#define FUNC_PACKETIZE(name) \
    pj_status_t(name)(ps_private *ff, pj_uint8_t *bits, \
                      pj_size_t bits_len, unsigned *bits_pos, \
                      const pj_uint8_t **payload, pj_size_t *payload_len)

#define FUNC_UNPACKETIZE(name) \
    pj_status_t(name)(ps_private *ff, const pj_uint8_t *payload, \
                      pj_size_t payload_len, pj_uint8_t *bits, \
                      pj_size_t bits_len, unsigned *bits_pos)

#define FUNC_FMT_MATCH(name) \
    pj_status_t(name)(pj_pool_t *pool, \
                      pjmedia_sdp_media *offer, unsigned o_fmt_idx, \
                      pjmedia_sdp_media *answer, unsigned a_fmt_idx, \
                      unsigned option)


/* Type definition of codec specific functions */
typedef FUNC_PACKETIZE(*func_packetize);
typedef FUNC_UNPACKETIZE(*func_unpacketize);
typedef pj_status_t (*func_preopen)        (ps_private *ff);
typedef pj_status_t (*func_postopen)        (ps_private *ff);
typedef FUNC_FMT_MATCH(*func_sdp_fmt_match);


/* PS codec info */
struct ps_codec_desc
{
    /* Predefined info */
    pjmedia_vid_codec_info       info;
    pjmedia_format_id                 base_fmt_id;        /**< Some codecs may be exactly
                                                     same or compatible with
                                                     another codec, base format
                                                     will tell the initializer
                                                     to copy this codec desc
                                                     from its base format   */
    pjmedia_rect_size            size;
    pjmedia_ratio                fps;
    pj_uint32_t                         avg_bps;
    pj_uint32_t                         max_bps;
    func_packetize                 packetize;
    func_unpacketize                 unpacketize;
    func_preopen                 preopen;
    func_preopen                 postopen;
    func_sdp_fmt_match                 sdp_fmt_match;
    pjmedia_codec_fmtp                 dec_fmtp;

    /* Init time defined info */
    pj_bool_t                   enabled;
    AVCodec                     *enc;
    AVCodec                     *dec;
};


#if PJMEDIA_HAS_FFMPEG_CODEC_H264 && !LIBAVCODEC_VER_AT_LEAST(53,20)
#   error "Must use libavcodec version 53.20 or later to enable FFMPEG H264"
#endif

/* H264 constants */
#define PROFILE_H264_BASELINE            66
#define PROFILE_H264_MAIN                77

/* Codec specific functions */
#if PJMEDIA_HAS_FFMPEG_CODEC_H264
static pj_status_t h264_preopen(ps_private *ff);
static pj_status_t h264_postopen(ps_private *ff);
static FUNC_PACKETIZE(h264_packetize);
static FUNC_UNPACKETIZE(h264_unpacketize);
#endif

/* Internal codec info */
static ps_codec_desc codec_desc[] =
{
    {
        {PJMEDIA_FORMAT_H264, PJMEDIA_RTP_PT_PS, {"PS",2},
         {"Constrained Baseline (level=30, pack=1)", 39}},
        0,
        {1920, 1080},        {25, 1},        256000, 256000,
        &h264_packetize, &h264_unpacketize, &h264_preopen, &h264_postopen,
        &pjmedia_vid_codec_h264_match_sdp,
        /* Leading space for better compatibility (strange indeed!) */
        {2, { {{"profile-level-id",16},    {"42e01e",6}},
              {{" packetization-mode",19},  {"1",1}}, } },
    },
};

#if PJMEDIA_HAS_FFMPEG_CODEC_H264

typedef struct h264_data
{
    pjmedia_vid_codec_h264_fmtp         fmtp;
    pjmedia_h264_packetizer             *pktz;
} h264_data;

static pj_status_t h264_preopen(ps_private *ff)
{
    h264_data *data;
    pjmedia_h264_packetizer_cfg pktz_cfg;
    pj_status_t status;

    data = PJ_POOL_ZALLOC_T(ff->pool, h264_data);
    ff->data = data;

    /* Parse remote fmtp */
    status = pjmedia_vid_codec_h264_parse_fmtp(&ff->param.enc_fmtp, &data->fmtp);
    if (status != PJ_SUCCESS) {
        return status;
    }

    /* Create packetizer */
    pktz_cfg.mtu = ff->param.enc_mtu;
    pktz_cfg.unpack_nal_start = 0;
#if 0
    if (data->fmtp.packetization_mode == 0)
        pktz_cfg.mode = PJMEDIA_H264_PACKETIZER_MODE_SINGLE_NAL;
    else if (data->fmtp.packetization_mode == 1)
        pktz_cfg.mode = PJMEDIA_H264_PACKETIZER_MODE_NON_INTERLEAVED;
    else
        return PJ_ENOTSUP;
#else
    if (data->fmtp.packetization_mode != PJMEDIA_H264_PACKETIZER_MODE_SINGLE_NAL
     && data->fmtp.packetization_mode != PJMEDIA_H264_PACKETIZER_MODE_NON_INTERLEAVED) {
        return PJ_ENOTSUP;
    }
    /* Better always send in single NAL mode for better compatibility */
    pktz_cfg.mode = PJMEDIA_H264_PACKETIZER_MODE_SINGLE_NAL;
#endif

    status = pjmedia_h264_packetizer_create(ff->pool, &pktz_cfg, &data->pktz);
    if (status != PJ_SUCCESS) {
        return status;
    }

    /* Apply SDP fmtp to format in codec param */
    if (!ff->param.ignore_fmtp) {
        status = pjmedia_vid_codec_h264_apply_fmtp(&ff->param);
        if (status != PJ_SUCCESS) {
            return status;
        }
    }

    if (ff->param.dir & PJMEDIA_DIR_ENCODING) {
        pjmedia_video_format_detail *vfd;
        AVCodecContext *ctx = ff->enc_ctx;
        const char *profile = NULL;

        vfd = pjmedia_format_get_video_format_detail(&ff->param.enc_fmt, PJ_TRUE);

        /* Override generic params after applying SDP fmtp */
        ctx->width = vfd->size.w;
        ctx->height = vfd->size.h;
        ctx->time_base.num = vfd->fps.denum;
        ctx->time_base.den = vfd->fps.num;

        /* Apply profile. */
        ctx->profile  = data->fmtp.profile_idc;
        switch (ctx->profile) {
        case PROFILE_H264_BASELINE:
            profile = "baseline";
            break;
        case PROFILE_H264_MAIN:
            profile = "main";
            break;
        default:
            break;
        }

        if (profile && !AV_OPT_SET(ctx->priv_data, "profile", profile, 0)) {
            PJ_LOG(3, (THIS_FILE, "Failed to set H264 profile to '%s'",
                       profile));
        }

        /* Apply profile constraint bits. */
        //PJ_TODO(set_h264_constraint_bits_properly_in_ps);
        if (data->fmtp.profile_iop) {
#if defined(FF_PROFILE_H264_CONSTRAINED)
            ctx->profile |= FF_PROFILE_H264_CONSTRAINED;
#endif
        }

        /* Apply profile level. */
        ctx->level    = data->fmtp.level;

        /* Limit NAL unit size as we prefer single NAL unit packetization */
        if (!AV_OPT_SET_INT(ctx->priv_data, "slice-max-size", ff->param.enc_mtu)) {
            PJ_LOG(3, (THIS_FILE, "Failed to set H264 max NAL size to %d",
                       ff->param.enc_mtu));
        }

        /* Apply intra-refresh */
        if (!AV_OPT_SET_INT(ctx->priv_data, "intra-refresh", 1)) {
            PJ_LOG(3, (THIS_FILE, "Failed to set x264 intra-refresh"));
        }

        /* Misc x264 settings (performance, quality, latency, etc).
         * Let's just use the x264 predefined preset & tune.
         */
        if (!AV_OPT_SET(ctx->priv_data, "preset", "veryfast", 0)) {
            PJ_LOG(3, (THIS_FILE, "Failed to set x264 preset 'veryfast'"));
        }

        if (!AV_OPT_SET(ctx->priv_data, "tune", "animation+zerolatency", 0)) {
            PJ_LOG(3, (THIS_FILE, "Failed to set x264 tune 'zerolatency'"));
        }
    }

    if (ff->param.dir & PJMEDIA_DIR_DECODING) {
        AVCodecContext *ctx = ff->dec_ctx;

        /* Apply the "sprop-parameter-sets" fmtp from remote SDP to
         * extradata of ps codec context.
         */
        if (data->fmtp.sprop_param_sets_len) {
            ctx->extradata_size = (int)data->fmtp.sprop_param_sets_len;
            ctx->extradata = data->fmtp.sprop_param_sets;
        }
    }

    return PJ_SUCCESS;
}

static pj_status_t h264_postopen(ps_private *ff)
{
    h264_data *data = (h264_data*)ff->data;
    PJ_UNUSED_ARG(data);
    return PJ_SUCCESS;
}

static FUNC_PACKETIZE(h264_packetize)
{
    h264_data *data = (h264_data*)ff->data;
    return pjmedia_h264_packetize(data->pktz, bits, bits_len, bits_pos,
                                  payload, payload_len);
}

static FUNC_UNPACKETIZE(h264_unpacketize)
{
    h264_data *data = (h264_data*)ff->data;
    return pjmedia_h264_unpacketize(data->pktz, payload, payload_len,
                                    bits, bits_len, bits_pos);
}

#endif /* PJMEDIA_HAS_FFMPEG_CODEC_H264 */

static const ps_codec_desc* find_codec_desc_by_info(
                        const pjmedia_vid_codec_info *info)
{
    int i;

    for (i=0; i<PJ_ARRAY_SIZE(codec_desc); ++i) {
            ps_codec_desc *desc = &codec_desc[i];

            if (desc->enabled &&
                (desc->info.fmt_id == info->fmt_id) &&
            ((desc->info.dir & info->dir) == info->dir) &&
                (desc->info.pt == info->pt) &&
                (desc->info.packings & info->packings)) {
            return desc;
        }
    }

    return NULL;
}

static int find_codec_idx_by_fmt_id(pjmedia_format_id fmt_id)
{
    int i;
    for (i=0; i<PJ_ARRAY_SIZE(codec_desc); ++i) {
        if (codec_desc[i].info.fmt_id == fmt_id) {
            return i;
        }
    }

    return -1;
}

/*
 * Initialize and register ps codec factory to pjmedia endpoint.
 */
PJ_DEF(pj_status_t) pjmedia_codec_ps_vid_init(pjmedia_vid_codec_mgr *mgr,
                                                  pj_pool_factory *pf)
{
    pj_pool_t *pool;
    AVCodec *c;
    pj_status_t status;
    unsigned i;

    if (ps_factory.pool != NULL) {
        /* Already initialized. */
        return PJ_SUCCESS;
    }

    if (!mgr) mgr = pjmedia_vid_codec_mgr_instance();
    PJ_ASSERT_RETURN(mgr, PJ_EINVAL);

    /* Create PS codec factory. */
    ps_factory.base.op = &ps_factory_op;
    ps_factory.base.factory_data = NULL;
    ps_factory.mgr = mgr;
    ps_factory.pf = pf;

    ps_factory.ps_codec_callback = NULL;

    pool = pj_pool_create(pf, "ps codec factory", 256, 256, NULL);
    if (!pool) {
        return PJ_ENOMEM;
    }

    /* Create mutex. */
    status = pj_mutex_create_simple(pool, "ps codec factory", &ps_factory.mutex);
    if (status != PJ_SUCCESS) {
        goto on_error;
    }

    ps_add_ref();
//    avcodec_register_all();

    /* Enum FFMPEG codecs */
    for (c=av_codec_next(NULL); c; c=av_codec_next(c)) {
        ps_codec_desc *desc;
        pjmedia_format_id fmt_id;
        int codec_info_idx;

        if (c->type != AVMEDIA_TYPE_VIDEO) {
            continue;
        }

        /* Video encoder and decoder are usually implemented in separate
         * AVCodec instances. While the codec attributes (e.g: raw formats,
             * supported fps) are in the encoder.
         */

        //PJ_LOG(3, (THIS_FILE, "%s", c->name));
        status = CodecID_to_ps_format_id(c->id, &fmt_id);
        /* Skip if format ID is unknown */
        if (status != PJ_SUCCESS) {
            continue;
        }

        codec_info_idx = find_codec_idx_by_fmt_id(fmt_id);
        /* Skip if codec is unwanted by this wrapper (not listed in
         * the codec info array)
         */
        if (codec_info_idx < 0) {
            continue;
        }

        desc = &codec_desc[codec_info_idx];

        /* Skip duplicated codec implementation */
        if ((AVCODEC_HAS_ENCODE(c) && (desc->info.dir & PJMEDIA_DIR_ENCODING))
            || (AVCODEC_HAS_DECODE(c) && (desc->info.dir & PJMEDIA_DIR_DECODING))) {
            continue;
        }

        /* Get raw/decoded format ids in the encoder */
        if (c->pix_fmts && AVCODEC_HAS_ENCODE(c)) {
            pjmedia_format_id raw_fmt[PJMEDIA_VID_CODEC_MAX_DEC_FMT_CNT];
            unsigned raw_fmt_cnt = 0;
            unsigned raw_fmt_cnt_should_be = 0;
            const enum AVPixelFormat *p = c->pix_fmts;

            for(;(p && *p != -1) && (raw_fmt_cnt < PJMEDIA_VID_CODEC_MAX_DEC_FMT_CNT); ++p) {
                pjmedia_format_id fmt_id;

                raw_fmt_cnt_should_be++;
                status = PixelFormat_to_ps_format_id(*p, &fmt_id);
                if (status != PJ_SUCCESS) {
                    PJ_PERROR(6, (THIS_FILE, status,
                          "Unrecognized ps pixel format %d", *p));
                    continue;
                }

                //raw_fmt[raw_fmt_cnt++] = fmt_id;
                /* Disable some formats due to H.264 error:
                 * x264 [error]: baseline profile doesn't support 4:4:4
                 */
                if (desc->info.pt != PJMEDIA_RTP_PT_H264 || fmt_id != PJMEDIA_FORMAT_RGB24) {
                    raw_fmt[raw_fmt_cnt++] = fmt_id;
                }
            }

            if (raw_fmt_cnt == 0) {
                PJ_LOG(5, (THIS_FILE, "No recognized raw format "
                              "for codec [%s/%s], codec ignored",
                              c->name, c->long_name));
                /* Skip this encoder */
                continue;
            }

            if (raw_fmt_cnt < raw_fmt_cnt_should_be) {
                PJ_LOG(6, (THIS_FILE, "Codec [%s/%s] have %d raw formats, "
                              "recognized only %d raw formats",
                              c->name, c->long_name,
                              raw_fmt_cnt_should_be, raw_fmt_cnt));
            }

            desc->info.dec_fmt_id_cnt = raw_fmt_cnt;
            pj_memcpy(desc->info.dec_fmt_id, raw_fmt, sizeof(raw_fmt[0])*raw_fmt_cnt);
        }

        /* Get supported framerates */
        if (c->supported_framerates) {
            const AVRational *fr = c->supported_framerates;
            while ((fr->num != 0 || fr->den != 0) &&
               desc->info.fps_cnt < PJMEDIA_VID_CODEC_MAX_FPS_CNT)
            {
            desc->info.fps[desc->info.fps_cnt].num = fr->num;
            desc->info.fps[desc->info.fps_cnt].denum = fr->den;
            ++desc->info.fps_cnt;
            ++fr;
            }
        }

        /* Get ps encoder instance */
        if (AVCODEC_HAS_ENCODE(c) && !desc->enc) {
            desc->info.dir |= PJMEDIA_DIR_ENCODING;
            desc->enc = c;
        }

            /* Get ps decoder instance */
        if (AVCODEC_HAS_DECODE(c) && !desc->dec) {
            desc->info.dir |= PJMEDIA_DIR_DECODING;
            desc->dec = c;
        }

        /* Enable this codec when any ps codec instance are recognized
         * and the supported raw formats info has been collected.
         */
        if ((desc->dec || desc->enc) && desc->info.dec_fmt_id_cnt) {
            desc->enabled = PJ_TRUE;
        }

        /* Normalize default value of clock rate */
        if (desc->info.clock_rate == 0) {
            desc->info.clock_rate = 90000;
        }

        /* Set supported packings */
        desc->info.packings |= PJMEDIA_VID_PACKING_WHOLE;
        if (desc->packetize && desc->unpacketize) {
            desc->info.packings |= PJMEDIA_VID_PACKING_PACKETS;
        }
    }

    /* Review all codecs for applying base format, registering format match for
     * SDP negotiation, etc.
     */
    for (i = 0; i < PJ_ARRAY_SIZE(codec_desc); ++i) {
        ps_codec_desc *desc = &codec_desc[i];

        /* Init encoder/decoder description from base format */
        if (desc->base_fmt_id && (!desc->dec || !desc->enc)) {
            ps_codec_desc *base_desc = NULL;
            int base_desc_idx;
            pjmedia_dir copied_dir = PJMEDIA_DIR_NONE;

            base_desc_idx = find_codec_idx_by_fmt_id(desc->base_fmt_id);
            if (base_desc_idx != -1) {
                base_desc = &codec_desc[base_desc_idx];
            }

            if (!base_desc || !base_desc->enabled) {
                continue;
            }

            /* Copy description from base codec */
            if (!desc->info.dec_fmt_id_cnt) {
                desc->info.dec_fmt_id_cnt = base_desc->info.dec_fmt_id_cnt;
                pj_memcpy(desc->info.dec_fmt_id, base_desc->info.dec_fmt_id,
                      sizeof(pjmedia_format_id)*desc->info.dec_fmt_id_cnt);
            }

            if (!desc->info.fps_cnt) {
                desc->info.fps_cnt = base_desc->info.fps_cnt;
                pj_memcpy(desc->info.fps, base_desc->info.fps,
                      sizeof(desc->info.fps[0])*desc->info.fps_cnt);
            }

            if (!desc->info.clock_rate) {
                desc->info.clock_rate = base_desc->info.clock_rate;
            }

            if (!desc->dec && base_desc->dec) {
                copied_dir |= PJMEDIA_DIR_DECODING;
                desc->dec = base_desc->dec;
            }

            if (!desc->enc && base_desc->enc) {
                copied_dir |= PJMEDIA_DIR_ENCODING;
                desc->enc = base_desc->enc;
            }

            desc->info.dir |= copied_dir;
            desc->enabled = (desc->info.dir != PJMEDIA_DIR_NONE);

            /* Set supported packings */
            desc->info.packings |= PJMEDIA_VID_PACKING_WHOLE;
            if (desc->packetize && desc->unpacketize) {
                desc->info.packings |= PJMEDIA_VID_PACKING_PACKETS;
            }

            if (copied_dir != PJMEDIA_DIR_NONE) {
                const char *dir_name[] = {NULL, "encoder", "decoder", "codec"};
                PJ_LOG(5, (THIS_FILE, "The %.*s %s is using base codec (%.*s)",
                       desc->info.encoding_name.slen,
                       desc->info.encoding_name.ptr,
                       dir_name[copied_dir],
                       base_desc->info.encoding_name.slen,
                       base_desc->info.encoding_name.ptr));
            }
        }

        /* Registering format match for SDP negotiation */
        if (desc->sdp_fmt_match) {
            status = pjmedia_sdp_neg_register_fmt_match_cb(
                            &desc->info.encoding_name,
                            desc->sdp_fmt_match);
            pj_assert(status == PJ_SUCCESS);
        }

        /* Print warning about missing encoder/decoder */
        if (!desc->enc) {
            PJ_LOG(4, (THIS_FILE, "Cannot find %.*s encoder in ps library",
                   desc->info.encoding_name.slen,
                   desc->info.encoding_name.ptr));
        }
        if (!desc->dec) {
            PJ_LOG(4, (THIS_FILE, "Cannot find %.*s decoder in ps library",
                   desc->info.encoding_name.slen,
                   desc->info.encoding_name.ptr));
        }
    }

    /* Register codec factory to codec manager. */
    status = pjmedia_vid_codec_mgr_register_factory(mgr, &ps_factory.base);
    if (status != PJ_SUCCESS) {
        goto on_error;
    }

    ps_factory.pool = pool;

    /* Done. */
    return PJ_SUCCESS;

on_error:
    pj_pool_release(pool);
    return status;
}

PJ_DECL(pj_status_t) pjmedia_codec_ps_vid_init_cb(pjmedia_ps_codec_callback *cb) {
    ps_factory.ps_codec_callback = cb;
    return PJ_SUCCESS;
}

/*
 * Unregister PS codecs factory from pjmedia endpoint.
 */
PJ_DEF(pj_status_t) pjmedia_codec_ps_vid_deinit(void)
{
    pj_status_t status = PJ_SUCCESS;

    if (ps_factory.pool == NULL) {
        /* Already deinitialized */
        return PJ_SUCCESS;
    }

    pj_mutex_lock(ps_factory.mutex);

    /* Unregister PS codecs factory. */
    status = pjmedia_vid_codec_mgr_unregister_factory(ps_factory.mgr, &ps_factory.base);

    /* Destroy mutex. */
    pj_mutex_unlock(ps_factory.mutex);
    pj_mutex_destroy(ps_factory.mutex);
    ps_factory.mutex = NULL;

    /* Destroy pool. */
    pj_pool_release(ps_factory.pool);
    ps_factory.pool = NULL;

    ps_dec_ref();

    return status;
}

/*
 * Check if factory can allocate the specified codec.
 */
static pj_status_t ps_test_alloc( pjmedia_vid_codec_factory *factory,
                                  const pjmedia_vid_codec_info *info )
{
    const ps_codec_desc *desc;

    PJ_ASSERT_RETURN(factory==&ps_factory.base, PJ_EINVAL);
    PJ_ASSERT_RETURN(info, PJ_EINVAL);

    desc = find_codec_desc_by_info(info);
    if (!desc) {
        return PJMEDIA_CODEC_EUNSUP;
    }

    return PJ_SUCCESS;
}

/*
 * Generate default attribute.
 */
static pj_status_t ps_default_attr( pjmedia_vid_codec_factory *factory,
                                        const pjmedia_vid_codec_info *info,
                                        pjmedia_vid_codec_param *attr )
{
    const ps_codec_desc *desc;
    unsigned i;

    PJ_ASSERT_RETURN(factory==&ps_factory.base, PJ_EINVAL);
    PJ_ASSERT_RETURN(info && attr, PJ_EINVAL);

    desc = find_codec_desc_by_info(info);
    if (!desc) {
        return PJMEDIA_CODEC_EUNSUP;
    }

    pj_bzero(attr, sizeof(pjmedia_vid_codec_param));

    /* Scan the requested packings and use the lowest number */
    attr->packing = 0;
    for (i=0; i<15; ++i) {
        unsigned packing = (1 << i);
        if ((desc->info.packings & info->packings) & packing) {
            attr->packing = (pjmedia_vid_packing)packing;
            break;
        }
    }
    if (attr->packing == 0) {
        /* No supported packing in info */
        return PJMEDIA_CODEC_EUNSUP;
    }

    /* Direction */
    attr->dir = desc->info.dir;

    /* Encoded format */
    pjmedia_format_init_video(&attr->enc_fmt, desc->info.fmt_id,
                              desc->size.w, desc->size.h,
                              desc->fps.num, desc->fps.denum);

    /* Decoded format */
    pjmedia_format_init_video(&attr->dec_fmt, desc->info.dec_fmt_id[0],
                              desc->size.w, desc->size.h,
                              desc->fps.num, desc->fps.denum);

    /* Decoding fmtp */
    attr->dec_fmtp = desc->dec_fmtp;

    /* Bitrate */
    attr->enc_fmt.det.vid.avg_bps = desc->avg_bps;
    attr->enc_fmt.det.vid.max_bps = desc->max_bps;

    /* Encoding MTU */
    attr->enc_mtu = PJMEDIA_MAX_VID_PAYLOAD_SIZE;

    return PJ_SUCCESS;
}

/*
 * Enum codecs supported by this factory.
 */
static pj_status_t ps_enum_codecs( pjmedia_vid_codec_factory *factory,
                                   unsigned *count,
                                   pjmedia_vid_codec_info codecs[])
{
    unsigned i, max_cnt;

    PJ_ASSERT_RETURN(codecs && *count > 0, PJ_EINVAL);
    PJ_ASSERT_RETURN(factory == &ps_factory.base, PJ_EINVAL);

    max_cnt = PJ_MIN(*count, PJ_ARRAY_SIZE(codec_desc));
    *count = 0;

    for (i=0; i<max_cnt; ++i) {
        if (codec_desc[i].enabled) {
            pj_memcpy(&codecs[*count], &codec_desc[i].info,
                      sizeof(pjmedia_vid_codec_info));
            (*count)++;
        }
    }

    return PJ_SUCCESS;
}

/*
 * Allocate a new codec instance.
 */
static pj_status_t ps_alloc_codec( pjmedia_vid_codec_factory *factory,
                                       const pjmedia_vid_codec_info *info,
                                       pjmedia_vid_codec **p_codec)
{
    ps_private *ff;
    const ps_codec_desc *desc;
    pjmedia_vid_codec *codec;
    pj_pool_t *pool = NULL;
    pj_status_t status = PJ_SUCCESS;

    PJ_ASSERT_RETURN(factory && info && p_codec, PJ_EINVAL);
    PJ_ASSERT_RETURN(factory == &ps_factory.base, PJ_EINVAL);

    desc = find_codec_desc_by_info(info);
    if (!desc) {
        return PJMEDIA_CODEC_EUNSUP;
    }

    /* Create pool for codec instance */
    pool = pj_pool_create(ps_factory.pf, "ps codec", 512, 512, NULL);
    codec = PJ_POOL_ZALLOC_T(pool, pjmedia_vid_codec);
    if (!codec) {
        status = PJ_ENOMEM;
        goto on_error;
    }
    codec->op = &ps_op;
    codec->factory = factory;
    ff = PJ_POOL_ZALLOC_T(pool, ps_private);
    if (!ff) {
        status = PJ_ENOMEM;
        goto on_error;
    }
    codec->codec_data = ff;
    ff->pool = pool;
    ff->enc = desc->enc;
    ff->dec = desc->dec;
    ff->desc = desc;

    *p_codec = codec;
    return PJ_SUCCESS;

on_error:
    if (pool) {
        pj_pool_release(pool);
    }
    return status;
}

/*
 * Free codec.
 */
static pj_status_t ps_dealloc_codec( pjmedia_vid_codec_factory *factory,
                                         pjmedia_vid_codec *codec )
{
    ps_private *ff;
    pj_pool_t *pool;

    PJ_ASSERT_RETURN(factory && codec, PJ_EINVAL);
    PJ_ASSERT_RETURN(factory == &ps_factory.base, PJ_EINVAL);

    /* Close codec, if it's not closed. */
    ff = (ps_private*) codec->codec_data;
    pool = ff->pool;
    codec->codec_data = NULL;
    pj_pool_release(pool);

    return PJ_SUCCESS;
}

/*
 * Init codec.
 */
static pj_status_t ps_codec_init( pjmedia_vid_codec *codec,
                                      pj_pool_t *pool )
{
    PJ_UNUSED_ARG(codec);
    PJ_UNUSED_ARG(pool);
    return PJ_SUCCESS;
}

static void print_ps_err(int err)
{
#if LIBAVCODEC_VER_AT_LEAST(52,72)
    char errbuf[512];
    if (av_strerror(err, errbuf, sizeof(errbuf)) >= 0) {
        PJ_LOG(3, (THIS_FILE, "ps err %d: %s", err, errbuf));
    }
#else
    PJ_LOG(3, (THIS_FILE, "ps err %d", err));
#endif

}

static pj_status_t open_ps_codec(ps_private *ff,
                                     pj_mutex_t *ff_mutex)
{
    enum AVPixelFormat pix_fmt;
    pjmedia_video_format_detail *vfd;
    pj_bool_t enc_opened = PJ_FALSE, dec_opened = PJ_FALSE;
    pj_status_t status;

    /* Get decoded pixel format */
    status = ps_format_id_to_PixelFormat(ff->param.dec_fmt.id,
                                              &pix_fmt);
    if (status != PJ_SUCCESS)
        return status;
    ff->expected_dec_fmt = pix_fmt;

    /* Get video format detail for shortcut access to encoded format */
    vfd = pjmedia_format_get_video_format_detail(&ff->param.enc_fmt,
                                                 PJ_TRUE);

    /* Allocate ps codec context */
    if (ff->param.dir & PJMEDIA_DIR_ENCODING) {
#if LIBAVCODEC_VER_AT_LEAST(53,20)
        ff->enc_ctx = avcodec_alloc_context3(ff->enc);
#else
        ff->enc_ctx = avcodec_alloc_context();
#endif
        if (ff->enc_ctx == NULL)
            goto on_error;
    }
    if (ff->param.dir & PJMEDIA_DIR_DECODING) {
#if LIBAVCODEC_VER_AT_LEAST(53,20)
        ff->dec_ctx = avcodec_alloc_context3(ff->dec);
#else
        ff->dec_ctx = avcodec_alloc_context();
#endif
        if (ff->dec_ctx == NULL)
            goto on_error;
    }

    /* Init generic encoder params */
    if (ff->param.dir & PJMEDIA_DIR_ENCODING) {
        AVCodecContext *ctx = ff->enc_ctx;

        ctx->pix_fmt = pix_fmt;
        ctx->width = vfd->size.w;
        ctx->height = vfd->size.h;
        ctx->time_base.num = vfd->fps.denum;
        ctx->time_base.den = vfd->fps.num;
        if (vfd->avg_bps) {
            ctx->bit_rate = vfd->avg_bps;
            if (vfd->max_bps > vfd->avg_bps) {
                ctx->bit_rate_tolerance = vfd->max_bps - vfd->avg_bps;
            }
        }
        ctx->strict_std_compliance = FF_COMPLIANCE_STRICT;
        ctx->workaround_bugs = FF_BUG_AUTODETECT;
        ctx->opaque = ff;

        /* Set no delay, note that this may cause some codec functionals
         * not working (e.g: rate control).
         */
#if LIBAVCODEC_VER_AT_LEAST(52,113) && !LIBAVCODEC_VER_AT_LEAST(53,20)
        ctx->rc_lookahead = 0;
#endif
    }

    /* Init generic decoder params */
    if (ff->param.dir & PJMEDIA_DIR_DECODING) {
        AVCodecContext *ctx = ff->dec_ctx;

        /* Width/height may be overriden by ps after first decoding. */
        ctx->width  = ctx->coded_width  = ff->param.dec_fmt.det.vid.size.w;
        ctx->height = ctx->coded_height = ff->param.dec_fmt.det.vid.size.h;
        ctx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
        ctx->workaround_bugs = FF_BUG_AUTODETECT;
        ctx->opaque = ff;
    }

    /* Override generic params or apply specific params before opening
     * the codec.
     */
    if (ff->desc->preopen) {
        status = (*ff->desc->preopen)(ff);
        if (status != PJ_SUCCESS) {
            goto on_error;
        }
    }

    /* Open encoder */
    if (ff->param.dir & PJMEDIA_DIR_ENCODING) {
        int err;

        pj_mutex_lock(ff_mutex);
        err = AVCODEC_OPEN(ff->enc_ctx, ff->enc);
        pj_mutex_unlock(ff_mutex);
        if (err < 0) {
            print_ps_err(err);
            status = PJMEDIA_CODEC_EFAILED;
            goto on_error;
        }
        enc_opened = PJ_TRUE;
    }

    /* Open decoder */
    if (ff->param.dir & PJMEDIA_DIR_DECODING) {
        int err;

        pj_mutex_lock(ff_mutex);
        err = AVCODEC_OPEN(ff->dec_ctx, ff->dec);
        pj_mutex_unlock(ff_mutex);
        if (err < 0) {
            print_ps_err(err);
            status = PJMEDIA_CODEC_EFAILED;
            goto on_error;
        }
        dec_opened = PJ_TRUE;
    }

    /* Let the codec apply specific params after the codec opened */
    if (ff->desc->postopen) {
        status = (*ff->desc->postopen)(ff);
        if (status != PJ_SUCCESS) {
            goto on_error;
        }
    }

    return PJ_SUCCESS;

on_error:
    if (ff->enc_ctx) {
        if (enc_opened) {
            avcodec_close(ff->enc_ctx);
        }
        av_free(ff->enc_ctx);
        ff->enc_ctx = NULL;
    }
    if (ff->dec_ctx) {
        if (dec_opened)
            avcodec_close(ff->dec_ctx);
        av_free(ff->dec_ctx);
        ff->dec_ctx = NULL;
    }
    return status;
}

/*
 * Open codec.
 */
static pj_status_t ps_codec_open( pjmedia_vid_codec *codec,
                                      pjmedia_vid_codec_param *attr )
{
    ps_private *ff;
    pj_status_t status;
    pj_mutex_t *ff_mutex;

    PJ_ASSERT_RETURN(codec && attr, PJ_EINVAL);
    ff = (ps_private*)codec->codec_data;

    pj_memcpy(&ff->param, attr, sizeof(*attr));

    /* Normalize encoding MTU in codec param */
    if (attr->enc_mtu > PJMEDIA_MAX_VID_PAYLOAD_SIZE) {
        attr->enc_mtu = PJMEDIA_MAX_VID_PAYLOAD_SIZE;
    }

    /* Open the codec */
    ff_mutex = ((struct ps_factory*)codec->factory)->mutex;
    status = open_ps_codec(ff, ff_mutex);
    if (status != PJ_SUCCESS) {
        goto on_error;
    }

    /* Init format info and apply-param of decoder */
    ff->dec_vfi = pjmedia_get_video_format_info(NULL, ff->param.dec_fmt.id);
    if (!ff->dec_vfi) {
        status = PJ_EINVAL;
        goto on_error;
    }

    pj_bzero(&ff->dec_vafp, sizeof(ff->dec_vafp));
    ff->dec_vafp.size = ff->param.dec_fmt.det.vid.size;
    ff->dec_vafp.buffer = NULL;
    status = (*ff->dec_vfi->apply_fmt)(ff->dec_vfi, &ff->dec_vafp);
    if (status != PJ_SUCCESS) {
        goto on_error;
    }

    /* Init format info and apply-param of encoder */
    ff->enc_vfi = pjmedia_get_video_format_info(NULL, ff->param.dec_fmt.id);
    if (!ff->enc_vfi) {
        status = PJ_EINVAL;
        goto on_error;
    }
    pj_bzero(&ff->enc_vafp, sizeof(ff->enc_vafp));
    ff->enc_vafp.size = ff->param.enc_fmt.det.vid.size;
    ff->enc_vafp.buffer = NULL;
    status = (*ff->enc_vfi->apply_fmt)(ff->enc_vfi, &ff->enc_vafp);
    if (status != PJ_SUCCESS) {
        goto on_error;
    }

    /* Alloc buffers if needed */
    ff->whole = (ff->param.packing == PJMEDIA_VID_PACKING_WHOLE);
    if (!ff->whole) {
        ff->enc_buf_size = (unsigned)ff->enc_vafp.framebytes;
        ff->enc_buf = pj_pool_alloc(ff->pool, ff->enc_buf_size);

        ff->dec_buf_size = (unsigned)ff->dec_vafp.framebytes;
        ff->dec_buf = pj_pool_alloc(ff->pool, ff->dec_buf_size);
    }

    /* Update codec attributes, e.g: encoding format may be changed by
     * SDP fmtp negotiation.
     */
    pj_memcpy(attr, &ff->param, sizeof(*attr));

    return PJ_SUCCESS;

on_error:
    ps_codec_close(codec);
    return status;
}

/*
 * Close codec.
 */
static pj_status_t ps_codec_close( pjmedia_vid_codec *codec )
{
    ps_private *ff;
    pj_mutex_t *ff_mutex;

    PJ_ASSERT_RETURN(codec, PJ_EINVAL);
    ff = (ps_private*)codec->codec_data;
    ff_mutex = ((struct ps_factory*)codec->factory)->mutex;

    pj_mutex_lock(ff_mutex);
    if (ff->enc_ctx) {
        avcodec_close(ff->enc_ctx);
        av_free(ff->enc_ctx);
    }
    if (ff->dec_ctx && ff->dec_ctx!=ff->enc_ctx) {
        avcodec_close(ff->dec_ctx);
        av_free(ff->dec_ctx);
    }
    ff->enc_ctx = NULL;
    ff->dec_ctx = NULL;
    pj_mutex_unlock(ff_mutex);

    return PJ_SUCCESS;
}


/*
 * Modify codec settings.
 */
static pj_status_t  ps_codec_modify( pjmedia_vid_codec *codec,
                                         const pjmedia_vid_codec_param *attr)
{
    ps_private *ff = (ps_private*)codec->codec_data;

    PJ_UNUSED_ARG(attr);
    PJ_UNUSED_ARG(ff);

    return PJ_ENOTSUP;
}

static pj_status_t  ps_codec_get_param(pjmedia_vid_codec *codec,
                                           pjmedia_vid_codec_param *param)
{
    ps_private *ff;

    PJ_ASSERT_RETURN(codec && param, PJ_EINVAL);

    ff = (ps_private*)codec->codec_data;
    pj_memcpy(param, &ff->param, sizeof(*param));

    return PJ_SUCCESS;
}


static pj_status_t  ps_packetize ( pjmedia_vid_codec *codec,
                                       pj_uint8_t *bits,
                                       pj_size_t bits_len,
                                       unsigned *bits_pos,
                                       const pj_uint8_t **payload,
                                       pj_size_t *payload_len)
{
    ps_private *ff = (ps_private*)codec->codec_data;

    if (ff->desc->packetize) {
        return (*ff->desc->packetize)(ff, bits, bits_len, bits_pos,
                                      payload, payload_len);
    }

    return PJ_ENOTSUP;
}


/*
 * Encode frames.
 */
static pj_status_t ps_codec_encode_whole(pjmedia_vid_codec *codec,
                                             const pjmedia_vid_encode_opt *opt,
                                             const pjmedia_frame *input,
                                             unsigned output_buf_len,
                                             pjmedia_frame *output)
{
    ps_private *ff = (ps_private*)codec->codec_data;
    pj_uint8_t *p = (pj_uint8_t*)input->buf;
    AVFrame avframe;
    AVPacket avpacket;
    int err, got_packet;
    //AVRational src_timebase;
    /* For some reasons (e.g: SSE/MMX usage), the avcodec_encode_video() must
     * have stack aligned to 16 bytes. Let's try to be safe by preparing the
     * 16-bytes aligned stack here, in case it's not managed by the ps.
     */
    PJ_ALIGN_DATA(pj_uint32_t i[4], 16);

    if ((long)(pj_ssize_t)i & 0xF) {
        PJ_LOG(2,(THIS_FILE, "Stack alignment fails"));
    }

    /* Check if encoder has been opened */
    PJ_ASSERT_RETURN(ff->enc_ctx, PJ_EINVALIDOP);

    pj_bzero(&avframe, sizeof(avframe));
    av_frame_unref(&avframe);

    // Let ps manage the timestamps
    /*
    src_timebase.num = 1;
    src_timebase.den = ff->desc->info.clock_rate;
    avframe.pts = av_rescale_q(input->timestamp.u64, src_timebase,
                               ff->enc_ctx->time_base);
    */

    for (i[0] = 0; i[0] < ff->enc_vfi->plane_cnt; ++i[0]) {
        avframe.data[i[0]] = p;
        avframe.linesize[i[0]] = ff->enc_vafp.strides[i[0]];
        p += ff->enc_vafp.plane_bytes[i[0]];
    }

    /* Force keyframe */
    if (opt && opt->force_keyframe) {
        avframe.pict_type = AV_PICTURE_TYPE_I;
    }

    av_init_packet(&avpacket);
    avpacket.data = (pj_uint8_t*)output->buf;
    avpacket.size = output_buf_len;

    err = avcodec_encode_video2(ff->enc_ctx, &avpacket, &avframe, &got_packet);
    if (!err && got_packet)
        err = avpacket.size;

    if (err < 0) {
        print_ps_err(err);
        return PJMEDIA_CODEC_EFAILED;
    } else {
        pj_bool_t has_key_frame = PJ_FALSE;
        output->size = err;
        output->bit_info = 0;

        has_key_frame = (avpacket.flags & AV_PKT_FLAG_KEY);

        if (has_key_frame) {
            output->bit_info |= PJMEDIA_VID_FRM_KEYFRAME;
        }
    }

    return PJ_SUCCESS;
}

static pj_status_t ps_codec_encode_begin(pjmedia_vid_codec *codec,
                                             const pjmedia_vid_encode_opt *opt,
                                             const pjmedia_frame *input,
                                             unsigned out_size,
                                             pjmedia_frame *output,
                                             pj_bool_t *has_more)
{
    ps_private *ff = (ps_private*)codec->codec_data;
    pj_status_t status;

    *has_more = PJ_FALSE;

    if (ff->whole) {
        status = ps_codec_encode_whole(codec, opt, input, out_size, output);
    } else {
        pjmedia_frame whole_frm;
        const pj_uint8_t *payload;
        pj_size_t payload_len;

        pj_bzero(&whole_frm, sizeof(whole_frm));
        whole_frm.buf = ff->enc_buf;
        whole_frm.size = ff->enc_buf_size;
        status = ps_codec_encode_whole(codec, opt, input,
                                       (unsigned)whole_frm.size,
                                       &whole_frm);
        if (status != PJ_SUCCESS) {
            return status;
        }

        ff->enc_buf_is_keyframe = (whole_frm.bit_info &
                                   PJMEDIA_VID_FRM_KEYFRAME);
        ff->enc_frame_len = (unsigned)whole_frm.size;
        ff->enc_processed = 0;
        status = ps_packetize(codec, (pj_uint8_t*)whole_frm.buf,
                                  whole_frm.size, &ff->enc_processed,
                                  &payload, &payload_len);
        if (status != PJ_SUCCESS) {
            return status;
        }

        if (out_size < payload_len) {
            return PJMEDIA_CODEC_EFRMTOOSHORT;
        }

        output->type = PJMEDIA_FRAME_TYPE_VIDEO;
        pj_memcpy(output->buf, payload, payload_len);
        output->size = payload_len;

        if (ff->enc_buf_is_keyframe) {
            output->bit_info |= PJMEDIA_VID_FRM_KEYFRAME;
        }

        *has_more = (ff->enc_processed < ff->enc_frame_len);
    }

    return status;
}

static pj_status_t ps_codec_encode_more(pjmedia_vid_codec *codec,
                                            unsigned out_size,
                                            pjmedia_frame *output,
                                            pj_bool_t *has_more)
{
    ps_private *ff = (ps_private*)codec->codec_data;
    const pj_uint8_t *payload;
    pj_size_t payload_len;
    pj_status_t status;

    *has_more = PJ_FALSE;

    if (ff->enc_processed >= ff->enc_frame_len) {
        /* No more frame */
        return PJ_EEOF;
    }

    status = ps_packetize(codec, (pj_uint8_t*)ff->enc_buf,
                              ff->enc_frame_len, &ff->enc_processed,
                              &payload, &payload_len);
    if (status != PJ_SUCCESS) {
        return status;
    }

    if (out_size < payload_len) {
        return PJMEDIA_CODEC_EFRMTOOSHORT;
    }

    output->type = PJMEDIA_FRAME_TYPE_VIDEO;
    pj_memcpy(output->buf, payload, payload_len);
    output->size = payload_len;

    if (ff->enc_buf_is_keyframe) {
        output->bit_info |= PJMEDIA_VID_FRM_KEYFRAME;
    }

    *has_more = (ff->enc_processed < ff->enc_frame_len);

    return PJ_SUCCESS;
}


static pj_status_t check_decode_result(pjmedia_vid_codec *codec,
                                       const pj_timestamp *ts,
                                       pj_bool_t got_keyframe)
{
    ps_private *ff = (ps_private*)codec->codec_data;
    pjmedia_video_apply_fmt_param *vafp = &ff->dec_vafp;
    pjmedia_event event;

    /* Check for format change.
     * Decoder output format is set by libavcodec, in case it is different
     * to the configured param.
     */
    if (ff->dec_ctx->pix_fmt != ff->expected_dec_fmt ||
        ff->dec_ctx->width != (int)vafp->size.w ||
        ff->dec_ctx->height != (int)vafp->size.h)
    {
        pjmedia_format_id new_fmt_id;
        pj_status_t status;

        /* Get current raw format id from ps decoder context */
        status = PixelFormat_to_ps_format_id(ff->dec_ctx->pix_fmt,
                                                  &new_fmt_id);
        if (status != PJ_SUCCESS)
            return status;

        /* Update decoder format in param */
                ff->param.dec_fmt.id = new_fmt_id;
        ff->param.dec_fmt.det.vid.size.w = ff->dec_ctx->width;
        ff->param.dec_fmt.det.vid.size.h = ff->dec_ctx->height;
        ff->expected_dec_fmt = ff->dec_ctx->pix_fmt;

        /* Re-init format info and apply-param of decoder */
        ff->dec_vfi = pjmedia_get_video_format_info(NULL, ff->param.dec_fmt.id);
        if (!ff->dec_vfi) {
            return PJ_ENOTSUP;
        }
        pj_bzero(&ff->dec_vafp, sizeof(ff->dec_vafp));
        ff->dec_vafp.size = ff->param.dec_fmt.det.vid.size;
        ff->dec_vafp.buffer = NULL;
        status = (*ff->dec_vfi->apply_fmt)(ff->dec_vfi, &ff->dec_vafp);
        if (status != PJ_SUCCESS) {
            return status;
        }

        /* Realloc buffer if necessary */
        if (ff->dec_vafp.framebytes > ff->dec_buf_size) {
            PJ_LOG(5,(THIS_FILE, "Reallocating decoding buffer %u --> %u",
                       (unsigned)ff->dec_buf_size,
                       (unsigned)ff->dec_vafp.framebytes));
            ff->dec_buf_size = (unsigned)ff->dec_vafp.framebytes;
            ff->dec_buf = pj_pool_alloc(ff->pool, ff->dec_buf_size);
        }

        /* Broadcast format changed event */
        pjmedia_event_init(&event, PJMEDIA_EVENT_FMT_CHANGED, ts, codec);
        event.data.fmt_changed.dir = PJMEDIA_DIR_DECODING;
        pj_memcpy(&event.data.fmt_changed.new_fmt, &ff->param.dec_fmt,
                  sizeof(ff->param.dec_fmt));
        pjmedia_event_publish(NULL, codec, &event, 0);
    }

    /* Check for missing/found keyframe */
    if (got_keyframe) {
        pj_get_timestamp(&ff->last_dec_keyframe_ts);

        /* Broadcast keyframe event */
        pjmedia_event_init(&event, PJMEDIA_EVENT_KEYFRAME_FOUND, ts, codec);
        pjmedia_event_publish(NULL, codec, &event, 0);
    } else if (ff->last_dec_keyframe_ts.u64 == 0) {
        /* Broadcast missing keyframe event */
        pjmedia_event_init(&event, PJMEDIA_EVENT_KEYFRAME_MISSING, ts, codec);
        pjmedia_event_publish(NULL, codec, &event, 0);
    }

    return PJ_SUCCESS;
}

/*
 * Decode frame.
 */
static pj_status_t ps_codec_decode_whole(pjmedia_vid_codec *codec,
                                             const pjmedia_frame *input,
                                             unsigned output_buf_len,
                                             pjmedia_frame *output)
{
    ps_private *ff = (ps_private*)codec->codec_data;
    AVFrame avframe;
    AVPacket avpacket;
    int err, got_picture;

    /* Check if decoder has been opened */
    PJ_ASSERT_RETURN(ff->dec_ctx, PJ_EINVALIDOP);

    /* Reset output frame bit info */
    output->bit_info = 0;

    /* Validate output buffer size */
    // Do this validation later after getting decoding result, where the real
    // decoded size will be assured.
    //if (ff->dec_vafp.framebytes > output_buf_len)
        //return PJ_ETOOSMALL;

    /* Init frame to receive the decoded data, the ps codec context will
     * automatically provide the decoded buffer (single buffer used for the
     * whole decoding session, and seems to be freed when the codec context
     * closed).
     */

    pj_bzero(&avframe, sizeof(avframe));
    av_frame_unref(&avframe);

    /* Init packet, the container of the encoded data */
    av_init_packet(&avpacket);
    avpacket.data = (pj_uint8_t*)input->buf;
    avpacket.size = (int)input->size;

    /* ps warns:
     * - input buffer padding, at least FF_INPUT_BUFFER_PADDING_SIZE
     * - null terminated
     * Normally, encoded buffer is allocated more than needed, so lets just
     * bzero the input buffer end/pad, hope it will be just fine.
     */
    pj_bzero(avpacket.data+avpacket.size, FF_INPUT_BUFFER_PADDING_SIZE);

    output->bit_info = 0;
    output->timestamp = input->timestamp;

#if LIBAVCODEC_VER_AT_LEAST(52,72)
    //avpacket.flags = AV_PKT_FLAG_KEY;
#else
    avpacket.flags = 0;
#endif

#if LIBAVCODEC_VER_AT_LEAST(52,72)
    err = avcodec_decode_video2(ff->dec_ctx, &avframe,
                                &got_picture, &avpacket);
#else
    err = avcodec_decode_video(ff->dec_ctx, &avframe,
                               &got_picture, avpacket.data, avpacket.size);
#endif
    if (err < 0) {
        pjmedia_event event;

        output->type = PJMEDIA_FRAME_TYPE_NONE;
        output->size = 0;
        print_ps_err(err);

        /* Broadcast missing keyframe event */
        pjmedia_event_init(&event, PJMEDIA_EVENT_KEYFRAME_MISSING,
                           &input->timestamp, codec);
        pjmedia_event_publish(NULL, codec, &event, 0);

        return PJMEDIA_CODEC_EBADBITSTREAM;
    } else if (got_picture) {
        pjmedia_video_apply_fmt_param *vafp = &ff->dec_vafp;
        pj_uint8_t *q = (pj_uint8_t*)output->buf;
        unsigned i;
        pj_status_t status;

        /* Check decoding result, e.g: see if the format got changed,
         * keyframe found/missing.
         */
        status = check_decode_result(codec, &input->timestamp, avframe.key_frame);
        if (status != PJ_SUCCESS) {
            return status;
        }

        /* Check provided buffer size */
        if (vafp->framebytes > output_buf_len) {
            return PJ_ETOOSMALL;
        }

        /* Get the decoded data */
        for (i = 0; i < ff->dec_vfi->plane_cnt; ++i) {
            pj_uint8_t *p = avframe.data[i];

            /* The decoded data may contain padding */
            if (avframe.linesize[i]!=vafp->strides[i]) {
                /* Padding exists, copy line by line */
                pj_uint8_t *q_end;

                q_end = q+vafp->plane_bytes[i];
                while(q < q_end) {
                    pj_memcpy(q, p, vafp->strides[i]);
                    q += vafp->strides[i];
                    p += avframe.linesize[i];
                }
            } else {
                /* No padding, copy the whole plane */
                pj_memcpy(q, p, vafp->plane_bytes[i]);
                q += vafp->plane_bytes[i];
            }
        }

        output->type = PJMEDIA_FRAME_TYPE_VIDEO;
        output->size = vafp->framebytes;
    } else {
        output->type = PJMEDIA_FRAME_TYPE_NONE;
        output->size = 0;
    }

    return PJ_SUCCESS;
}



static pj_status_t check_ps_codec_valid(ps_codec *ppc) {
    PJ_ASSERT_RETURN(ppc->pkt_idx < ppc->pkt_count, PJ_EINVAL);
    if (ppc->current_buf == NULL || ppc->packets[ppc->pkt_idx].size == 0) {
#ifdef TRACE_PS
        int idx = ppc->pkt_idx;
        PJ_LOG(3,(THIS_FILE, "ps decode pkt empty. ts: %d, pkt_cnt: %d, pkt_idx: %d, rtp_seq: %d, pre_seq: %d",
                            ppc->packets[idx].timestamp.u64, ppc->pkt_count, idx,
                            ppc->packets[idx].rtp_seq, idx > 0 ? ppc->packets[idx-1].rtp_seq : -1));
#endif // TRACE_PS
        return PJ_EINVAL;
    }

    return PJ_SUCCESS;
}
/*
*   then get_buf == null and op == PS_CODEC_GET, copy data to temp buf
*/
static pj_status_t _do_op_ps_codec(ps_codec *ppc, pj_size_t len, enum ps_codec_op op, pj_uint8_t **get_buf) {
    switch (op) {
        case PS_CODEC_OP_GET:
            if (get_buf == NULL) {
                if (ppc->temp_data_len + len > MAX_GET_OR_SKIP_BUF_SIZE) {
                    PJ_LOG(3,(THIS_FILE, "Copy to skip buf size too large. size: %d", ppc->temp_data_len + len));
                    return PJ_ETOOSMALL;
                }

                pj_memcpy(ppc->temp_buf + ppc->temp_data_len, ppc->current_buf, len);
                ppc->temp_data_len += len;
            } else {
                *get_buf = ppc->current_buf;
            }
            break;
        case PS_CODEC_OP_COPY:
            if (ppc->dec_buf_size < ppc->dec_data_len + len) {
                PJ_LOG(3,(THIS_FILE, "Decoding buffer overflow. dec buf size: %d, need buf len: %d", ppc->dec_buf_size, ppc->dec_data_len + len));
                return PJ_ETOOSMALL;
            }

            pj_memcpy(ppc->dec_buf + ppc->dec_data_len, ppc->current_buf, len);
            ppc->dec_data_len += len;
            break;
        default:
            // do nothing
            break;
    }

    return PJ_SUCCESS;
}

static pj_status_t op_ps_codec(ps_codec *ppc, pj_size_t len, enum ps_codec_op op, pj_uint8_t **get_buf) {
    PJ_ASSERT_RETURN(op == PS_CODEC_OP_GET || op == PS_CODEC_OP_SEEK || op == PS_CODEC_OP_COPY, PJ_EINVAL);
    PJ_ASSERT_RETURN(ppc->current_buf != NULL, PJ_EINVAL);
    PJ_ASSERT_RETURN(ppc->pkt_idx < ppc->pkt_count, PJ_EINVAL);

    if (ppc->remain_buf_len > len) {
        if (_do_op_ps_codec(ppc, len, op, get_buf) != PJ_SUCCESS) {
            return PJ_ETOOSMALL;
        }

        ppc->current_buf += len;
        ppc->remain_buf_len -= len;

        return PJ_SUCCESS;
    } else {
        int remain_len = len;
        int copy_or_skip_len = ppc->remain_buf_len;
        ppc->temp_data_len = 0;

        do {
            if (ppc->current_buf == NULL) {
                return PJ_EINVAL;
            }

            if (_do_op_ps_codec(ppc, copy_or_skip_len, op, NULL) != PJ_SUCCESS) {
                return PJ_ETOOSMALL;
            }

            ppc->current_buf += copy_or_skip_len;
            ppc->remain_buf_len -= copy_or_skip_len;
            remain_len -= copy_or_skip_len;

            if (ppc->remain_buf_len < 0) {
                return PJ_EBUG;
            }

            if (ppc->remain_buf_len == 0) {
                if (ppc->pkt_idx + 1 == ppc->pkt_count) {
                    // this is the last packet
                    ppc->current_buf = NULL;

                    if (remain_len > 0) {
                        int idx = ppc->pkt_idx;
                        PJ_LOG(2, (THIS_FILE, "ps decode at the end of packet. ts: %d, pkt_cnt: %d, pkt_idx: %d, rtp_seq: %d, pre_seq: %d, remain_len: %d",
                                                    ppc->packets[idx].timestamp.u64, ppc->pkt_count, idx,
                                                    ppc->packets[idx].rtp_seq, idx > 0 ? ppc->packets[idx-1].rtp_seq : -1, remain_len));
                        return PJ_EEOF;
                    }
                } else {
                    // move to next packet
                    ppc->pkt_idx++;
                    ppc->current_buf = ppc->packets[ppc->pkt_idx].buf;
                    ppc->remain_buf_len = ppc->packets[ppc->pkt_idx].size;

                    if (check_ps_codec_valid(ppc) != PJ_SUCCESS) {
                        return PJ_EINVAL;
                    }

                    if (remain_len >= ppc->remain_buf_len) {
                        copy_or_skip_len = ppc->remain_buf_len;
                    } else {
                        copy_or_skip_len = remain_len;
                    }
                }
            }
        } while (remain_len > 0);

        if (op == PS_CODEC_OP_GET) {
            *get_buf = ppc->temp_buf;
        }

        return PJ_SUCCESS;
    }
}


#define CHECK_START_CODE_PREFIX(buf) (*(buf+0) == 0x00 && *(buf+1) == 0x00 && *(buf+2) == 0x01)
#define CHECK_NAL_START_CODE(buf) (*(buf+0) == 0x00 && *(buf+1) == 0x00 && *(buf+2) == 0x00 && *(buf+3) == 0x01)
#define LOG_PS_CODEC_INFO(lvl, msg) idx = ppc->pkt_idx; \
                PJ_LOG(lvl, (THIS_FILE, "%s ts: %d, pkt_cnt: %d, pkt_idx: %d, remain_buf_len: %d, rtp_seq: %d, pre_seq: %d", msg, \
                                                    ppc->packets[idx].timestamp.u64, ppc->pkt_count, idx, ppc->remain_buf_len, \
                                                    ppc->packets[idx].rtp_seq, idx > 0 ? ppc->packets[idx-1].rtp_seq : -1))

static pj_status_t  ps_unpacketize(pjmedia_vid_codec *codec,
                                       ps_codec      *ppc)
{
    int idx = ppc->pkt_idx;

    if (ppc->current_buf == NULL) {
        LOG_PS_CODEC_INFO(3, "First package is nil.");
        return PJ_EINVAL;
    }

    pj_uint8_t * buf = NULL;
    ps_private *ff = (ps_private*)codec->codec_data;
    ppc->total_video_pes_len = 0;

    while (ppc->current_buf != NULL) {
        if (op_ps_codec(ppc, 4, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
            LOG_PS_CODEC_INFO(3, "Get start code error.");
            return PJ_EINVAL;
        }

        if (CHECK_START_CODE_PREFIX(buf)) {
            switch (*(buf+3)) {
                case 0xBA: // pack header start code
                    // ps pack header, length is 14 -> 4 + 10
                    if (op_ps_codec(ppc, 10, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Get ps header error.");
                        return PJ_EINVAL;
                    }

                    int pack_stuffing_length = (*(buf + 9) & 0x07);

                    if (op_ps_codec(ppc, pack_stuffing_length, PS_CODEC_OP_SEEK, NULL) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Get ps header stuffing error.");
                        return PJ_EINVAL;
                    }
                    break;

                case 0xBB: // system header start code
                    if (op_ps_codec(ppc, 2, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Get system header length error.");
                        return PJ_EINVAL;
                    }

                    int system_header_len = pj_ntohs(*(pj_uint16_t*)buf);

                    if (op_ps_codec(ppc, system_header_len, PS_CODEC_OP_SEEK, NULL) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Skip system header error.");
                        return PJ_EINVAL;
                    }

                    ppc->is_i_frame = PJ_TRUE;
                    break;

                case 0xBC: // program stream map start code
                    if (ppc->is_i_frame == PJ_FALSE) {
                        LOG_PS_CODEC_INFO(3, "I frame miss system header.");
                    }

                    ppc->is_i_frame = PJ_TRUE;

                    if (op_ps_codec(ppc, 2, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Get program stream map length error.");
                        return PJ_EINVAL;
                    }

                    int program_stream_map_len = pj_ntohs(*(pj_uint16_t*)buf);

                    if (op_ps_codec(ppc, 2, PS_CODEC_OP_SEEK, NULL) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Skip program stream map params error.");
                        return PJ_EINVAL;
                    }

                    program_stream_map_len -= 2;
                    /*****************  program_stream_info_len ******************/
                    if (op_ps_codec(ppc, 2, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Get program stream info length error.");
                        return PJ_EINVAL;
                    }
                    program_stream_map_len -= 2;
                    int program_stream_info_len = pj_ntohs(*(pj_uint16_t*)buf);
                    if (op_ps_codec(ppc, program_stream_info_len, PS_CODEC_OP_SEEK, NULL) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Skip program stream info error.");
                        return PJ_EINVAL;
                    }
                    program_stream_map_len -= program_stream_info_len;

                    /*****************  check elementary_stream_map ******************/
                    if (op_ps_codec(ppc, 2, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Get elementary stream map length error.");
                        return PJ_EINVAL;
                    }
                    program_stream_map_len -= 2;
                    int elementary_stream_map_len = pj_ntohs(*(pj_uint16_t*)buf);

                    while (elementary_stream_map_len > 0) {
                        if (op_ps_codec(ppc, 2, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                            LOG_PS_CODEC_INFO(3, "Get elementary stream map type error.");
                            return PJ_EINVAL;
                        }
                        elementary_stream_map_len -= 2;
                        program_stream_map_len -= 2;

                        if (set_ps_codec_id_from_psm_info(ppc, buf) != PJ_SUCCESS) {
                            PJ_LOG(1, (THIS_FILE, "Unsupported stream type: %x, and stream id: %x", *buf, *(buf+1)));
                            return PJ_EINVAL;
                        }

                        if (op_ps_codec(ppc, 2, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                            LOG_PS_CODEC_INFO(3, "Get elementary stream map type len error.");
                            return PJ_EINVAL;
                        }
                        elementary_stream_map_len -=2;
                        program_stream_map_len -= 2;

                        int elementary_stream_info_length = pj_ntohs(*(pj_uint16_t*)buf);
                        if (op_ps_codec(ppc, elementary_stream_info_length, PS_CODEC_OP_SEEK, NULL) != PJ_SUCCESS) {
                            LOG_PS_CODEC_INFO(3, "Skip elementary stream info length error.");
                            return PJ_EINVAL;
                        }

                        elementary_stream_map_len -= elementary_stream_info_length;
                        program_stream_map_len -= elementary_stream_info_length;
                    }

                    if (program_stream_map_len != 4) {
                        LOG_PS_CODEC_INFO(3, "Program stream map length error, crc must be 4 len.");
                    } else {
                        if (op_ps_codec(ppc, 4, PS_CODEC_OP_SEEK, NULL) != PJ_SUCCESS) {
                            LOG_PS_CODEC_INFO(3, "Skip elementary stream info crc error.");
                            return PJ_EINVAL;
                        }
                    }

                    break;

                case 0xE0: // pes video header start code
                    if (op_ps_codec(ppc, 2, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Get pes video header length error.");
                        return PJ_EINVAL;
                    }

                    int video_pes_packet_length = pj_ntohs(*(pj_uint16_t*)buf);

                    if (op_ps_codec(ppc, 3, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Get video pes header data length error.");
                        return PJ_EINVAL;
                    }

                    int video_pes_header_data_length = *(buf + 2);

                    // skip video pes header
                    if (op_ps_codec(ppc, video_pes_header_data_length, PS_CODEC_OP_SEEK, NULL) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Skip video pes header data length error.");
                        return PJ_EINVAL;
                    }

                    // copy video data
                    int video_data_len = video_pes_packet_length - 2 - 1 - video_pes_header_data_length;

                    // NAL start code is 0x00, 0x00, 0x01, change from 4 bit to 3 bit
                    ppc->total_video_pes_len += video_data_len;

                    if (op_ps_codec(ppc, 4, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Get nal start code error.");
                        return PJ_EINVAL;
                    }

                    video_data_len -= 4;
                    if (CHECK_NAL_START_CODE(buf)) {
                        // NAL start code is 0x00, 0x00, 0x01, change from 4 bit to 3 bit
                        ppc->total_video_pes_len -= 1;

                        pj_bool_t has_more_pkt_in_current_frame = video_data_len < ppc->remain_buf_len;
                        int do_unpack_len = has_more_pkt_in_current_frame ? video_data_len : ppc->remain_buf_len;

                        if (ff->desc->unpacketize) {
                            pj_status_t ret = (*ff->desc->unpacketize)(ff, ppc->current_buf, do_unpack_len,
                                              ppc->dec_buf, ppc->dec_buf_size, &ppc->dec_data_len);
                            if (ret != PJ_SUCCESS) {
                                PJ_LOG(2, (THIS_FILE, "Unpacketize error: %d", ret));
                            }
                            if (op_ps_codec(ppc, do_unpack_len, PS_CODEC_OP_SEEK, NULL) != PJ_SUCCESS) {
                                LOG_PS_CODEC_INFO(3, "Seek copied data error.");
                                return PJ_EINVAL;
                            }
                        }

                        if (has_more_pkt_in_current_frame) {
                            continue;
                        } else {
                            if (op_ps_codec(ppc, video_data_len - do_unpack_len, PS_CODEC_OP_COPY, NULL) != PJ_SUCCESS) {
                                LOG_PS_CODEC_INFO(3, "Copy data error.");
                                return PJ_EINVAL;
                            }
                        }
                    } else {
                        pj_memcpy(ppc->dec_buf + ppc->dec_data_len , buf, 4);
                        ppc->dec_data_len += 4;
                        if (op_ps_codec(ppc, video_data_len, PS_CODEC_OP_COPY, NULL) != PJ_SUCCESS) {
                            LOG_PS_CODEC_INFO(3, "Copy data error.");
                            return PJ_EINVAL;
                        }
                    }

                    break;

                case 0xC0: // pes audio header start code
                    if (op_ps_codec(ppc, 2, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Get pes audio header length error.");
                        return PJ_EINVAL;
                    }

                    int audio_pes_packet_length = pj_ntohs(*(pj_uint16_t*)buf);

                    if (op_ps_codec(ppc, 3, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Get pes audio header data length error.");
                        return PJ_EINVAL;
                    }

                    int audio_pes_header_data_length = *(buf + 2);

                    // skip audio pes header
                    if (op_ps_codec(ppc, audio_pes_header_data_length, PS_CODEC_OP_SEEK, NULL) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Skip pes audio header data error.");
                        return PJ_EINVAL;
                    }

                    // skip audio pes data
                    int audio_data_len = audio_pes_packet_length - 2 - 1 - audio_pes_header_data_length;
                    if (op_ps_codec(ppc, audio_data_len, PS_CODEC_OP_SEEK, NULL) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Skip pes audio data error.");
                        return PJ_EINVAL;
                    }
                    break;

                case 0xBD: // ps tail header start code
                    if (op_ps_codec(ppc, 2, PS_CODEC_OP_GET, &buf) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Get pes tail header length error.");
                        return PJ_EINVAL;
                    }

                    int program_stream_tail_len = pj_ntohs(*(pj_uint16_t*)buf);

                    if (op_ps_codec(ppc, program_stream_tail_len, PS_CODEC_OP_SEEK, NULL) != PJ_SUCCESS) {
                        LOG_PS_CODEC_INFO(3, "Skip pes tail data error.");
                        return PJ_EINVAL;
                    }
                    break;
                default:
                    idx = ppc->pkt_idx;
                    PJ_LOG(1, (THIS_FILE, "Unknown payload type: %x, ts: %d, idx:%d, len: %d", *buf+3,
                            ppc->packets[idx].timestamp.u64, idx, ppc->remain_buf_len));
                    return PJ_EBUG;
            }
        } else {
            LOG_PS_CODEC_INFO(3, "Couldn't get start code.");
            return PJ_EINVAL;
        }
    }

    return PJ_SUCCESS;
}

static pj_status_t ps_codec_decode( pjmedia_vid_codec *codec,
                                        pj_size_t pkt_count,
                                        pjmedia_frame packets[],
                                        unsigned out_size,
                                        pjmedia_frame *output)
{
    ps_private *ff = (ps_private*)codec->codec_data;
    pj_status_t status;

    PJ_ASSERT_RETURN(codec && pkt_count > 0 && packets && output, PJ_EINVAL);

    if (ff->whole) {
        // assert not support
        pj_assert(pkt_count==1);
        return ps_codec_decode_whole(codec, &packets[0], out_size, output);
    } else {
        pjmedia_frame whole_frm;

        ps_codec ps;
        ps.packets = packets;
        ps.pkt_count = pkt_count;
        ps.pkt_idx = 0;
        ps.current_buf = packets[0].buf;
        ps.remain_buf_len = packets[0].size;
        ps.dec_buf = ff->dec_buf;
        ps.dec_buf_size = ff->dec_buf_size;
        ps.dec_data_len = 0;
        ps.is_i_frame = PJ_FALSE;
        // copy cname from buf
        if (strlen(output->buf) > 0) {
            for (int i = 0; i < pjsua_var.call_cnt; i++) {
                if (pj_strcmp2(&pjsua_var.calls[i].cname, output->buf) == 0) {
                    pj_str_t *remote_info = &pjsua_var.calls[0].inv->dlg->remote.info_str;
                    if (remote_info->slen +1 < PJSIP_MAX_URL_SIZE) {
                        pj_memcpy(ps.callee_id, remote_info->ptr, remote_info->slen);
                        ps.callee_id[remote_info->slen] = 0;
                    }
                }
            }
        }

        status = ps_unpacketize(codec, &ps);
        if (status != PJ_SUCCESS) {
#ifdef TRACE_PS
//            FILE *fptr;
//            char filename[256];
//            sprintf(filename, "./%d_%d.bin", packets[i].timestamp.u64, i);
//
//            if ((fptr = fopen(filename,"ab")) != NULL){
//               fwrite(packets[i].buf, packets[i].size, 1, fptr);
//               fclose(fptr);
//            }
#endif // TRACE_PS
            int idx = ps.pkt_idx;
            PJ_PERROR(3,(THIS_FILE, status, "Unpacketize error. ts: %d, idx: %d, rtp_seq: %d, expect len: %d, real len: %d",
                packets[idx].timestamp.u64, idx, packets[idx].rtp_seq, ps.total_video_pes_len, ps.dec_data_len));
        }

        whole_frm.buf = ff->dec_buf;
        whole_frm.size = ps.dec_data_len;
        whole_frm.timestamp = output->timestamp = packets[ps.pkt_idx].timestamp;
        whole_frm.bit_info = 0;

        if(ps_factory.ps_codec_callback != NULL && ps_factory.ps_codec_callback->on_decode_cb != NULL) {
            if(ps.is_i_frame) {
                ps_factory.ps_codec_callback->on_decode_cb(&ps);
                PJ_LOG(3,
                       (THIS_FILE, "Decode send i frame to callback. ts: %d, pkt_cnt: %d, remain len: %d, idx: %d, expect len: %d, real len: %d, beg_seq: %d, end_seq: %d",
                               whole_frm.timestamp.u64, pkt_count, ps.remain_buf_len,
                               ps.pkt_idx, ps.total_video_pes_len, ps.dec_data_len,
                               packets[0].rtp_seq, packets[ps.pkt_idx].rtp_seq));
            }

            return PJ_SUCCESS;
        } else {
            status = ps_codec_decode_whole(codec, &whole_frm, out_size, output);
            if (status != PJ_SUCCESS) {
                PJ_LOG(3,
                       (THIS_FILE, "ps_codec_decode_whole err.ts: %d, pkg_count: %d, buf len is %d, expect len: %d, real len: %d",
                               whole_frm.timestamp.u64, pkt_count, whole_frm.size, ps.total_video_pes_len, ps.dec_data_len));
            } else {
                if (ps.is_i_frame) {
                    PJ_LOG(3,
                           (THIS_FILE, "Decode key frame success. ts: %d, pkt_cnt: %d, remain len: %d, idx: %d, expect len: %d, real len: %d, beg_seq: %d, end_seq: %d",
                                   whole_frm.timestamp.u64, pkt_count, ps.remain_buf_len,
                                   ps.pkt_idx, ps.total_video_pes_len, ps.dec_data_len,
                                   packets[0].rtp_seq, packets[ps.pkt_idx].rtp_seq));
                }
            }
        }
        return status;
    }
}



#ifdef _MSC_VER
#   pragma comment( lib, "avcodec.lib")
#endif

