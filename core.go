package gua

/*
#include "pjsua.h"

pjsip_transport_type_e to_pjsip_transport_type_e(int type) {
	return type;
}

*/
import "C"

import (
	"errors"
	"fmt"
)

const (
	PJSIP_TRANSPORT_UDP = C.PJSIP_TRANSPORT_UDP
	PJSIP_TRANSPORT_TCP = C.PJSIP_TRANSPORT_TCP
	PJSIP_TRANSPORT_TLS = C.PJSIP_TRANSPORT_TLS
)

type GuaContext struct {
	tid C.pjsua_transport_id
}

func NewGuaContext() *GuaContext {
	guaContext := GuaContext{}
	return &guaContext
}

func (gc *GuaContext) Create() error {
	if ret := C.pjsua_create(); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Create sua error: %d", ret))
	}

	return nil
}

func (gc *GuaContext) Init() error {
	if ret := C.pjsua_init(nil, nil, nil); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Init sua error: %d", ret))
	}

	return nil
}

func (gc *GuaContext) TransportCreate(typ int, cfg *TransportConfig) error {
	if cfg == nil {
		cfg = NewTransportConfig()
	}

	if ret := C.pjsua_transport_create(C.to_pjsip_transport_type_e(C.int(typ)), &cfg.tcfg, &gc.tid); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Init sua error: %d", ret))
	}

	return nil
}

func (gc *GuaContext) Destroy() error {
	if ret := C.pjsua_destroy(); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Destroy sua error: %d", ret))
	}

	return nil
}

type TransportConfig struct {
	tcfg C.pjsua_transport_config
}

func NewTransportConfig() *TransportConfig {
	transportConfig := TransportConfig{}

	C.pjsua_transport_config_default(&transportConfig.tcfg)

	return &transportConfig
}
