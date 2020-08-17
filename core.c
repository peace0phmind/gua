#include "gua.h"

#define THIS_FILE "core.c"

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


PJ_DECL(pj_status_t) gua_create(gua_content *guaCtx) {
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
