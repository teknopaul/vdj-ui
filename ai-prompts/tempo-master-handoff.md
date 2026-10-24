When a player or mixer is to become tempo master the same process is followed.

If there is currently no tempo master, the device simply becomes master, 
and starts sending status packets with appropriate values of F and Mm (mixer status packets only have F in them).

If another player is currently tempo master, however, a coordinated handoff takes place.
The device that wants to become tempo master first sends a takeover request packet to port 50001 
of the current tempo master, with the player number of the device wanting to become master as the value of D:


The current tempo master will agree to the handoff by sending a packet to port 50001 
of the device that sent the takeover request, with its own device number as the value of D:

Once that is done, the outgoing master will continue to report itself as the master according to its status packets 
(bit 5 of F , and for CDJs, the value of Mm),
but it will announce to the world that the handoff is taking place by sending the device number of the device that is 
about to become tempo master as the value of Mh. (See the mixer and CDJ status packet diagrams for the locations of these bytes.)

As soon as the device becoming tempo master sees its device number in Mh in the status packets from the outgoing tempo master, 
it starts reporting itself as the tempo master using F and, for CDJs, Mm in its own status packets.

And as soon as the outgoing tempo master sees the new master has asserted this role in its status packets,
it stops reporting itself as tempo master in its own status packets, goes back to sending the value ff in Mh, 
and sets its Syncn value to be one greater than the Syncn value reported by any other player on the network 
(although mixers do not report this value at all).
