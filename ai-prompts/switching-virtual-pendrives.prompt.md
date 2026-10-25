Now that we can export a pendrive (or any folder with a rekordbox db) when we connect, we should be able to support switching the pendrives at runtime.

In real decks, provided a pendrive is not activly being used, you can touch the USP STOP button and unmount the pendrive and insert a different physical pendrive.

We need a similar feature in vdj-ui.

We need to start with capturing a real deck pendrive extracts and see what messages are sent.

1) a USB extract that does not work because it is playing on one deck.
2) a USB extract that the does work becuase it is not.

Raeal decks show the pendrive name in teh UI we needto see how that is done so we can mock it for virtual direves

We need to implelent those messages.

We then need a UI feature to setup virtual pendrives on the local PC to select from.

The Use should be able to add and remove folders from the list of virtual pendrives, and give them names.
The UI should also iterate pysical pendrives in serted in the pc, PCs can support may pendrives simultaneously mounted in /media/<username>
It should be possibel to load the drive in our existing drive viwer pane.

The we need a selector option to choose a virtual(or physical pendrive to export)
We should support dynamically exporitng a folder too with a default name, so setting up drives in advance is not a requirement.
We should keep the exiting menuitems to load from the File menu




