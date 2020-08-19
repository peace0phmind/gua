package gua

/*
#include "include/pjsua.h"


*/
import "C"
import (
	"errors"
	"fmt"
	"unsafe"
)

type accountConfig struct {
	accCfg *C.struct_pjsua_acc_config
}

func NewAccountConfig() *accountConfig {
	accCfg := accountConfig{}

	accCfg.accCfg = (*C.struct_pjsua_acc_config)(C.malloc(C.sizeof_struct_pjsua_acc_config))

	C.pjsua_acc_config_default(accCfg.accCfg)

	return &accCfg
}

func (af *accountConfig) SetIdUri(idUri string) {
	af.accCfg.id = str2Pj(idUri)
}

func (af *accountConfig) SetRegistrarUri(regUri string) {
	af.accCfg.reg_uri = str2Pj(regUri)
}

func (af *accountConfig) SetRegistrarTimeoutSecond(timeoutSec int) {
	af.accCfg.reg_timeout = C.uint(timeoutSec)
}

func (af *accountConfig) AddAuthCred(aci *authCredInfo) {
	count := af.accCfg.cred_count
	dst := &af.accCfg.cred_info[count]

	dst.realm = str2Pj(aci.realm)
	dst.scheme = str2Pj(aci.scheme)
	dst.username = str2Pj(aci.username)
	dst.data_type = C.int(aci.dataType)
	dst.data = str2Pj(aci.data)

	// TODO fix this bug
	// dst.ext.aka.k = str2Pj(aci.akaK)
	// dst.ext.aka.op = str2Pj(aci.akaOp)
	// dst.ext.aka.amf = str2Pj(aci.akaAmf)

	af.accCfg.cred_count = count + 1
}

func (af *accountConfig) Free() {
	if af.accCfg != nil {
		C.free(unsafe.Pointer(af.accCfg))
		af.accCfg = nil
	}
}

type account struct {
	id C.pjsua_acc_id
}

func NewAccount() *account {
	return &account{}
}

func (ac *account) Create(af *accountConfig, makeDefault bool) error {
	iMakeDefault := 0
	if makeDefault {
		iMakeDefault = 1
	}

	af.accCfg.user_data = unsafe.Pointer(ac)

	if ret := C.pjsua_acc_add(af.accCfg, C.int(iMakeDefault), &ac.id); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Create account error: %d", ret))
	}

	return nil
}

func (ac *account) IsValid() bool {
	return C.pjsua_acc_is_valid(ac.id) != 0
}

func (ac *account) SetDefault() {
	C.pjsua_acc_set_default(ac.id)
}

func (ac *account) IsDefault() bool {
	return C.pjsua_acc_get_default() == ac.id
}

func (ac *account) Shutdown() {
	if ac.IsValid() && C.pjsua_get_state() < C.PJSUA_STATE_CLOSING {
		C.pjsua_acc_del(ac.id)
	}
}

type authCredInfo struct {
	realm    string
	scheme   string
	username string
	dataType int
	data     string
	akaK     string
	akaOp    string
	akaAmf   string
}

func NewAuthCredInfo(scheme string, realm string, username string, dataType int, data string) *authCredInfo {
	return &authCredInfo{scheme: scheme, realm: realm, username: username, dataType: dataType, data: data}
}
