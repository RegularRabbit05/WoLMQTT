package api

import (
	"bytes"
	"crypto/ed25519"
	"encoding/hex"
	"encoding/json"
	"fmt"
	mqtt "github.com/eclipse/paho.mqtt.golang"
	"io"
	"net/http"
	"os"
)

type DiscordInteraction struct {
	Type   float64                  `json:"type"`
	Data   DiscordInteractionData   `json:"data"`
	Member DiscordInteractionMember `json:"member"`
}

type DiscordInteractionData struct {
	Name    string                          `json:"name"`
	ID      string                          `json:"id"`
	Type    float64                         `json:"type"`
	Options []DiscordInteractionDataOptions `json:"options"`
}

type DiscordInteractionDataOptions struct {
	Name  string      `json:"name"`
	Type  float64     `json:"type"`
	Value interface{} `json:"value"`
}

type DiscordInteractionMember struct {
	User DiscordInteractionMemberUser `json:"user"`
}

type DiscordInteractionMemberUser struct {
	Username      string `json:"username"`
	Discriminator string `json:"discriminator"`
}

type DiscordResponse struct {
	Type float64             `json:"type"`
	Data DiscordResponseData `json:"data"`
}

type DiscordResponseData struct {
	Content string                 `json:"content"`
	Embeds  []DiscordResponseEmbed `json:"embeds,omitempty"`
}

type DiscordResponseEmbed struct {
	Title       string `json:"title"`
	Description string `json:"description"`
	URL         string `json:"url"`
	Type        string `json:"type"`
}

const ChannelMessageWithSource = float64(4)

func writeResponse(w http.ResponseWriter, commandRes DiscordResponse) {
	data, _ := json.Marshal(commandRes)

	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(http.StatusOK)
	_, _ = w.Write(data)
}

func PowerON(w http.ResponseWriter, r *http.Request) {
	var body []byte
	if r.Body != nil {
		defer func(Body io.ReadCloser) {
			_ = Body.Close()
		}(r.Body)
		body, _ = io.ReadAll(r.Body)
	}

	discordMsg := make(map[string]interface{})

	if err := json.Unmarshal(body, &discordMsg); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}

	if v, ok := discordMsg["type"].(float64); ok && v == 1 {
		verify(w, r, body)
		return
	}

	msg := DiscordInteraction{}

	if err := json.Unmarshal(body, &msg); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	command := msg.Data.Name

	if command != "poweron" {
		http.Error(w, "invalid command", http.StatusBadRequest)
		return
	}

	state := func() bool {
		opts := mqtt.NewClientOptions()
		opts.AddBroker(fmt.Sprintf("tls://%s:%d", os.Getenv("mqtt_host"), 8883))
		opts.SetClientID(os.Getenv("mqtt_client"))
		opts.SetUsername(os.Getenv("mqtt_username"))
		opts.SetPassword(os.Getenv("mqtt_password"))
		client := mqtt.NewClient(opts)
		defer client.Disconnect(250)
		if token := client.Connect(); token.Wait() && token.Error() != nil {
			return false
		}
		token := client.Publish(os.Getenv("mqtt_channel"), 0, false, os.Getenv("mqtt_mac"))
		if token.Error() != nil {
			return false
		}

		return true
	}()

	message := "Sent start command"
	if !state {
		message = "Start failed!"
	}

	commandRes := DiscordResponse{
		Type: ChannelMessageWithSource,
		Data: DiscordResponseData{
			Content: message,
		},
	}

	writeResponse(w, commandRes)
}

func verify(w http.ResponseWriter, r *http.Request, body []byte) {
	publicKey := os.Getenv("discord_public_key")

	signature := r.Header.Get("X-Signature-Ed25519")
	timestamp := r.Header.Get("X-Signature-Timestamp")

	signatureHexDecoded, err := hex.DecodeString(signature)
	if err != nil {
		http.Error(w, err.Error(), http.StatusUnauthorized)
		return
	}

	if len(signatureHexDecoded) != ed25519.SignatureSize {
		http.Error(w, "invalid signature length", http.StatusUnauthorized)
		return
	}

	publicKeyHexDecoded, err := hex.DecodeString(publicKey)
	if err != nil {
		http.Error(w, err.Error(), http.StatusUnauthorized)
		return
	}

	pubKey := [32]byte{}

	copy(pubKey[:], publicKeyHexDecoded)

	var msg bytes.Buffer
	msg.WriteString(timestamp)
	msg.Write(body)

	verified := ed25519.Verify(publicKeyHexDecoded, msg.Bytes(), signatureHexDecoded)

	if !verified {
		http.Error(w, "invalid request signature", http.StatusUnauthorized)
		return
	}

	p := map[string]float64{
		"type": float64(1),
	}

	w.WriteHeader(http.StatusOK)
	if err = json.NewEncoder(w).Encode(p); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
		return
	}
}
