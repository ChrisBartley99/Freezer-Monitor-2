# Freezer-Monitor-2
An ESP32 based Freezer Monitor

Creates 2 sub tasks to handle ThingSpeak updates and Buzzer sounds

Started as a simple project to learn ESP32 Originally intended just to alert me if the door was left open , but suffered a bit of mission creep as I experimented with ESP32 features

Sends Email if Door is left open too long Now also includes An OLED display to display internal temp of freezer Reports data to a ThingSpeak account - 3rd party widget on phone can monitor ThingSpeak channel and alert if temp out of range Code is updatable via OTA

