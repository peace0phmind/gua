package gua

/*
#include "i/pjsua.h"


*/
import "C"

func pj2Str(str *C.struct_pj_str_t) string {
	return C.GoStringN(str.ptr, C.int(str.slen))
}

func str2Pj(str string) C.struct_pj_str_t {
	output_str := C.struct_pj_str_t{}
	output_str.ptr = C.CString(str)
	output_str.slen = C.long(len(str))
	return output_str
}
