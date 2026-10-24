Pioneer decks are known to talk to each other with direct TCP connections but hte protocol is not reversed yet because it required a network device that can snoop the port.
I have just purchased one.


According to this docuemnt https://djl-analysis.deepsymmetry.org/djl-analysis/missing.html
and this packate analysis  `/home/teknopaul/bzr_workspace/adj/doc/LinkInfo-tcp.pcapng`

Modern XDJs open TCP connections, and the protocol has not been reverse engineered yet.

I beleive from looking at the packets its a simple binary stream of length+value bytes where each value is a message.
I suspect message format may be similar to data sent over UDP.

Clearly TCP is more reliable and messages aare not limeted to UDP max packet size, so it stands to reason
that a more modern protocol might use TCP to send similar information, but support longer track names and more details.

To debug this we can look at the pcap file to get a head start, but further analysis will require starting ADJ,
connecting to the network, and performing player ID negotiation then starting the expected TCP sockets and dumping the binary messages for analysis.

Hopefully we can reverse engineer the messages within the protocol by reading messages without the other decks expecting us to write more than is covered by the pcap dump.

Since the TCP protocol is implemented in the same binary and OS as the UDP protocol we can probably make some guesses about how data is represented,
since its likley that the same structs are used for both protocols, and its likely that String representations use similar encoders to how the database format
represents strings. It is also likely that the TCP protocol sends the same type of content but probably is used for String data that might be longer than fits in
a UDP packet.  Track name data is this class of information, users control the track names not the rekordbox software.
Since it is a streaming protocol we can also imagine that it is not used for much realtime timing information since the TCP stream must be drained before a
timing packet arrives.

Another possible reason for using TCP is to detect liveness, if a program crashes the kernel will close TCP stream so other decks know faster about userland software failures.
Potentially TCP keepalive is used to detect when decks fail, this is particuarly important if the current master fails.
The only known way to become master is to talk to the current master, if its gone there must be a fall back mechanism.
Potentially decks can simply declare themselves master.

Current UDP packets send informatin about future beat grid points, this is limited to 4 beats ahead, TCP would not have that limitation so that also could be a use case for TCP 
connections.

The database file transfer across the network uses an old NFS protocol, and sends the entire DB in one file.  As dbs get bigger since modern pendrives can hold
much more data maybe Pioneer hit the size limits for sending the whole DB, so another potential use case is a better way of transfering partial track information 
from the database wihtout sending the whole db file.

We know that very new XDJs have added features to the UDP protocol so I don't suspect that the TCP is a rewrite of the UDP protocol I suspect it will
only be used for new features that require arbirary length data streams.

references/adj/doc/tcp-protocol-notes.md has some initial debugging notes

The first step in analysis should be to 

- start port mirrorring
- verify capture of at least one side of the TCP
- verify that wireshark can capture pcap files.
- user has to do some track operations on the decks in a known order
- user had to make one deck master
- user had to turn off that deck
- user had to make the other deck master (to see if tcp is involved it may not be)
- then do some initial analysis of the data offline

When we know the track protocol we can write some code to send out TCP data to real decks and see if they survive and we really understand the protocol
this can be done in temporary code in vdj-ui project

When we fully understand the TCP protocol we can document it
then build library code in libcdj

# PC rekordbox analysis

Another likly use of TCP is to talk to computers on the network running rekordbox software.
There is a different protocol for reading tracks of pcs
we can presume this this becuase the UI is different, you can browse rekordbox data on teh decks

- start port mirrorring
- connect rekordbox software and one deck
- user has to do some track operations on the deck in a known order
- then do some initial analysis of the data offline

