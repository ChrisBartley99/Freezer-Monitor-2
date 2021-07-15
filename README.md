# Freezer-Monitor-2
An ESP32 based Freezer Monitor

This is another version of the Freezer-Monitor code in which I was experimenting with the multi tasking features of the RTOS on the ESP32 
This uses a seperate task to update ThingSpeak, and another to run the buzzer
But I ran into various crashng problems mainly due to overrunning the stack ( probably due to string handling?)  and a couple of other odd errors 
So just here for the record - The original loop based version works well enough

Creates 2 sub tasks to handle ThingSpeak updates and Buzzer sounds

Started as a simple project to learn ESP32 Originally intended just to alert me if the door was left open , but suffered a bit of mission creep as I experimented with ESP32 features

Sends Email if Door is left open too long Now also includes An OLED display to display internal temp of freezer Reports data to a ThingSpeak account - 3rd party widget on phone can monitor ThingSpeak channel and alert if temp out of range Code is updatable via OTA

