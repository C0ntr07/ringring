#!/bin/bash
#
# This script will output all needed compile flags taking default
# values when no value is provided as an env variable to the script
# Note that in a first setup, all variables are required except:
#
#   - WWW_USERNAME

FLAG_WIFI_SSID=${WIFI_SSID:-""}
FLAG_WIFI_PASSWORD=${WIFI_PASSWORD:-""}
FLAG_TELEGRAM_BOT_TOKEN=${TELEGRAM_BOT_TOKEN:-""}
FLAG_TELEGRAM_CHAT_ID=${TELEGRAM_CHAT_ID:-""}
FLAG_WWW_USERNAME=${WWW_USERNAME:-"admin"}
FLAG_WWW_PASSWORD=${WWW_PASSWORD:-""}

echo $(echo "
-DWIFI_SSID=\\\"$FLAG_WIFI_SSID\\\"
-DWIFI_PASSWORD=\\\"$FLAG_WIFI_PASSWORD\\\"
-DTELEGRAM_BOT_TOKEN=\\\"$FLAG_TELEGRAM_BOT_TOKEN\\\"
-DTELEGRAM_CHAT_ID=\\\"$FLAG_TELEGRAM_CHAT_ID\\\"
-DWWW_USERNAME=\\\"$FLAG_WWW_USERNAME\\\"
-DWWW_PASSWORD=\\\"$FLAG_WWW_PASSWORD\\\"
")
