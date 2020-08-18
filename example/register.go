package main

import (
	"fmt"
	"log"
	"os"
	"os/signal"
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

	tc := gua.NewTransportConfig()
	tc.SetPort(5060)

	if err := guaCtx.TransportCreate(gua.PJSIP_TRANSPORT_UDP, tc); err != nil {
		fatal(err)
	}

	if err := guaCtx.Start(); err != nil {
		fatal(err)
	}

	for ci := range guaCtx.CodecInfoIterator() {
		fmt.Print(ci.String())
	}

	// accountConfig := pjsua2.NewAccountConfig()
	// cred := gua.NewAuthCredInfo("digest", "*", "test1", 0, "test1")
	// accountConfig.GetSipConfig().GetAuthCreds().Add(cred)

	// sipAccount.Create(accountConfig)

	accountConfig := gua.NewAccountConfig()
	accountConfig.SetIdUri("sip:34020000002060000001@32010100")
	accountConfig.SetRegistrarUri("sip:58.213.90.194:5061")
	accountConfig.SetRegistrarTimeoutSecond(3600)
	cred := gua.NewAuthCredInfo("digest", "*", "test1", 0, "test1")
	accountConfig.AddAuthCred(cred)

	account := gua.NewAccount()
	if err := account.Create(accountConfig, false); err != nil {
		fatal(err)
	}

	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt)

	<-c

	if err := guaCtx.Destroy(); err != nil {
		fatal(err)
	}
}
