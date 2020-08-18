package gua

/*
#include "pjsua.h"

#define THIS_FILE "core.go"

pjsip_transport_type_e to_pjsip_transport_type_e(int type) {
	return type;
}

void libRegisterThread(char *name)
{
    pj_thread_t *thread;
    pj_thread_desc *desc;
	pj_status_t status;

	if (!name) {
		name = "gua";
	}

    desc = (pj_thread_desc*)malloc(sizeof(pj_thread_desc));
    if (!desc) {
		PJ_LOG(1,(THIS_FILE, "pj_thread_register no mem"));
    }

    pj_bzero(desc, sizeof(pj_thread_desc));

    status = pj_thread_register(name, *desc, &thread);
    if (status == PJ_SUCCESS) {
    	// pj_mutex_lock(threadDescMutex);
		// threadDescMap[thread] = desc;
		// pj_mutex_unlock(threadDescMutex);
		free(desc);
    } else {
		free(desc);
		PJ_LOG(1,(THIS_FILE, "pj_thread_register error : %d\n", status));
    }
}

*/
import "C"

import (
	"errors"
	"fmt"
	"sync"
	"unsafe"
)

const (
	PJSIP_TRANSPORT_UDP = C.PJSIP_TRANSPORT_UDP
	PJSIP_TRANSPORT_TCP = C.PJSIP_TRANSPORT_TCP
	PJSIP_TRANSPORT_TLS = C.PJSIP_TRANSPORT_TLS
)

var (
	threadMutex sync.Mutex
)

type GuaContext struct {
	tid C.pjsua_transport_id
}

type codecInfo C.struct_pjsua_codec_info

func NewGuaContext() *GuaContext {
	return &GuaContext{}
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

func (gc *GuaContext) TransportCreate(typ int, cfg *transportConfig) error {
	if cfg == nil {
		cfg = NewTransportConfig()
	}

	if ret := C.pjsua_transport_create(C.to_pjsip_transport_type_e(C.int(typ)), &cfg.tcfg, &gc.tid); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Init sua error: %d", ret))
	}

	return nil
}

func (gc *GuaContext) Start() error {
	if ret := C.pjsua_start(); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Start sua error: %d", ret))
	}

	return nil
}

func (gc *GuaContext) Destroy() error {
	gc.checkThread()

	if ret := C.pjsua_destroy(); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Destroy sua error: %d", ret))
	}

	return nil
}

func (gc *GuaContext) checkThread() {
	threadMutex.Lock()
	defer threadMutex.Unlock()

	if C.pj_thread_is_registered() == 0 {
		C.libRegisterThread(nil)
	}
}

func (gc *GuaContext) CodecInfoIterator() <-chan codecInfo {
	iterator := make(chan codecInfo, 30)

	go func() {
		gc.checkThread()

		defer close(iterator)

		count := 128
		pj_codec := make([]codecInfo, count)

		if ret := C.pjsua_enum_codecs((*C.struct_pjsua_codec_info)(&pj_codec[0]), (*C.uint)(unsafe.Pointer(&count))); ret != C.PJ_SUCCESS {
			fmt.Printf("enum codecs error: %d", ret)
			return
		}

		fmt.Println(fmt.Sprintf("Gua has %d codecs.", count))

		for i := 0; i < count; i++ {
			iterator <- pj_codec[i]
		}

	}()

	return iterator
}

func (ci *codecInfo) String() string {
	return fmt.Sprintf("Codec - %s (priority: %d)\n", pj2Str(&ci.codec_id), ci.priority)
}

type transportConfig struct {
	tcfg C.pjsua_transport_config
}

func NewTransportConfig() *transportConfig {
	tc := transportConfig{}

	C.pjsua_transport_config_default(&tc.tcfg)

	return &tc
}

func (tc *transportConfig) SetPort(port int) error {
	if port < 0 || port > 65535 {
		return errors.New("Port must at 0~65535")
	}

	tc.tcfg.port = C.uint(port)
	return nil
}
