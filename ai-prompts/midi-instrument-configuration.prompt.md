`adj` had facilities to recognise USB devices and autowire them up.

/etc/adj.conf was a basic system, with hardcoded ID and whitepsae issues.

 I think we can do better.

We should be able to list connected devices and have the user select the device to be used for midi output and midi input.
We should be able to configure devices based on their usb hardware ID to be autoconfigured when they are detected.
We should detect on boot, and have an option to rescan for known devices now.
It should be possibel to configure that specific devices are always used for mid input or output if they are detected.

It should be possible to pre-configure USB devices with flat files somehow because I intend to add support for all the Poioneer decks and distribute that with the .deb package.

For Joystick configuration it should be easy for users with a clone device but a differe, to select a similar module and map it to the usb hw-id of the device they have.


