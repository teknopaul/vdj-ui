
We have `difflock` to keep the ALSA midi sequencer a certain difference from the observed Beats on the Prolink network when we are following the Beats of another deck.

We need a similar feature for when we are the MASTER and real decks are following us.
User should be able to set a millisecond value as_master_difflock to a positive or negative value.
This new offset should affect the offset between ALSA midi clock and the beat packets we send out to other decks.
It seems we may not currenlty be senditg beat packets at all.   libcdj supports the packets and ./references/adj  use a `tick_handler` to send packets.

We have VDJ APIs to send beats.  broadcastBeat();   vdj.c vdj_broadcast_beat()

But we might not have code in `vdj-ui` to trigger these based on ALSA clock yet the way `adj` does.  So that needs doing first and we need to test it.


# UI

The VDJ local deck area in the UI has a "Send Beat" button which does not make sense.
Beats should be sent automatically according to the ALSA sequencer clock.
Replace that button with an integer and spinner for this new offset.

