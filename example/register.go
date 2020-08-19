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

type ServiceCallback struct{}

func NewServiceCallback() *ServiceCallback {
	return &ServiceCallback{}
}

func (sc *ServiceCallback) OnRegStarted(acc *gua.Account, accId gua.AccountId, renew bool) {
	fmt.Println("****************** on reg started**********************")
}

func (sc *ServiceCallback) OnRegState2(acc *gua.Account, accId gua.AccountId, info *gua.RegInfo) {
	fmt.Println("****************** on reg state2**********************")
	fmt.Print(acc.GetInfo())
}

func main() {
	guaCtx := gua.NewGuaContext(NewServiceCallback())
	if err := guaCtx.Create(); err != nil {
		fatal(err)
	}

	epc := gua.NewEndPointConfig()
	epc.LogConfig().SetLevel(4)

	if err := guaCtx.Init(epc); err != nil {
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

	accountConfig := gua.NewAccountConfig()
	defer accountConfig.Free()
	accountConfig.SetIdUri("sip:34020000002060000001@32010100")
	accountConfig.SetRegistrarUri("sip:58.213.90.194:5061")
	accountConfig.SetRegistrarTimeoutSecond(3600)
	cred := gua.NewAuthCredInfo("digest", "*", "test1", 0, "test1")
	accountConfig.AddAuthCred(cred)

	account := guaCtx.NewAccount()
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
