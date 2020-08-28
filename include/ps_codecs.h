#ifndef __PS_CODECS_H__
#define __PS_CODECS_H__


#include <pjmedia-codec/types.h>
#include <pjmedia/vid_codec.h>

PJ_BEGIN_DECL

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

