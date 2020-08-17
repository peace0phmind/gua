package gua

/*
#include "pjsua.h"

*/
import "C"

import (
	"errors"
	"fmt"
)

type GuaContext struct {
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

func (gc *GuaContext) Destroy() error {
	if ret := C.pjsua_destroy(); ret != C.PJ_SUCCESS {
		return errors.New(fmt.Sprintf("Destroy sua error: %d", ret))
	}

	return nil
}
