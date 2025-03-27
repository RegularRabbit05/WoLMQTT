package main

import (
	"WoLMQTT/api"
	"net/http"
)

func main() {
	http.HandleFunc("/poweron", api.PowerON)
	http.HandleFunc("/init", api.Init)
	_ = http.ListenAndServe(":3000", nil)
}
