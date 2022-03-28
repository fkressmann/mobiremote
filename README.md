# MobiRemote

## What

This small tool was built to remote control and monitor a Mobicool FR 40 / MCF 40 (34/60
respectively) fridge. It's somehow inspired
by [this project on GitHub](https://github.com/UnifiedEngineering/mobicool-fr34) which completly
replaces the firmware of the controller. I didn't want to go that far to write my own firmware, I
just wanted the possibility of remote monitoring / control.

## Why

This project is part of my bigger SmartBoat project. I wanted to be able to integrate the fridge and
control the target temperature and on/off state (not just using a relay to turn off power). There
were two main reasons why I wanted to programmatically change the temp:

- According to the outside temperature (nobody needs 2°C beer at 30° outside temp, brain freeze
  coming...)
- The available electrical energy. Cool down during the day when there is a lot of PV energy. Save
  energy and noise at night. Rise temperature / turn off fridge when battery is low.

## How

As the internal logic of the fridge already runs on 3v3, I could use this for the ESP, too.

### Networking

MQTT is used for communication.

#### MQTT out

- ```{prefix}/ip``` IP address of the chip (retained)
- ```{prefix}/rssi``` Signal strength of the WiFi (retained)
- ```{prefix}/log``` Log (retained)
- ```{prefix}/temp``` Current temperature every 1-2sek

#### MQTT in

All settings require a number as payload

- ```{prefix}/cmnd/inittemp``` Set current target temperature (only for initialization)
- ```{prefix}/cmnd/initpwr``` Set current power state (only for initialization)
- ```{prefix}/cmnd/set``` Set target temperature
- ```{prefix}/cmnd/pwr``` Set power state (turn on/off fridge)

### Pressing buttons

I took the minimum invasive way and automated the four buttons of the fridges control panel using an
optocoupler for each button to simulate button presses. After playing a little with the timing, this
works very good by now. As the software is not getting any feedback of the controller, it needs to
remember the current target temperature. This is why it gets written to the flash after each
change (and loaded on boot, same for the power state). It's not planned to change the temperature
manually from now on.

In the beginning I thought about hijacking the SPI like communication between display panel and main
controller but after hooking up a logic analyser, this seemed too complex. Optocouplers are the
easiest way because the display chip (TM1620B) does some crazy multiplexing on the button inputs.
Didn't want so spend too much time reverse engineering that.

### Measuring temp

Furthermore, I realized that the fridge uses a 10k NTC for temperature measurement and connected
this one to the ESP, too to measure the inside temperature (previously, I had a DS18B20 inside and a
ribbon cable to through the lid).

Normally, you would use a Steinhart and Hart equation to calculate the temp from the resistance of
the NTC, but this somehow did not work out. So I decided to hook up two DS18B20 sensors, cool the
fridge down to -10°C and let it warm up slowly overnight while logging the mean temp of both
DS18B20 and the ESPs avg voltage reading of 1k samples (beware, read ADC only every 10ms or so,
otherwise WiFi crashes...). After plotting this, the curve was surprisingly linear. So used a linear
approximation which had a error of <0.2°C between 0 and 10°C. Precise enough I'd say :D

I did not disconnect the ADC from the main controller because it is used for controlling the
compressor. Maybe this is the reason why the measured voltage was so far off from what one would
expect from a 10k NTC. Nobody knows exactly what kind of voltage divider Mobicool uses on the main
board.

## ToDo

- [x] Calibrate temp measurements
- [x] Send temp only when changed
- [ ] Use WiFi manager instead of fixed WiFi credentails
- [ ] Use WiFi manager to set mqtt settings
- [ ] Add soem pics here