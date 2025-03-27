# WoL Discord bot
Allows the user to send a mqtt wake-up message from discord that will create a WoL packet for your desired computer
- discord_bot_token: token from the bot page
- discord_client_id: app id
- discord_public_key: public key from the app page (remember to enable both user and guild installs!)
- mqtt_host: hostname of the broker
- mqtt_client: name of this client
- mqtt_username: username
- mqtt_password: password
- mqtt_channel: topic for the message
- mqtt_mac: mac address for the WoL

set env "allow_init" to "true", call https://link/api/Init to register the commands and then delete "allow_init"