The become master process fails in different ways.

I start the GUI, connect, and one real deck is master.

When I click the "Master" button, I see nothing happen except on the decks the master player [1] notification flicks for a second but stays at 1.

When running `adj` cli vesion, if I click `B` for become master it comlains that I am connected to an older firmware version which usually means some data is wrong in the packet they received.

I changed master a few times with real decks, so it was working, but no music playing so there has not been a beat packet.
Then in the VDJ GUI I clicked "Master" and the UI becomes master immediatly, but I dont think it has really negotiated master.
I think it just did not have a current master in memory even though there really was one on the network.
Subsequent messages confuse the players.  And at one point all 3 players crashed.

If I press play on the VDJ, first. So it sends out beat packets before asking for Master it then just fails to become master, no other symptoms but I would guess the other decks ignore a 
packet claiming to be master when it is not.

UI Status says `AutoIP: off MIDI 7 input(s) Network: connected Master: none Decks: 3`  on boot.

AutoIP is running fine, but I did not start it with the UI.
Midi 7 inputs is wierd there are inputs for adj, vdj-ui, vdj-midi-router, vdj-midi-in,  maybe some temp code forgot to cleanup ALSA resources?  I dont think ALSA is related.

After connecting I see...

Network connected 
Master: flickers many times a second between 1, 3 and 4, its really fast.  At the time 3 was master,  1 and 4 were normal decks.

The VDJ was deck `5`. I dont think Pioneer allow 5 active Decks on the Prolink network.

So you added a feature to run ad Deck 2 and its the same issue.

The VDJ-UI always thinks that the Master is changing but the  with every message but the rest of the decks are happand can quickly change master between them.

https://djl-analysis.deepsymmetry.org/djl-analysis/sync.html

tempo-master-handoff.md

