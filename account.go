package gua

/*
#include "pjsua.h"


*/
import "C"

type accountConfig struct {
	accCfg C.pjsua_acc_config
}

func NewAccountConfig() *accountConfig {
	accCfg := accountConfig{}

	C.pjsua_acc_config_default(&accCfg.accCfg)

	return &accCfg
}

func (af *accountConfig) SetIdUri(idUri string) {
	af.accCfg.id = str2Pj(idUri)
}

func (af *accountConfig) SetRegistrarUri(regUri string) {
	af.accCfg.reg_uri = str2Pj(regUri)
}

type account struct {
	id C.pjsua_acc_id
}

func NewAccount() *account {
	return &account{}
}

func (ac *account) Create() error {
	return nil
}
