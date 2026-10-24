If we can read pendrives locally and export NFS, maybe we can do it in a way that the physical players can read tracks from us and play them.

We have no spec so we need to try it.  I have an extra pendrive with a rekordbox database plugged in to export.

Players seems to detect what is currently readable on the network and there are two formats to select from on the player, rekordbox computers and other players. 
I suspect it will be easiest to act like another player, since the only data we have is on the pendrive and rekordbox on computers may have more infomation
at its disposal.

It would help debugging if we could log access to the NFS server we are exporting.

If we need to change core libraries let me know before doing it, to agree the design.

We also need to watch the TCP port we have opened because we don't relaly understand that protocol and its likely its used for two way communication.

There is a real software rekordbox instance on the network too on 192.168.1.48 running in a windows server.
