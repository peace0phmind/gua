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

type DecodedDataConsumer interface {
	OnConsumer(string, []byte)
}

var (
	decoder  map[int]*gmf.Codec  = nil
	encoder  *gmf.Codec          = nil
	consumer DecodedDataConsumer = nil
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
	decoder[gmf.AV_CODEC_ID_MPEG4] = codec

	codec, err = gmf.FindEncoder("mjpeg")
	if err != nil {
		log.Println("unable to find mjpeg encode codec")
		return
	}

	encoder = codec
}

func InitConsumer(ddc DecodedDataConsumer) {
	consumer = ddc
}

//export on_decode_cb
func on_decode_cb(ps *C.ps_codec) {
	if len(ps.callee_id) == 0 {
		log.Println("callee id is nil, ignore this data...")
	}

	calleeId := C.GoString(&ps.callee_id[0])

	log.Printf("recv decode from callee[%s]. remain len: %d, idx: %d, expect len: %d, real len: %d\n", calleeId,
		ps.remain_buf_len, ps.pkt_idx, ps.total_video_pes_len, ps.dec_data_len)

	pkt := gmf.NewPacketWith(unsafe.Pointer(ps.dec_buf), int(ps.dec_data_len))

	log.Printf("buf: %v, size: %v, pkt: %v\n", ps.dec_buf, ps.dec_data_len, pkt)

	codec := decoder[int(ps.video_codec_id)]

	if codec == nil {
		log.Printf("unable to find decoder from ps video codec: %v\n", ps.video_codec_id)
		return
	}

	var cc *gmf.CodecCtx
	if cc = gmf.NewCodecCtx(codec); cc == nil {
		log.Println("unable to create codec context")
		return
	}

	defer gmf.Release(cc)

	if err := cc.Open(nil); err != nil {
		log.Printf("open codec ctx err: %v\n", err)
		return
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

	occ := gmf.NewCodecCtx(encoder)
	defer gmf.Release(occ)

	occ.SetPixFmt(gmf.AV_PIX_FMT_YUVJ420P).SetWidth(cc.Width()).SetHeight(cc.Height())
	occ.SetTimeBase(gmf.AVR{Num: 1, Den: 1})

	if err := occ.Open(nil); err != nil {
		log.Printf("occ open error: %v\n", err)
		return
	}

	outputCtx, err := gmf.NewOutputCtx("test.jpeg")
	if err != nil {
		log.Printf("output ctx error: %v\n", err)
		return
	}
	defer outputCtx.Free()
	outputCtx.Dump()

	//fg, err := gmf.NewSimpleVideoGraph("crop=w=800:h=600:x=0:y=0", cc, occ)
	//if err != nil {
	//	log.Printf("SimpleVideoGraph error: %v\n", err)
	//	return
	//}
	//
	//var (
	//	ff    []*gmf.Frame
	//)
	//
	//if frame != nil {
	//	if err := fg.AddFrame(frame, 0, 1); err != nil {
	//		log.Fatalf("%s\n", err)
	//	}
	//	fg.Dump()
	//	frame.Free()
	//}
	//
	//if err := fg.AddFrame(frame, 0, 4); err != nil {
	//	log.Fatalf("%s\n", err)
	//}
	//frame.Free()
	//
	//if ff, err = fg.GetFrame(); err != nil && len(ff) == 0 {
	//	log.Printf("GetFrame() returned '%s', continue\n", err)
	//	return
	//}
	//
	//if len(ff) == 0 {
	//	log.Println("GetFrame() length zero")
	//	return
	//}

	// TODO remove when fix fiter graph issue, use ff
	result := []*gmf.Frame{frame}
	packets, err := occ.Encode(result, -1)
	if err != nil {
		log.Fatal(err)
	}

	for _, f := range result {
		f.Free()
	}

	for _, op := range packets {
		if consumer != nil {
			consumer.OnConsumer(calleeId, op.Data())
		}
		op.Free()
	}
}
