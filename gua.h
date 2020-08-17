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
 * This enumeration represents pjsua state.
 */
typedef enum gua_state
{
    /**
     * The library has not been initialized.
     */
    GUA_STATE_NULL,

    /**
     * After gua_create() is called but before pjsua_init() is called.
     */
    GUA_STATE_CREATED,

    /**
     * After gua_init() is called but before pjsua_start() is called.
     */
    GUA_STATE_INIT,

    /**
     * After gua_start() is called but before everything is running.
     */
    GUA_STATE_STARTING,

    /**
     * After gua_start() is called and before pjsua_destroy() is called.
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
    pj_pool_t		*pool;	    /**< pjsua's private pool.		*/
    pj_mutex_t		*mutex;	    /**< Mutex protection for this data	*/
    gua_state		 state;	    /**< Library state.			*/


    /* SIP: */
    pjsip_endpoint	*endpt;	    /**< Global endpoint.		*/

    /* Timer entry list */
    gua_timer_list	 timer_list;
    pj_mutex_t       *timer_mutex;
} gua_content;

PJ_END_DECL

#endif // __GUA_H__