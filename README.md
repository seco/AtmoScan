## AtmoScan


[WATCH VIDEO](https://www.youtube.com/watch?v=iyFuKU8ZcuA)

**AtmoScan** is a multisensor device that measures:

* Temperature
* Humidity
* Pressure
* CO2
* CO
* NO2
* VOC (Air Quality indicator)
* PM 01
* PM25
* PM10
* PM 01
* Radiation

It has an LCD color display, it is gesture controled and it posts to ThingSpeak (or others) via MQTT, but can properly handle disconnected operations. With a rechargeable battery it lasts a full day when disconnected from power.

It uses a a multitasking cooperative framework and is very responsive to user input while sampling sensors, handling UI, posting to MQTT. I believe it squeezes quite a bit out of the tiny ESP8266...

It integrates a number of open source libraries and leverages internet web services. I started buiding the software with a proper framework in mind... but never had the time to properly finish it the way i started, so i quickly completed it with way too many hacks. Therefore code could be way better. If anyone is interested in contributing, it is welcome.

Should anyone be interested, i can publish BoM, schematics etc and describe it. I still have 9 PCB that i am happy to give away at nominal price or less.

### MAIN FEATURES
```
* Color screen
* Gesture control
* Location awareness
* ThingSpeak logging        
* Gracefully handles connected and disconnected operation
* 24h Rechargeable battery
* OTA updates
* Bonus features:
	* Weather Station
	* Plane Spotter

```


### CREDITS


**INCLUDES CODE & LIBRARIES FROM**

```
Adafruit
Arcao
Bblanchon
Bodmer
ClosedCube
Gmag11
Knolleary	
Lucadentella
Seeed
Squix78
Tzapu
Wizard97	
```

**INTEGRATES WEB SERVICES FROM**

```
Adsbexchange.com
GeoNames.org
Google.com
Mylnikov.org
Timezonedb.com
Wunderground.com
```















