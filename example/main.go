package main

import (
	"log"
	"runtime/debug"

	"github.com/peace0phmind/gua"
)

func fatal(err error) {
	debug.PrintStack()
	log.Fatal(err)
}

func main() {
	guaCtx := gua.NewGuaContext()

	if err := guaCtx.Create(); err != nil {
		fatal(err)
	}

	if err := guaCtx.Init(); err != nil {
		fatal(err)
	}

	if err := guaCtx.TransportCreate(gua.PJSIP_TRANSPORT_UDP, nil); err != nil {
		fatal(err)
	}

	if err := guaCtx.Destroy(); err != nil {
		fatal(err)
	}
}
