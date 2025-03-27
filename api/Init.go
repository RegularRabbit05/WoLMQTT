package api

import (
	"fmt"
	"log"
	"net/http"
	"os"
	"strings"
)

func Init(w http.ResponseWriter, r *http.Request) {
	if os.Getenv("allow_init") != "true" {
		log.Println("Init refused")
		return
	}

	discordClientID := os.Getenv("discord_client_id")
	if len(discordClientID) == 0 {
		w.WriteHeader(http.StatusInternalServerError)
		return
	}

	registerCommand := fmt.Sprintf("https://discord.com/api/v10/applications/%s/commands",
		discordClientID)

	botToken := os.Getenv("discord_bot_token")

	commandOptions := `{"name": "poweron", "description": "Start your pc", "options": [], "integration_types": [0, 1], "contexts": [0, 1, 2]}`

	req, err := http.NewRequest(http.MethodPost, registerCommand, strings.NewReader(commandOptions))
	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		return
	}

	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("User-Agent", "DiscordBot")
	req.Header.Set("Authorization", "Bot "+botToken)

	res, err := http.DefaultClient.Do(req)
	if err != nil {
		w.WriteHeader(http.StatusInternalServerError)
		return
	}

	log.Printf("Command status: %d", res.StatusCode)

	if res.StatusCode != http.StatusOK && res.StatusCode != http.StatusCreated {
		w.WriteHeader(http.StatusInternalServerError)
		return
	}
}
