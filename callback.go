package gua

/*
#include "include/pjsua.h"

*/
import "C"

/****************************call back*******************************/
type CallbackOnRegStarted interface {
	OnRegStarted(acc *Account, accId AccountId, renew bool)
}

//export callback_on_reg_started
func callback_on_reg_started(acc_id C.pjsua_acc_id, renew C.pj_bool_t) {
	user_data := C.pjsua_acc_get_user_data(acc_id)
	if user_data != nil {
		acc := (*Account)(user_data)
		if acc.gc != nil {
			if cb, ok := acc.gc.callback.(CallbackOnRegStarted); ok {
				cb.OnRegStarted(acc, AccountId(acc_id), renew > 0)
			}
		}
	}
}

type RegInfo struct {
	info *C.struct_pjsua_reg_info
}

func NewRegInfo(info *C.pjsua_reg_info) *RegInfo {
	return &RegInfo{info: info}
}

type CallbackOnRegState2 interface {
	OnRegState2(acc *Account, accId AccountId, info *RegInfo)
}

//export callback_on_reg_state2
func callback_on_reg_state2(acc_id C.pjsua_acc_id, info *C.pjsua_reg_info) {
	user_data := C.pjsua_acc_get_user_data(acc_id)
	if user_data != nil {
		acc := (*Account)(user_data)
		if acc.gc != nil {
			if cb, ok := acc.gc.callback.(CallbackOnRegState2); ok {
				cb.OnRegState2(acc, AccountId(acc_id), NewRegInfo(info))
			}
		}
	}
}
