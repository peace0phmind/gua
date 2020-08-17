#include "gua.h"

#define THIS_FILE "gua_core.c"

static void init_data(gua_content *guaCtx) {
    pj_bzero(guaCtx, sizeof(gua_content));
}

/* Init random seed */
static void init_random_seed(void) {
    pj_sockaddr addr;
    const pj_str_t *hostname;
    pj_uint32_t pid;
    pj_time_val t;
    unsigned seed=0;

    /* Add hostname */
    hostname = pj_gethostname();
    seed = pj_hash_calc(seed, hostname->ptr, (int)hostname->slen);

    /* Add primary IP address */
    if (pj_gethostip(pj_AF_INET(), &addr)==PJ_SUCCESS)
	seed = pj_hash_calc(seed, &addr.ipv4.sin_addr, 4);

    /* Get timeofday */
    pj_gettimeofday(&t);
    seed = pj_hash_calc(seed, &t, sizeof(t));

    /* Add PID */
    pid = pj_getpid();
    seed = pj_hash_calc(seed, &pid, sizeof(pid));

    /* Init random seed */
    pj_srand(seed);
}

/*
 * Create memory pool.
 */
PJ_DEF(pj_pool_t*) gua_pool_create(gua_content *guaCtx, const char *name, pj_size_t init_size, pj_size_t increment) {
    /* Pool factory is thread safe, no need to lock */
    return pj_pool_create(&guaCtx->cp.factory, name, init_size, increment, NULL);
}

/* Display error */
PJ_DEF(void) gua_perror( const char *sender, const char *title, pj_status_t status) {
    char errmsg[PJ_ERR_MSG_SIZE];

    pj_strerror(status, errmsg, sizeof(errmsg));
    PJ_LOG(1,(sender, "%s: %s [status=%d]", title, errmsg, status));
}

/*
 * Destroy gua content.
 */
PJ_DEF(pj_status_t) gua_destroy(gua_content *guaCtx) {
    /* Done. */
    return PJ_SUCCESS;
}

void gua_set_state(gua_content *guaCtx, gua_state new_state)
{
    const char *state_name[] = {
        "NULL",
        "CREATED",
        "INIT",
        "STARTING",
        "RUNNING",
        "CLOSING"
    };
    gua_state old_state = guaCtx->state;

    guaCtx->state = new_state;
    PJ_LOG(4,(THIS_FILE, "GUA state changed: %s --> %s",
	      state_name[old_state], state_name[new_state]));
}


PJ_DEF(pj_status_t) gua_create(gua_content *guaCtx) {
    pj_status_t status;

    /* Init gua data */
    init_data(guaCtx);

    /* Init PJLIB: */
    status = pj_init();
    PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

    pj_log_push_indent();

    /* Init random seed */
    init_random_seed();

    /* Init PJLIB-UTIL: */
    status = pjlib_util_init();
    if (status != PJ_SUCCESS) {
	    pj_log_pop_indent();
	    gua_perror(THIS_FILE, "Failed in initializing pjlib-util", status);
	    pj_shutdown();
	    return status;
    }

    /* Init PJNATH */
    status = pjnath_init();
    if (status != PJ_SUCCESS) {
	    pj_log_pop_indent();
	    gua_perror(THIS_FILE, "Failed in initializing pjnath", status);
	    pj_shutdown();
	    return status;
    }

    /* Init caching pool. */
    pj_caching_pool_init(&guaCtx->cp, NULL, 0);

    /* Create memory pool for application. */
    guaCtx->pool = gua_pool_create(guaCtx, "gua", 1000, 1000);
    if (guaCtx->pool == NULL) {
	    pj_log_pop_indent();
	    status = PJ_ENOMEM;
	    gua_perror(THIS_FILE, "Unable to create gua pool", status);
	    pj_shutdown();
	    return status;
    }
    
    /* Create mutex */
    status = pj_mutex_create_recursive(guaCtx->pool, "gua", &guaCtx->mutex);
    if (status != PJ_SUCCESS) {
	    pj_log_pop_indent();
	    gua_perror(THIS_FILE, "Unable to create mutex", status);
	    gua_destroy(guaCtx);
	    return status;
    }

    /* Must create SIP endpoint to initialize SIP parser. The parser
     * is needed for example when application needs to call gua_verify_url().
     */
    status = pjsip_endpt_create(&guaCtx->cp.factory, 
				pj_gethostname()->ptr, 
				&guaCtx->endpt);
    if (status != PJ_SUCCESS) {
	    pj_log_pop_indent();
	    gua_perror(THIS_FILE, "Unable to create endpoint", status);
	    gua_destroy(guaCtx);
	    return status;
    }

    /* Init timer entry list */
    pj_list_init(&guaCtx->timer_list);

    /* Create timer mutex */
    status = pj_mutex_create_recursive(guaCtx->pool, "gua_timer", 
				       &guaCtx->timer_mutex);
    if (status != PJ_SUCCESS) {
	    pj_log_pop_indent();
	    gua_perror(THIS_FILE, "Unable to create mutex", status);
	    gua_destroy(guaCtx);
	    return status;
    }

    gua_set_state(guaCtx, GUA_STATE_CREATED);
    pj_log_pop_indent();
    return PJ_SUCCESS;
}

PJ_DEF(void) gua_config_default(gua_config *cfg)
{
    pj_bzero(cfg, sizeof(*cfg));

    cfg->max_calls = ((GUA_MAX_CALLS) < 4) ? (GUA_MAX_CALLS) : 4;
    cfg->thread_cnt = GUA_SEPARATE_WORKER_FOR_TIMER? 2 : 1;
    cfg->nat_type_in_sdp = 1;
    cfg->stun_ignore_failure = PJ_TRUE;
    cfg->force_lr = PJ_TRUE;
    cfg->enable_unsolicited_mwi = PJ_TRUE;
    cfg->use_srtp = GUA_DEFAULT_USE_SRTP;
    cfg->srtp_secure_signaling = GUA_DEFAULT_SRTP_SECURE_SIGNALING;
    cfg->hangup_forked_call = PJ_TRUE;

    cfg->use_timer = GUA_SIP_TIMER_OPTIONAL;
    pjsip_timer_setting_default(&cfg->timer_setting);
    // pjsua_srtp_opt_default(&cfg->srtp_opt);
}

PJ_DEF(void) gua_media_config_default(gua_media_config *cfg)
{
    const pj_sys_info *si = pj_get_sys_info();
    pj_str_t dev_model = {"gua", 3};
    
    pj_bzero(cfg, sizeof(*cfg));

    cfg->clock_rate = GUA_DEFAULT_CLOCK_RATE;
    /* It is reported that there may be some media server resampling problem
     * with iPhone 5 devices running iOS 7, so we set the sound device's
     * clock rate to 44100 to avoid resampling.
     */
    if (pj_stristr(&si->machine, &dev_model) &&
        ((si->os_ver & 0xFF000000) >> 24) >= 7)
    {
        cfg->snd_clock_rate = 44100;
    } else {
        cfg->snd_clock_rate = 0;
    }
    cfg->channel_count = 1;
    cfg->audio_frame_ptime = GUA_DEFAULT_AUDIO_FRAME_PTIME;
    cfg->max_media_ports = GUA_MAX_CONF_PORTS;
    cfg->has_ioqueue = PJ_TRUE;
    cfg->thread_cnt = 1;
    cfg->quality = GUA_DEFAULT_CODEC_QUALITY;
    cfg->ilbc_mode = GUA_DEFAULT_ILBC_MODE;
    cfg->ec_tail_len = GUA_DEFAULT_EC_TAIL_LEN;
    cfg->snd_rec_latency = PJMEDIA_SND_DEFAULT_REC_LATENCY;
    cfg->snd_play_latency = PJMEDIA_SND_DEFAULT_PLAY_LATENCY;
    cfg->jb_init = cfg->jb_min_pre = cfg->jb_max_pre = cfg->jb_max = -1;
    cfg->snd_auto_close_time = 1;

    cfg->ice_max_host_cands = -1;
    cfg->ice_always_update = PJ_TRUE;
    pj_ice_sess_options_default(&cfg->ice_opt);

    cfg->turn_conn_type = PJ_TURN_TP_UDP;
#if PJ_HAS_SSL_SOCK
    pj_turn_sock_tls_cfg_default(&cfg->turn_tls_setting);
#endif
    cfg->vid_preview_enable_native = PJ_TRUE;
}

/*
 * Initialize gua with the specified settings. All the settings are 
 * optional, and the default values will be used when the config is not
 * specified.
 */
PJ_DEF(pj_status_t) pjsua_init( const gua_config *ua_cfg,
				const gua_logging_config *log_cfg,
				const gua_media_config *media_cfg)
{
    gua_config	 default_cfg;
    gua_media_config	 default_media_cfg;
    const pj_str_t	 STR_OPTIONS = { "OPTIONS", 7 };
    pjsip_ua_init_param  ua_init_param;
    unsigned i;
    pj_status_t status;

    pj_log_push_indent();

    /* Create default configurations when the config is not supplied */

    if (ua_cfg == NULL) {
	    gua_config_default(&default_cfg);
	    ua_cfg = &default_cfg;
    }

    if (media_cfg == NULL) {
	    gua_media_config_default(&default_media_cfg);
	    media_cfg = &default_media_cfg;
    }

//     /* Initialize logging first so that info/errors can be captured */
//     if (log_cfg) {
// 	status = pjsua_reconfigure_logging(log_cfg);
// 	if (status != PJ_SUCCESS)
// 	    goto on_error;
//     }

// #if defined(PJ_IPHONE_OS_HAS_MULTITASKING_SUPPORT) && \
//     PJ_IPHONE_OS_HAS_MULTITASKING_SUPPORT != 0
//     if (!(pj_get_sys_info()->flags & PJ_SYS_HAS_IOS_BG)) {
// 	PJ_LOG(5, (THIS_FILE, "Device does not support "
// 			      "background mode"));
// 	pj_activesock_enable_iphone_os_bg(PJ_FALSE);
//     }
// #endif

//     /* If nameserver is configured, create DNS resolver instance and
//      * set it to be used by SIP resolver.
//      */
//     if (ua_cfg->nameserver_count) {
// #if PJSIP_HAS_RESOLVER
// 	unsigned ii;

// 	/* Create DNS resolver */
// 	status = pjsip_endpt_create_resolver(pjsua_var.endpt, 
// 					     &pjsua_var.resolver);
// 	if (status != PJ_SUCCESS) {
// 	    pjsua_perror(THIS_FILE, "Error creating resolver", status);
// 	    goto on_error;
// 	}

// 	/* Configure nameserver for the DNS resolver */
// 	status = pj_dns_resolver_set_ns(pjsua_var.resolver, 
// 					ua_cfg->nameserver_count,
// 					ua_cfg->nameserver, NULL);
// 	if (status != PJ_SUCCESS) {
// 	    pjsua_perror(THIS_FILE, "Error setting nameserver", status);
// 	    goto on_error;
// 	}

// 	/* Set this DNS resolver to be used by the SIP resolver */
// 	status = pjsip_endpt_set_resolver(pjsua_var.endpt, pjsua_var.resolver);
// 	if (status != PJ_SUCCESS) {
// 	    pjsua_perror(THIS_FILE, "Error setting DNS resolver", status);
// 	    goto on_error;
// 	}

// 	/* Print nameservers */
// 	for (ii=0; ii<ua_cfg->nameserver_count; ++ii) {
// 	    PJ_LOG(4,(THIS_FILE, "Nameserver %.*s added",
// 		      (int)ua_cfg->nameserver[ii].slen,
// 		      ua_cfg->nameserver[ii].ptr));
// 	}
// #else
// 	PJ_LOG(2,(THIS_FILE, 
// 		  "DNS resolver is disabled (PJSIP_HAS_RESOLVER==0)"));
// #endif
//     }

//     /* Init SIP UA: */

//     /* Initialize transaction layer: */
//     status = pjsip_tsx_layer_init_module(pjsua_var.endpt);
//     PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);


//     /* Initialize UA layer module: */
//     pj_bzero(&ua_init_param, sizeof(ua_init_param));
//     if (ua_cfg->hangup_forked_call) {
// 	ua_init_param.on_dlg_forked = &on_dlg_forked;
//     }
//     status = pjsip_ua_init_module( pjsua_var.endpt, &ua_init_param);
//     PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);


//     /* Initialize Replaces support. */
//     status = pjsip_replaces_init_module( pjsua_var.endpt );
//     PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

//     /* Initialize 100rel support */
//     status = pjsip_100rel_init_module(pjsua_var.endpt);
//     PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

//     /* Initialize session timer support */
//     status = pjsip_timer_init_module(pjsua_var.endpt);
//     PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

//     /* Initialize and register PJSUA application module. */
//     {
// 	const pjsip_module mod_initializer = 
// 	{
// 	NULL, NULL,		    /* prev, next.			*/
// 	{ "mod-pjsua", 9 },	    /* Name.				*/
// 	-1,			    /* Id				*/
// 	PJSIP_MOD_PRIORITY_APPLICATION,	/* Priority			*/
// 	NULL,			    /* load()				*/
// 	NULL,			    /* start()				*/
// 	NULL,			    /* stop()				*/
// 	NULL,			    /* unload()				*/
// 	&mod_pjsua_on_rx_request,   /* on_rx_request()			*/
// 	&mod_pjsua_on_rx_response,  /* on_rx_response()			*/
// 	NULL,			    /* on_tx_request.			*/
// 	NULL,			    /* on_tx_response()			*/
// 	NULL,			    /* on_tsx_state()			*/
// 	};

// 	pjsua_var.mod = mod_initializer;

// 	status = pjsip_endpt_register_module(pjsua_var.endpt, &pjsua_var.mod);
// 	PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);
//     }

//     /* Parse outbound proxies */
//     for (i=0; i<ua_cfg->outbound_proxy_cnt; ++i) {
// 	pj_str_t tmp;
//     	pj_str_t hname = { "Route", 5};
// 	pjsip_route_hdr *r;

// 	pj_strdup_with_null(pjsua_var.pool, &tmp, &ua_cfg->outbound_proxy[i]);

// 	r = (pjsip_route_hdr*)
// 	    pjsip_parse_hdr(pjsua_var.pool, &hname, tmp.ptr,
// 			    (unsigned)tmp.slen, NULL);
// 	if (r == NULL) {
// 	    pjsua_perror(THIS_FILE, "Invalid outbound proxy URI",
// 			 PJSIP_EINVALIDURI);
// 	    status = PJSIP_EINVALIDURI;
// 	    goto on_error;
// 	}

// 	if (pjsua_var.ua_cfg.force_lr) {
// 	    pjsip_sip_uri *sip_url;
// 	    if (!PJSIP_URI_SCHEME_IS_SIP(r->name_addr.uri) &&
// 		!PJSIP_URI_SCHEME_IS_SIPS(r->name_addr.uri))
// 	    {
// 		status = PJSIP_EINVALIDSCHEME;
// 		goto on_error;
// 	    }
// 	    sip_url = (pjsip_sip_uri*)r->name_addr.uri;
// 	    sip_url->lr_param = 1;
// 	}

// 	pj_list_push_back(&pjsua_var.outbound_proxy, r);
//     }
    

//     /* Initialize PJSUA call subsystem: */
//     status = pjsua_call_subsys_init(ua_cfg);
//     if (status != PJ_SUCCESS)
// 	goto on_error;

//     /* Convert deprecated STUN settings */
//     if (pjsua_var.ua_cfg.stun_srv_cnt==0) {
// 	if (pjsua_var.ua_cfg.stun_domain.slen) {
// 	    pjsua_var.ua_cfg.stun_srv[pjsua_var.ua_cfg.stun_srv_cnt++] = 
// 		pjsua_var.ua_cfg.stun_domain;
// 	}
// 	if (pjsua_var.ua_cfg.stun_host.slen) {
// 	    pjsua_var.ua_cfg.stun_srv[pjsua_var.ua_cfg.stun_srv_cnt++] = 
// 		pjsua_var.ua_cfg.stun_host;
// 	}
//     }

//     /* Start resolving STUN server */
//     status = resolve_stun_server(PJ_FALSE, PJ_FALSE, 0);
//     if (status != PJ_SUCCESS && status != PJ_EPENDING) {
// 	pjsua_perror(THIS_FILE, "Error resolving STUN server", status);
// 	goto on_error;
//     }

//     /* Initialize PJSUA media subsystem */
//     status = pjsua_media_subsys_init(media_cfg);
//     if (status != PJ_SUCCESS)
// 	goto on_error;


//     /* Init core SIMPLE module : */
//     status = pjsip_evsub_init_module(pjsua_var.endpt);
//     PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);


//     /* Init presence module: */
//     status = pjsip_pres_init_module( pjsua_var.endpt, pjsip_evsub_instance());
//     PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

//     /* Initialize MWI support */
//     status = pjsip_mwi_init_module(pjsua_var.endpt, pjsip_evsub_instance());

//     /* Init PUBLISH module */
//     pjsip_publishc_init_module(pjsua_var.endpt);

//     /* Init xfer/REFER module */
//     status = pjsip_xfer_init_module( pjsua_var.endpt );
//     PJ_ASSERT_RETURN(status == PJ_SUCCESS, status);

//     /* Init pjsua presence handler: */
//     status = pjsua_pres_init();
//     if (status != PJ_SUCCESS)
// 	goto on_error;

//     /* Init out-of-dialog MESSAGE request handler. */
//     status = pjsua_im_init();
//     if (status != PJ_SUCCESS)
// 	goto on_error;

//     /* Register OPTIONS handler */
//     pjsip_endpt_register_module(pjsua_var.endpt, &pjsua_options_handler);

//     /* Add OPTIONS in Allow header */
//     pjsip_endpt_add_capability(pjsua_var.endpt, NULL, PJSIP_H_ALLOW,
// 			       NULL, 1, &STR_OPTIONS);

//     /* Start worker thread if needed. */
//     if (pjsua_var.ua_cfg.thread_cnt) {
// 	unsigned ii;

// 	if (pjsua_var.ua_cfg.thread_cnt > PJ_ARRAY_SIZE(pjsua_var.thread))
// 	    pjsua_var.ua_cfg.thread_cnt = PJ_ARRAY_SIZE(pjsua_var.thread);

// #if PJSUA_SEPARATE_WORKER_FOR_TIMER
// 	if (pjsua_var.ua_cfg.thread_cnt < 2)
// 	    pjsua_var.ua_cfg.thread_cnt = 2;
// #endif

// 	for (ii=0; ii<pjsua_var.ua_cfg.thread_cnt; ++ii) {
// 	    char tname[16];
	    
// 	    pj_ansi_snprintf(tname, sizeof(tname), "pjsua_%d", ii);

// #if PJSUA_SEPARATE_WORKER_FOR_TIMER
// 	    if (ii == 0) {
// 		status = pj_thread_create(pjsua_var.pool, tname,
// 					  &worker_thread_timer,
// 					  NULL, 0, 0, &pjsua_var.thread[ii]);
// 	    } else {
// 		status = pj_thread_create(pjsua_var.pool, tname,
// 					  &worker_thread_ioqueue,
// 					  NULL, 0, 0, &pjsua_var.thread[ii]);
// 	    }
// #else
// 	    status = pj_thread_create(pjsua_var.pool, tname, &worker_thread,
// 				      NULL, 0, 0, &pjsua_var.thread[ii]);
// #endif
// 	    if (status != PJ_SUCCESS)
// 		goto on_error;
// 	}
// 	PJ_LOG(4,(THIS_FILE, "%d SIP worker threads created", 
// 		  pjsua_var.ua_cfg.thread_cnt));
//     } else {
// 	PJ_LOG(4,(THIS_FILE, "No SIP worker threads created"));
//     }

//     /* Done! */

//     PJ_LOG(3,(THIS_FILE, "pjsua version %s for %s initialized", 
// 			 pj_get_version(), pj_get_sys_info()->info.ptr));

//     pjsua_set_state(PJSUA_STATE_INIT);
//     pj_log_pop_indent();
//     return PJ_SUCCESS;

on_error:
    pj_log_pop_indent();
    return status;
}