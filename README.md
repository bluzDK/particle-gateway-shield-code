<p align="center" >
<img src="http://bluz.io/static/img/logo.png" alt="Bluz" title="Bluz">
</p>

Particle Gateway Code
==========
Bluz is a Development Kit (DK) that implements the Wiring language and talks to the [Particle](https://www.particle.io/) cloud through Bluetooth Low Energy. It can be accessed through a REST API and programmed from a Web IDE.

The code in this repository is used for a Particle Core/Photon/Electron to act as a gateway device using the bluz gateway shield. For more information on setting up the gateway shield, please see the [documentation.](http://docs.bluz.io/tutorials/gateway_getting_started/)

##Programming Particle Device

To program your Particle device through the Web IDE, copy the code to a new app and flash it to your Particle board.

To get your Particle device up and running locally:

- Insert a photon into the gateway shield, power it, connect it to the cloud and claim it
- Flash the gateway firmware with

```sh
cd particle-gateway-code/
particle flash <particle device name> particle-gateway-code.cpp
# wait for the upgrades to complete normally
```
