// +build !shared

package gua

/*
#cgo CPPFLAGS: -DPJ_AUTOCONF=1 -O2 -DPJ_IS_BIG_ENDIAN=0
#cgo CPPFLAGS: -DPJ_IS_LITTLE_ENDIAN=1
#cgo CPPFLAGS: -DPJMEDIA_USE_OLD_FFMPEG=1
#cgo CPPFLAGS: -I/usr/local/include -I/usr/include
#cgo CPPFLAGS: -I/usr/include/c++/9 -I/usr/include/x86_64-linux-gnu/c++/9
#cgo LDFLAGS: -L/usr/local/lib -L/usr/lib
#cgo LDFLAGS: -lpjsip-ua-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lpjsip-simple-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lpjsip-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lpjmedia-codec-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lpjmedia-videodev-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lpjmedia-audiodev-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lpjmedia-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lpjnath-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lpjlib-util-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lsrtp-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lresample-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lgsmcodec-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lspeex-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lilbccodec-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lg7221codec-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lyuv-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lwebrtc-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lpj-x86_64-unknown-linux-gnu
#cgo LDFLAGS: -lssl -lcrypto -luuid -lm -lrt -lpthread -lasound
*/
import "C"
