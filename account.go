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

type AccountId C.pjsua_acc_id

type Account struct {
	id C.pjsua_acc_id
	gc *GuaContext
}

func (gc *GuaContext) NewAccount() *Account {
	return &Account{gc: gc}
}

func (ac *Account) Create(af *accountConfig, makeDefault bool) error {
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

func (ac *Account) IsValid() bool {
	return C.pjsua_acc_is_valid(ac.id) != 0
}

func (ac *Account) SetDefault() {
	C.pjsua_acc_set_default(ac.id)
}

func (ac *Account) IsDefault() bool {
	return C.pjsua_acc_get_default() == ac.id
}

func (ac *Account) Shutdown() {
	if ac.IsValid() && C.pjsua_get_state() < C.PJSUA_STATE_CLOSING {
		C.pjsua_acc_del(ac.id)
	}
}

type StatusCode C.enum_pjsip_status_code

type AccountInfo struct {
	isDefault       bool
	uri             string
	regIsConfigured bool

	regIsActive   bool
	regExpiresSec int
	regStatus     StatusCode
	regStatusText string
	regLastErr    int

	onlineStatus     bool
	onlineStatusText string
}

func (ai *AccountInfo) IsDefault() bool {
	return ai.isDefault
}

func (ai *AccountInfo) Uri() string {
	return ai.uri
}

func (ai *AccountInfo) RegIsConfigured() bool {
	return ai.regIsConfigured
}

func (ai *AccountInfo) RegIsActive() bool {
	return ai.regIsActive
}

func (ai *AccountInfo) RegExpiresSec() int {
	return ai.regExpiresSec
}

func (ai *AccountInfo) RegStatus() StatusCode {
	return ai.regStatus
}

func (ai *AccountInfo) RegStatusText() string {
	return ai.regStatusText
}

func (ai *AccountInfo) RegLastErr() int {
	return ai.regLastErr
}

func (ai *AccountInfo) OnlineStatus() bool {
	return ai.onlineStatus
}

func (ai *AccountInfo) OnlineStatusText() string {
	return ai.onlineStatusText
}

func (ac *Account) GetInfo() (*AccountInfo, error) {
	pai := &C.struct_pjsua_acc_info{}

	if ret := C.pjsua_acc_get_info(ac.id, pai); ret != C.PJ_SUCCESS {
		return nil, errors.New(fmt.Sprintf("account get info error: %d", ret))
	}

	ai := &AccountInfo{}

	ai.isDefault = pai.is_default != 0
	ai.uri = pj2Str(&pai.acc_uri)
	ai.regIsConfigured = pai.has_registration != 0
	ai.regIsActive = pai.has_registration > 0 && pai.expires > 0 && pai.expires != C.PJSIP_EXPIRES_NOT_SPECIFIED && (pai.status/100 == 2)
	ai.regExpiresSec = int(pai.expires)
	ai.regStatus = StatusCode(pai.status)
	ai.regStatusText = pj2Str(&pai.status_text)
	ai.regLastErr = int(pai.reg_last_err)
	ai.onlineStatus = pai.online_status != 0
	ai.onlineStatusText = pj2Str(&pai.online_status_text)

	return ai, nil
}

type Call struct {
	id C.pjsua_call_id
}

type callSetting struct {
	setting C.pjsua_call_setting
}

func newCallSetting() *callSetting {
	setting := &callSetting{}

	C.pjsua_call_setting_default(&setting.setting)

	return setting
}

func (cs * callSetting) SetAudioCount(count int) {
	cs.setting.aud_cnt = C.uint(count)
}

func (cs *callSetting) SetFlag(flag int) {
	cs.setting.flag = C.uint(flag)
}

func (ac *Account) MakePlay(dstUri string) (*Call, error) {

	pj_dst_uri := str2Pj(dstUri)

	call := &Call{}

	setting := newCallSetting()
	setting.SetAudioCount(0)
	setting.SetFlag(0)

	if ret := C.pjsua_call_make_play(ac.id, &pj_dst_uri, &setting.setting, nil, nil, &call.id); ret != C.PJ_SUCCESS {
		return nil, errors.New(fmt.Sprintf("Make play error: %d", ret))
	}
	return call, nil
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
