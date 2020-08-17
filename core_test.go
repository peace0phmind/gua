package gua

import "testing"

func TestGuaCreate(t *testing.T) {
	guaCtx := NewGuaContext()

	if err := guaCtx.Create(); err != nil {
		t.Fatal(err)
	}

	if err := guaCtx.Destroy(); err != nil {
		t.Fatal(err)
	}
}
