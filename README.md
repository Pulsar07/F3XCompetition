<div class="PageDoc">

<div class="header">

<div class="headertitle">

<div class="title">

F3X Competition is a tool to provide training measurements and analysis data for F3X model flight classes. 
At the moment it is limited to the F3B/F3G speed task and F3F task, but could be extended later on.

</div>

</div>

</div>

<div class="contents">

<div class="textblock">

# <span id="intro_sec_en" class="anchor"></span> Overview

In many F3X model flight classes there are distance and speed tasks, expecting the model to pass/cross a A- and B-line of a defined distance.
The passing and crossing is supervised by human helpers and signalled by pressing a button indication the valid pass or line crossing.

These procedures are supported by this F3X Competition tool (currently only the F3B/F3G speed task and F3F task).

The F3X Competition tool consist of two main parts:
* the BaseManager 
  * with a rotation/push button signaling device (Control and A-Line-Signal)
  including almost all logic and time measurement module
  * a display to provide flight data and user interface data
  * 2.4GHz radio link to communicate with the B-LineController
* the B-LineController
  * with a push button signaling device (B-Line-Signal)
  * 2.4GHz radio link to communicate with the BaseManager

![Architektur](https://raw.githubusercontent.com/Pulsar07/F3XCompetition/master/doc/img/F3XTrainerArchitecture.png)

# <span id="features_sec_de" class="anchor"></span> Features
* 2.4GHz radio link using 1MHz channel between A-Line and B-Line
* time measurement with accurancy much better than 10ms
* accustic signal on A-Line
* F3B speed task is supported
  * overall time and detailed time and speed per leg is supported
  * dead distances behind the crossing lines are measured optionally and dead distances are supported
  * Loop mode: final signal of one task is the start signal for the next pilot
* F3F task is supported
  * overall time and detailed time and speed per leg is supported
  * dead distances behind the crossing lines are measured optionally and dead distances are supported
  * Loop mode: final signal of one task is the start signal for the next pilot
* integrated battery and charge HW with low battery warning
* firmware/data (BaseManager) can be updated over the air (OTA)
* BaseManager has a small OLED and rotary input device for on field operation
* BaseManager provides a Web-interface to be used for:
  * changes of settings
  * view, manage and download the saved trainings data
![OLED](https://raw.githubusercontent.com/Pulsar07/F3XCompetition/master/doc/img/OLED_views.png)

# <span id="hardware_sec_en" class="anchor"></span> Hardware
* Wemos D1 (BaseManager)
* Arduino Nano (B-LineController)
* 2.4GHz nRF24L01+ radio module
* BatteryShield
* LiIon Accu
* RotaryEncoder KY-040
* Push button
* OLED 128x64
* LED


</div>

</div>

</div>

