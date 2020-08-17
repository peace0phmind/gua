#ifndef __CORE_H__
#define __CORE_H__

#include "gua.h"

PJ_BEGIN_DECL


PJ_DEF(pj_status_t) gua_create(gua_content *guaCtx);

PJ_DEF(pj_status_t) gua_destroy(gua_content *guaCtx);

PJ_END_DECL

#endif // __CORE_H__
