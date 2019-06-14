# RingRing

This is a doorbell ring hack to open a door using a secret sequence of long and short presses.

## Motivation

This project starts with some friend of mine, that wanted a way of opening
the public door of the block of flats where he lives. He didn't want to ask to install anything
to the community head. As every flat its button of the doorbell panel wired directly to the
iterior of the flat, and luckily this was an old-school analog device it was pretty easy to
hook up into that to get the job done.

## Operation

The device regular operation when someone pushes the button of a flat, is to send an ~8v AC
signal that makes the buzzer on the flat to sound. Then, whoever is at home, presses a button
that opens the door mechanism.

In a nutshell, the modification consists on:
- capturing the flat button signal (preserving the buzzer sound)
- Decoding a sequence of pulses coming from the buzzer cable
- Trigger the door opening switch with a relay when the sequence is right (preserving the manual open)


## Configuration and remote commands


Manual door open

    http://<device_ip>/door/open

Configure secret key (S = short pulse, L = long pulse)

    http://<device_ip>/door/key/set?value=SLLSL

Configure parameters

    http://<device_ip>/param/set?<param-name>=value

Where the available parameters are timing parameters

    * short: maximum milliseconds for a pulse to be considered a short one
    * long: maximum milliseconds for a pulse to be considered a long one.
    * wait: maximum milliseconds between the end of the release of a pulse and the start of a new one

and notification parameters (accepts 0 or 1 as value)

    * notify_open_via_code: Sends a telegram notification whenever the door is opened with the secret key
    * notify_open_via_code: Sends a telegram notification whenever the door is opened with the web command

## Firmware build

To build the firmare we need to provide some variables that will be injected as defaults in the firmware. It's a simple
way to avoid needing to add some mechanism to do the setup on the device itself via a wifi AP or similar. These are not
paramters that you should need to change often.

    WIFI_SSID=<your-wifi-ssid> \
    WIFI_PASSWORD=<your-wifi-password> \
    TELEGRAM_BOT_TOKEN="<your-telegram-bot-token>" \
    TELEGRAM_CHAT_ID="<your-telegram-chat-or-group-id>" \
    WWW_USERNAME="<username-for-web-commands>" \
    WWW_PASSWORD="<password-for-web-commands>" \
    pio run -t upload --upload-port=/dev/tty.SLAB_USBtoUART



