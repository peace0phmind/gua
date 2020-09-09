package gua

/*
#include "include/ps_codecs.h"

*/
import "C"
import (
	"github.com/peace0phmind/gmf"
	"log"
	"syscall"
	"unsafe"
)

var (
	decoder map[int]*gmf.Codec = nil
	encoder *gmf.Codec         = nil
)

func init() {
	log.Println("begin to init decoder: h264, mpeg4")
	decoder = make(map[int]*gmf.Codec)
	codec, err := gmf.FindDecoder(gmf.AV_CODEC_ID_H264)
	if err != nil {
		log.Println("unable to find h264 decode codec")
		return
	}
	decoder[gmf.AV_CODEC_ID_H264] = codec

	codec, err = gmf.FindDecoder(gmf.AV_CODEC_ID_MPEG4)
	if err != nil {
		log.Println("unable to find mpeg4 decode codec")
		return
	}
	decoder[gmf.AV_CODEC_ID_H264] = codec

	codec, err = gmf.FindEncoder(gmf.AV_CODEC_ID_MJPEG)
	if err != nil {
		log.Println("unable to find mjpeg encode codec")
		return
	}
}

//export on_decode_cb
func on_decode_cb(ps *C.ps_codec) {
	log.Printf("recv decode callback. remain len: %d, idx: %d, expect len: %d, real len: %d\n",
		ps.remain_buf_len, ps.pkt_idx, ps.total_video_pes_len, ps.dec_data_len)

	pkt := gmf.NewPacketWith(unsafe.Pointer(ps.dec_buf), int(ps.dec_data_len))

	log.Printf("buf: %v, size: %v, pkt: %v\n", ps.dec_buf, ps.dec_data_len, pkt)

	codec := decoder[gmf.AV_CODEC_ID_H264]

	var cc *gmf.CodecCtx
	if cc = gmf.NewCodecCtx(codec); cc == nil {
		log.Println("unable to create codec context")
	}

	if err := cc.Open(nil); err != nil {
		log.Printf("open codec ctx err: %v\n", err)
	}

	frame, ret := cc.Decode2(pkt)

	if ret < 0 && gmf.AvErrno(ret) == syscall.EAGAIN {
		log.Printf("decode err")
		return
	} else if ret == gmf.AVERROR_EOF {
		log.Printf("EOF in Decode2, handle it\n")
		return
	} else if ret < 0 {
		log.Printf("Unexpected error - %s\n", gmf.AvError(ret))
		return
	}

	log.Printf("%v\n", frame)
}
