package gua

/*
#include "include/pjsua.h"
#include "include/ps_codecs.h"
#include "include/pjsua_internal.h"


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

extern void callback_on_reg_started(pjsua_acc_id acc_id, pj_bool_t renew);
void set_on_reg_started(pjsua_config *c) {
	c->cb.on_reg_started = callback_on_reg_started;
}

extern void callback_on_reg_state2(pjsua_acc_id acc_id, pjsua_reg_info *info);
void set_on_reg_state2(pjsua_config *c) {
	c->cb.on_reg_state2 = callback_on_reg_state2;
}

extern void on_decode_cb(ps_codec *psCodec);
void set_on_decode_cb(pjmedia_ps_codec_callback *cb) {
	cb->on_decode_cb = on_decode_cb;
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

/****************************config*******************************/
type config struct {
	c *C.struct_pjsua_config
}

func newConfig() *config {
	c := &config{}

	c.c = (*C.struct_pjsua_config)(C.malloc(C.sizeof_struct_pjsua_config))
	C.pjsua_config_default(c.c)

	return c
}

func (c *config) free() {
	if c.c != nil {
		C.free(unsafe.Pointer(c.c))
		c.c = nil
	}
}

/****************************log config*******************************/
type logConfig struct {
	lc *C.struct_pjsua_logging_config
}

func newLogConfig() *logConfig {
	lc := &logConfig{}

	lc.lc = (*C.struct_pjsua_logging_config)(C.malloc(C.sizeof_struct_pjsua_logging_config))
	C.pjsua_logging_config_default(lc.lc)

	return lc
}

func (lc *logConfig) SetLevel(level int) {
	lc.lc.level = C.uint(level)
}

func (lc *logConfig) SetConsoleLevel(level int) {
	lc.lc.console_level = C.uint(level)
}

func (lc *logConfig) free() {
	if lc.lc != nil {
		C.free(unsafe.Pointer(lc.lc))
		lc.lc = nil
	}
}

/****************************media config*******************************/
type mediaConfig struct {
	mc *C.struct_pjsua_media_config
}

func newMediaConfig() *mediaConfig {
	mc := &mediaConfig{}

	mc.mc = (*C.struct_pjsua_media_config)(C.malloc(C.sizeof_struct_pjsua_media_config))
	C.pjsua_media_config_default(mc.mc)

	return mc
}

func (mc *mediaConfig) free() {
	if mc.mc != nil {
		C.free(unsafe.Pointer(mc.mc))
		mc.mc = nil
	}
}

/****************************end point config*******************************/
type endPointConfig struct {
	pc  *config
	plc *logConfig
	pmc *mediaConfig

	callback interface{}
}

func NewEndPointConfig() *endPointConfig {
	epc := &endPointConfig{}
	epc.pc = newConfig()
	epc.plc = newLogConfig()
	epc.pmc = newMediaConfig()
	return epc
}

func (epc *endPointConfig) Config() *config {
	return epc.pc
}

func (epc *endPointConfig) LogConfig() *logConfig {
	return epc.plc
}

func (epc *endPointConfig) MediaConfig() *mediaConfig {
	return epc.pmc
}

func (epc *endPointConfig) Free() {
	epc.pc.free()
	epc.plc.free()
	epc.pmc.free()
}

/****************************GuaContext*******************************/

type GuaContext struct {
	tid      C.pjsua_transport_id
	callback interface{}
}

type codecInfo C.struct_pjsua_codec_info

func NewGuaContext(callback interface{}) *GuaContext {
	return &GuaContext{callback: callback}
}

func (gc *GuaContext) Create() error {
	gc.checkThread()
	if ret := C.pjsua_create(); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Create sua error: %d", ret))
	}

	return nil
}

func (gc *GuaContext) SetNullSndDev() error {
	gc.checkThread()
	if ret := C.pjsua_set_null_snd_dev(); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("SetNullSndDev error: %d", ret))
	}

	return nil
}

func (gc *GuaContext) initCallback(c *config) {
	if gc.callback == nil {
		return
	}

	cb := gc.callback

	if _, ok := cb.(CallbackOnRegStarted); ok {
		C.set_on_reg_started(c.c)
	}

	if _, ok := cb.(CallbackOnRegState2); ok {
		C.set_on_reg_state2(c.c)
	}
}

func (gc *GuaContext) Init(epc *endPointConfig) error {
	if epc == nil {
		epc = NewEndPointConfig()
	}

	gc.checkThread()
	gc.initCallback(epc.Config())

	if ret := C.pjsua_init(epc.Config().c, epc.LogConfig().lc, epc.MediaConfig().mc); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Init sua error: %d", ret))
	}

	if ret := C.pjmedia_codec_ps_vid_init(nil, &C.pjsua_var.cp.factory); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Error initializing ffmpeg library: %d", ret))
	}

	psCodecCb := (*C.pjmedia_ps_codec_callback)(C.malloc(C.sizeof_struct_pjmedia_ps_codec_callback))
	C.set_on_decode_cb(psCodecCb)
	if ret := C.pjmedia_codec_ps_vid_init_cb(psCodecCb); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Error initializing ps codec callback: %d", ret))
	}

	return nil
}

func (gc *GuaContext) LogSetLevel(level int) {
	C.pj_log_set_level(C.int(level))
}

func (gc *GuaContext) LogGetLevel() int {
	return int(C.pj_log_get_level())
}

func (gc *GuaContext) TransportCreate(typ int, cfg *transportConfig) error {
	if cfg == nil {
		cfg = NewTransportConfig()
	}

	gc.checkThread()
	if ret := C.pjsua_transport_create(C.to_pjsip_transport_type_e(C.int(typ)), &cfg.tcfg, &gc.tid); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Init sua error: %d", ret))
	}

	return nil
}

func (gc *GuaContext) Start() error {
	gc.checkThread()
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
