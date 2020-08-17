#ifndef __GUA_H__
#define __GUA_H__

#include <pjlib.h>
#include <pjmedia.h>
#include <pjlib-util.h>
#include <pjsip.h>
#include <pjsip_ua.h>

#define PJ_EXPORTING 1
#define PJ_DLL 1


PJ_BEGIN_DECL

/**
 * Maximum simultaneous calls.
 */
#ifndef GUA_MAX_CALLS
#   define GUA_MAX_CALLS	    32
#endif

/**
 * Specify whether timer heap events will be polled by a separate worker
 * thread. If this is set/enabled, a worker thread will be dedicated to
 * poll timer heap events only, and the rest worker thread(s) will poll
 * ioqueue/network events only.
 *
 * Note that if worker thread count setting (i.e: gua_config.thread_cnt)
 * is set to zero, this setting will be ignored.
 *
 * Default: 0 (disabled)
 */
#ifndef GUA_SEPARATE_WORKER_FOR_TIMER
#   define GUA_SEPARATE_WORKER_FOR_TIMER	0
#endif

/**
 * This enumeration represents gua state.
 */
typedef enum gua_state
{
    /**
     * The library has not been initialized.
     */
    GUA_STATE_NULL,

    /**
     * After gua_create() is called but before gua_init() is called.
     */
    GUA_STATE_CREATED,

    /**
     * After gua_init() is called but before gua_start() is called.
     */
    GUA_STATE_INIT,

    /**
     * After gua_start() is called but before everything is running.
     */
    GUA_STATE_STARTING,

    /**
     * After gua_start() is called and before gua_destroy() is called.
     */
    GUA_STATE_RUNNING,

    /**
     * After gua_destroy() is called but before the function returns.
     */
    GUA_STATE_CLOSING

} gua_state;

typedef struct gua_timer_list{
    PJ_DECL_LIST_MEMBER(struct gua_timer_list);
    pj_timer_entry         entry;
    void                  (*cb)(void *user_data);
    void                   *user_data;
} gua_timer_list;

/**
 * Global gua application data.
 */
typedef struct gua_content
{
    /* Control: */
    pj_caching_pool	 cp;	    /**< Global pool factory.		*/
    pj_pool_t		*pool;	    /**< gua's private pool.		*/
    pj_mutex_t		*mutex;	    /**< Mutex protection for this data	*/
    gua_state		 state;	    /**< Library state.			*/


    /* SIP: */
    pjsip_endpoint	*endpt;	    /**< Global endpoint.		*/

    /* Timer entry list */
    gua_timer_list	 timer_list;
    pj_mutex_t       *timer_mutex;
} gua_content;

/**
 * Maximum proxies in account.
 */
#ifndef GUA_ACC_MAX_PROXIES
#   define GUA_ACC_MAX_PROXIES    8
#endif

/**
 * Default value of SRTP mode usage. Valid values are PJMEDIA_SRTP_DISABLED, 
 * PJMEDIA_SRTP_OPTIONAL, and PJMEDIA_SRTP_MANDATORY.
 */
#ifndef GUA_DEFAULT_USE_SRTP
    #define GUA_DEFAULT_USE_SRTP  PJMEDIA_SRTP_DISABLED
#endif

/**
 * Default value of secure signaling requirement for SRTP.
 * Valid values are:
 *	0: SRTP does not require secure signaling
 *	1: SRTP requires secure transport such as TLS
 *	2: SRTP requires secure end-to-end transport (SIPS)
 */
#ifndef GUA_DEFAULT_SRTP_SECURE_SIGNALING
    #define GUA_DEFAULT_SRTP_SECURE_SIGNALING 1
#endif

/**
 * This enumeration specifies the usage of SIP Session Timers extension.
 */
typedef enum gua_sip_timer_use
{
    /**
     * When this flag is specified, Session Timers will not be used in any
     * session, except it is explicitly required in the remote request.
     */
    GUA_SIP_TIMER_INACTIVE,

    /**
     * When this flag is specified, Session Timers will be used in all 
     * sessions whenever remote supports and uses it.
     */
    GUA_SIP_TIMER_OPTIONAL,

    /**
     * When this flag is specified, Session Timers support will be 
     * a requirement for the remote to be able to establish a session.
     */
    GUA_SIP_TIMER_REQUIRED,

    /**
     * When this flag is specified, Session Timers will always be used
     * in all sessions, regardless whether remote supports/uses it or not.
     */
    GUA_SIP_TIMER_ALWAYS

} gua_sip_timer_use;

/**
 * This structure describes the settings to control the API and
 * user agent behavior, and can be specified when calling #gua_init().
 * Before setting the values, application must call #gua_config_default()
 * to initialize this structure with the default values.
 */
typedef struct gua_config
{

    /** 
     * Maximum calls to support (default: 4). The value specified here
     * must be smaller than the compile time maximum settings 
     * GUA_MAX_CALLS, which by default is 32. To increase this 
     * limit, the library must be recompiled with new GUA_MAX_CALLS
     * value.
     */
    unsigned	    max_calls;

    /** 
     * Number of worker threads. Normally application will want to have at
     * least one worker thread, unless when it wants to poll the library
     * periodically, which in this case the worker thread can be set to
     * zero.
     */
    unsigned	    thread_cnt;

    /**
     * Number of nameservers. If no name server is configured, the SIP SRV
     * resolution would be disabled, and domain will be resolved with
     * standard pj_gethostbyname() function.
     */
    unsigned	    nameserver_count;

    /**
     * Array of nameservers to be used by the SIP resolver subsystem.
     * The order of the name server specifies the priority (first name
     * server will be used first, unless it is not reachable).
     */
    pj_str_t	    nameserver[4];

    /**
     * Force loose-route to be used in all route/proxy URIs (outbound_proxy
     * and account's proxy settings). When this setting is enabled, the
     * library will check all the route/proxy URIs specified in the settings
     * and append ";lr" parameter to the URI if the parameter is not present.
     *
     * Default: 1
     */
    pj_bool_t	    force_lr;

    /**
     * Number of outbound proxies in the \a outbound_proxy array.
     */
    unsigned	    outbound_proxy_cnt;

    /** 
     * Specify the URL of outbound proxies to visit for all outgoing requests.
     * The outbound proxies will be used for all accounts, and it will
     * be used to build the route set for outgoing requests. The final
     * route set for outgoing requests will consists of the outbound proxies
     * and the proxy configured in the account.
     */
    pj_str_t	    outbound_proxy[4];

    /**
     * Warning: deprecated, please use \a stun_srv field instead. To maintain
     * backward compatibility, if \a stun_srv_cnt is zero then the value of
     * this field will be copied to \a stun_srv field, if present.
     *
     * Specify domain name to be resolved with DNS SRV resolution to get the
     * address of the STUN server. Alternatively application may specify
     * \a stun_host instead.
     *
     * If DNS SRV resolution failed for this domain, then DNS A resolution
     * will be performed only if \a stun_host is specified.
     */
    pj_str_t	    stun_domain;

    /**
     * Warning: deprecated, please use \a stun_srv field instead. To maintain
     * backward compatibility, if \a stun_srv_cnt is zero then the value of
     * this field will be copied to \a stun_srv field, if present.
     *
     * Specify STUN server to be used, in "HOST[:PORT]" format. If port is
     * not specified, default port 3478 will be used.
     */
    pj_str_t	    stun_host;

    /**
     * Number of STUN server entries in \a stun_srv array.
     */
    unsigned	    stun_srv_cnt;

    /**
     * Array of STUN servers to try. The library will try to resolve and
     * contact each of the STUN server entry until it finds one that is
     * usable. Each entry may be a domain name, host name, IP address, and
     * it may contain an optional port number. For example:
     *	- "pjsip.org" (domain name)
     *	- "sip.pjsip.org" (host name)
     *	- "pjsip.org:33478" (domain name and a non-standard port number)
     *	- "10.0.0.1:3478" (IP address and port number)
     *
     * When nameserver is configured in the \a pjsua_config.nameserver field,
     * if entry is not an IP address, it will be resolved with DNS SRV 
     * resolution first, and it will fallback to use DNS A resolution if this
     * fails. Port number may be specified even if the entry is a domain name,
     * in case the DNS SRV resolution should fallback to a non-standard port.
     *
     * When nameserver is not configured, entries will be resolved with
     * #pj_gethostbyname() if it's not an IP address. Port number may be
     * specified if the server is not listening in standard STUN port.
     */
    pj_str_t	    stun_srv[8];

    /**
     * This specifies if the library should try to do an IPv6 resolution of
     * the STUN servers if the IPv4 resolution fails. It can be useful
     * in an IPv6-only environment, including on NAT64.
     *
     * Default: PJ_FALSE
     */
    pj_bool_t	    stun_try_ipv6;

    /**
     * This specifies if the library should ignore failure with the
     * STUN servers. If this is set to PJ_FALSE, the library will refuse to
     * start if it fails to resolve or contact any of the STUN servers.
     *
     * This setting will also determine what happens if STUN servers are
     * unavailable during runtime (if set to PJ_FALSE, calls will
     * directly fail, otherwise (if PJ_TRUE) call medias will
     * fallback to proceed as though not using STUN servers.
     *
     * Default: PJ_TRUE
     */
    pj_bool_t	    stun_ignore_failure;

    /**
     * This specifies whether STUN requests for resolving socket mapped
     * address should use the new format, i.e: having STUN magic cookie
     * in its transaction ID.
     *
     * Default: PJ_FALSE
     */
    pj_bool_t	    stun_map_use_stun2;

    /**
     * Support for adding and parsing NAT type in the SDP to assist 
     * troubleshooting. The valid values are:
     *	- 0: no information will be added in SDP, and parsing is disabled.
     *	- 1: only the NAT type number is added.
     *	- 2: add both NAT type number and name.
     *
     * Default: 1
     */
    int		    nat_type_in_sdp;

    // /**
    //  * Specify how the support for reliable provisional response (100rel/
    //  * PRACK) should be used by default. Note that this setting can be
    //  * further customized in account configuration (#pjsua_acc_config).
    //  *
    //  * Default: PJSUA_100REL_NOT_USED
    //  */
    // pjsua_100rel_use require_100rel;

    /**
     * Specify the usage of Session Timers for all sessions. See the
     * #pjsua_sip_timer_use for possible values. Note that this setting can be
     * further customized in account configuration (#pjsua_acc_config).
     *
     * Default: PJSUA_SIP_TIMER_OPTIONAL
     */
    gua_sip_timer_use use_timer;

    /**
     * Handle unsolicited NOTIFY requests containing message waiting 
     * indication (MWI) info. Unsolicited MWI is incoming NOTIFY requests 
     * which are not requested by client with SUBSCRIBE request. 
     *
     * If this is enabled, the library will respond 200/OK to the NOTIFY
     * request and forward the request to \a on_mwi_info() callback.
     *
     * See also \a mwi_enabled field #on pjsua_acc_config.
     *
     * Default: PJ_TRUE
     *
     */
    pj_bool_t	    enable_unsolicited_mwi;

    /**
     * Specify Session Timer settings, see #pjsip_timer_setting. 
     * Note that this setting can be further customized in account 
     * configuration (#pjsua_acc_config).
     */
    pjsip_timer_setting timer_setting;

    /** 
     * Number of credentials in the credential array.
     */
    unsigned	    cred_count;

    /** 
     * Array of credentials. These credentials will be used by all accounts,
     * and can be used to authenticate against outbound proxies. If the
     * credential is specific to the account, then application should set
     * the credential in the pjsua_acc_config rather than the credential
     * here.
     */
    pjsip_cred_info cred_info[GUA_ACC_MAX_PROXIES];

    // /**
    //  * Application callback to receive various event notifications from
    //  * the library.
    //  */
    // pjsua_callback  cb;

    /**
     * Optional user agent string (default empty). If it's empty, no
     * User-Agent header will be sent with outgoing requests.
     */
    pj_str_t	    user_agent;

    /**
     * Specify default value of secure media transport usage. 
     * Valid values are PJMEDIA_SRTP_DISABLED, PJMEDIA_SRTP_OPTIONAL, and
     * PJMEDIA_SRTP_MANDATORY.
     *
     * Note that this setting can be further customized in account 
     * configuration (#pjsua_acc_config).
     *
     * Default: #PJSUA_DEFAULT_USE_SRTP
     */
    pjmedia_srtp_use	use_srtp;

    /**
     * Specify whether SRTP requires secure signaling to be used. This option
     * is only used when \a use_srtp option above is non-zero.
     *
     * Valid values are:
     *	0: SRTP does not require secure signaling
     *	1: SRTP requires secure transport such as TLS
     *	2: SRTP requires secure end-to-end transport (SIPS)
     *
     * Note that this setting can be further customized in account 
     * configuration (#pjsua_acc_config).
     *
     * Default: #PJSUA_DEFAULT_SRTP_SECURE_SIGNALING
     */
    int		     srtp_secure_signaling;

    /**
     * This setting has been deprecated and will be ignored.
     */
    pj_bool_t	     srtp_optional_dup_offer;

    // /**
    //  * Specify SRTP transport setting. Application can initialize it with
    //  * default values using pjsua_srtp_opt_default().
    //  */
    // pjsua_srtp_opt   srtp_opt;

    /**
     * Disconnect other call legs when more than one 2xx responses for 
     * outgoing INVITE are received due to forking. Currently the library
     * is not able to handle simultaneous forked media, so disconnecting
     * the other call legs is necessary. 
     *
     * With this setting enabled, the library will handle only one of the
     * connected call leg, and the other connected call legs will be
     * disconnected. 
     *
     * Default: PJ_TRUE (only disable this setting for testing purposes).
     */
    pj_bool_t	     hangup_forked_call;

} gua_config;

/**
 * Max ports in the conference bridge. This setting is the default value
 * for pjsua_media_config.max_media_ports.
 */
#ifndef GUA_MAX_CONF_PORTS
#   define GUA_MAX_CONF_PORTS		254
#endif

/**
 * The default clock rate to be used by the conference bridge. This setting
 * is the default value for pjsua_media_config.clock_rate.
 */
#ifndef GUA_DEFAULT_CLOCK_RATE
#   define GUA_DEFAULT_CLOCK_RATE	16000
#endif

/**
 * Default frame length in the conference bridge. This setting
 * is the default value for pjsua_media_config.audio_frame_ptime.
 */
#ifndef GUA_DEFAULT_AUDIO_FRAME_PTIME
#   define GUA_DEFAULT_AUDIO_FRAME_PTIME  20
#endif


/**
 * Default codec quality settings. This setting is the default value
 * for pjsua_media_config.quality.
 */
#ifndef GUA_DEFAULT_CODEC_QUALITY
#   define GUA_DEFAULT_CODEC_QUALITY	8
#endif

/**
 * Default iLBC mode. This setting is the default value for 
 * pjsua_media_config.ilbc_mode.
 */
#ifndef GUA_DEFAULT_ILBC_MODE
#   define GUA_DEFAULT_ILBC_MODE	30
#endif

/**
 * The default echo canceller tail length. This setting
 * is the default value for pjsua_media_config.ec_tail_len.
 */
#ifndef GUA_DEFAULT_EC_TAIL_LEN
#   define GUA_DEFAULT_EC_TAIL_LEN	200
#endif



/**
 * Logging configuration, which can be (optionally) specified when calling
 * #gua_init(). Application must call #gua_logging_config_default() to
 * initialize this structure with the default values.
 */
typedef struct gua_logging_config
{
    /**
     * Log incoming and outgoing SIP message? Yes!
     */
    pj_bool_t	msg_logging;

    /**
     * Input verbosity level. Value 5 is reasonable.
     */
    unsigned	level;

    /**
     * Verbosity level for console. Value 4 is reasonable.
     */
    unsigned	console_level;

    /**
     * Log decoration.
     */
    unsigned	decor;

    /**
     * Optional log filename.
     */
    pj_str_t	log_filename;

    /**
     * Additional flags to be given to #pj_file_open() when opening
     * the log file. By default, the flag is PJ_O_WRONLY. Application
     * may set PJ_O_APPEND here so that logs are appended to existing
     * file instead of overwriting it.
     *
     * Default is 0.
     */
    unsigned	log_file_flags;

    /**
     * Optional callback function to be called to write log to
     * application specific device. This function will be called for
     * log messages on input verbosity level.
     */
    void       (*cb)(int level, const char *data, int len);


} gua_logging_config;

/**
 * This structure describes media configuration, which will be specified
 * when calling #gua_init(). Application MUST initialize this structure
 * by calling #gua_media_config_default().
 */
typedef struct gua_media_config
{
    /**
     * Clock rate to be applied to the conference bridge.
     * If value is zero, default clock rate will be used 
     * (PJSUA_DEFAULT_CLOCK_RATE, which by default is 16KHz).
     */
    unsigned		clock_rate;

    /**
     * Clock rate to be applied when opening the sound device.
     * If value is zero, conference bridge clock rate will be used.
     */
    unsigned		snd_clock_rate;

    /**
     * Channel count be applied when opening the sound device and
     * conference bridge.
     */
    unsigned		channel_count;

    /**
     * Specify audio frame ptime. The value here will affect the 
     * samples per frame of both the sound device and the conference
     * bridge. Specifying lower ptime will normally reduce the
     * latency.
     *
     * Default value: PJSUA_DEFAULT_AUDIO_FRAME_PTIME
     */
    unsigned		audio_frame_ptime;

    /**
     * Specify maximum number of media ports to be created in the
     * conference bridge. Since all media terminate in the bridge
     * (calls, file player, file recorder, etc), the value must be
     * large enough to support all of them. However, the larger
     * the value, the more computations are performed.
     *
     * Default value: PJSUA_MAX_CONF_PORTS
     */
    unsigned		max_media_ports;

    /**
     * Specify whether the media manager should manage its own
     * ioqueue for the RTP/RTCP sockets. If yes, ioqueue will be created
     * and at least one worker thread will be created too. If no,
     * the RTP/RTCP sockets will share the same ioqueue as SIP sockets,
     * and no worker thread is needed.
     *
     * Normally application would say yes here, unless it wants to
     * run everything from a single thread.
     */
    pj_bool_t		has_ioqueue;

    /**
     * Specify the number of worker threads to handle incoming RTP
     * packets. A value of one is recommended for most applications.
     */
    unsigned		thread_cnt;

    /**
     * Media quality, 0-10, according to this table:
     *   5-10: resampling use large filter,
     *   3-4:  resampling use small filter,
     *   1-2:  resampling use linear.
     * The media quality also sets speex codec quality/complexity to the
     * number.
     *
     * Default: 5 (PJSUA_DEFAULT_CODEC_QUALITY).
     */
    unsigned		quality;

    /**
     * Specify default codec ptime.
     *
     * Default: 0 (codec specific)
     */
    unsigned		ptime;

    /**
     * Disable VAD?
     *
     * Default: 0 (no (meaning VAD is enabled))
     */
    pj_bool_t		no_vad;

    /**
     * iLBC mode (20 or 30).
     *
     * Default: 30 (PJSUA_DEFAULT_ILBC_MODE)
     */
    unsigned		ilbc_mode;

    /**
     * Percentage of RTP packet to drop in TX direction
     * (to simulate packet lost).
     *
     * Default: 0
     */
    unsigned		tx_drop_pct;

    /**
     * Percentage of RTP packet to drop in RX direction
     * (to simulate packet lost).
     *
     * Default: 0
     */
    unsigned		rx_drop_pct;

    /**
     * Echo canceller options (see #pjmedia_echo_create()).
     * Specify PJMEDIA_ECHO_USE_SW_ECHO here if application wishes
     * to use software echo canceller instead of device EC.
     *
     * Default: 0.
     */
    unsigned		ec_options;

    /**
     * Echo canceller tail length, in miliseconds.
     *
     * Default: PJSUA_DEFAULT_EC_TAIL_LEN
     */
    unsigned		ec_tail_len;

    /**
     * Audio capture buffer length, in milliseconds.
     *
     * Default: PJMEDIA_SND_DEFAULT_REC_LATENCY
     */
    unsigned		snd_rec_latency;

    /**
     * Audio playback buffer length, in milliseconds.
     *
     * Default: PJMEDIA_SND_DEFAULT_PLAY_LATENCY
     */
    unsigned		snd_play_latency;

    /** 
     * Jitter buffer initial prefetch delay in msec. The value must be
     * between jb_min_pre and jb_max_pre below. If the value is 0,
     * prefetching will be disabled.
     *
     * Default: -1 (to use default stream settings, currently 0)
     */
    int			jb_init;

    /**
     * Jitter buffer minimum prefetch delay in msec.
     *
     * Default: -1 (to use default stream settings, currently 60 msec)
     */
    int			jb_min_pre;
    
    /**
     * Jitter buffer maximum prefetch delay in msec.
     *
     * Default: -1 (to use default stream settings, currently 240 msec)
     */
    int			jb_max_pre;

    /**
     * Set maximum delay that can be accomodated by the jitter buffer msec.
     *
     * Default: -1 (to use default stream settings, currently 360 msec)
     */
    int			jb_max;

    /**
     * Enable ICE
     */
    pj_bool_t		enable_ice;

    /**
     * Set the maximum number of host candidates.
     *
     * Default: -1 (maximum not set)
     */
    int			ice_max_host_cands;

    /**
     * ICE session options.
     */
    pj_ice_sess_options	ice_opt;

    /**
     * Disable RTCP component.
     *
     * Default: no
     */
    pj_bool_t		ice_no_rtcp;

    /**
     * Send re-INVITE/UPDATE every after ICE connectivity check regardless
     * the default ICE transport address is changed or not. When this is set
     * to PJ_FALSE, re-INVITE/UPDATE will be sent only when the default ICE
     * transport address is changed.
     *
     * Default: yes
     */
    pj_bool_t		ice_always_update;

    /**
     * Enable TURN relay candidate in ICE.
     */
    pj_bool_t		enable_turn;

    /**
     * Specify TURN domain name or host name, in in "DOMAIN:PORT" or 
     * "HOST:PORT" format.
     */
    pj_str_t		turn_server;

    /**
     * Specify the connection type to be used to the TURN server. Valid
     * values are PJ_TURN_TP_UDP, PJ_TURN_TP_TCP or PJ_TURN_TP_TLS.
     *
     * Default: PJ_TURN_TP_UDP
     */
    pj_turn_tp_type	turn_conn_type;

    /**
     * Specify the credential to authenticate with the TURN server.
     */
    pj_stun_auth_cred	turn_auth_cred;

    /**
     * This specifies TLS settings for TLS transport. It is only be used
     * when this TLS is used to connect to the TURN server.
     */
    pj_turn_sock_tls_cfg turn_tls_setting;

    /**
     * Specify idle time of sound device before it is automatically closed,
     * in seconds. Use value -1 to disable the auto-close feature of sound
     * device
     *
     * Default : 1
     */
    int			snd_auto_close_time;

    /**
     * Specify whether built-in/native preview should be used if available.
     * In some systems, video input devices have built-in capability to show
     * preview window of the device. Using this built-in preview is preferable
     * as it consumes less CPU power. If built-in preview is not available,
     * the library will perform software rendering of the input. If this
     * field is set to PJ_FALSE, software preview will always be used.
     *
     * Default: PJ_TRUE
     */
    pj_bool_t vid_preview_enable_native;

    /**
     * Disable smart media update (ticket #1568). The smart media update
     * will check for any changes in the media properties after a successful
     * SDP negotiation and the media will only be reinitialized when any
     * change is found. When it is disabled, media streams will always be
     * reinitialized after a successful SDP negotiation.
     *
     * Note for third party media, the smart media update requires stream info
     * retrieval capability, see #PJSUA_THIRD_PARTY_STREAM_HAS_GET_INFO.
     *
     * Default: PJ_FALSE
     */
    pj_bool_t no_smart_media_update;

    /**
     * Omit RTCP SDES and BYE in outgoing RTCP packet, this setting will be
     * applied for both audio and video streams. Note that, when RTCP SDES
     * and BYE are set to be omitted, RTCP SDES will still be sent once when
     * the stream starts/stops and RTCP BYE will be sent once when the stream
     * stops.
     *
     * Default: PJ_FALSE
     */
    pj_bool_t no_rtcp_sdes_bye;

    /**
     * Optional callback for audio frame preview right before queued to
     * the speaker.
     * Notes:
     * - application MUST NOT block or perform long operation in the callback
     *   as the callback may be executed in sound device thread
     * - when using software echo cancellation, application MUST NOT modify
     *   the audio data from within the callback, otherwise the echo canceller
     *   will not work properly.
     */
    void (*on_aud_prev_play_frame)(pjmedia_frame *frame);

    /**
     * Optional callback for audio frame preview recorded from the microphone
     * before being processed by any media component such as software echo
     * canceller.
     * Notes:
     * - application MUST NOT block or perform long operation in the callback
     *   as the callback may be executed in sound device thread
     * - when using software echo cancellation, application MUST NOT modify
     *   the audio data from within the callback, otherwise the echo canceller
     *   will not work properly.
     */
    void (*on_aud_prev_rec_frame)(pjmedia_frame *frame);
} gua_media_config;

PJ_END_DECL

#endif // __GUA_H__