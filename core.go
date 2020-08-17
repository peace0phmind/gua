package gua

/*
#include "core.h"

*/
import "C"

type GuaContext struct {
	ctx C.struct_gua_content
}

func NewGuaContext() *GuaContext {
	guaContext := GuaContext{}
	return &guaContext
}

func (gc *GuaContext) Create() error {
	C.gua_create(&gc.ctx)
	return nil
}

func (gc *GuaContext) Destroy() error {
	C.gua_destroy(&gc.ctx)
	return nil
}
