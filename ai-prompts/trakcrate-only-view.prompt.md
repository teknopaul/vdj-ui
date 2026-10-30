# trakcrate only view

For users that don't have midi hardware to sync, or don't have decks connected many of the VDJ-UI features are not needed.

Can we have a way to start vdj-ui with only trakcrate features, we should use the same code base maybe detect which by which symlink is used to start the application.
Ideally we should have different .desktop files if possible to ensure the icon in the taskbar is different use img/trakcrate-32x32.png
This may mean the main window name needs to be different on loading is this even possible?

Goals

- Different task bar icon
- Window title "📦 trakcrate"
- Full window with just the trakcrate view.
- Completely different menus (these will change over time so we need to be able to specify in the code which are for trakcrate view only)
  - no Connect button
  - File menu only has Clone and Quit
  - no Network menu
  - Config only has Quantize and Quit
- trakcrate side bar treeview twice as wide on boot

