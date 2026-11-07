Pendrive "Write to physical device"  popup UI enhancements...

code mainlin in :  src/ui/trakcrate_export_dialog.cpp

this code handles `physical` pendrive exports and export to a folder, which changes the dialog slightly

Order of the info blocks should be (from the top)

1. Destination: <path> this is main export destination, this rows font should be a bit larger the rest of the text.
2. The pendrive logo and the drop down selector to choose a drive.
  - This shold not be visible when not in `physical` mode, we only need the directory chooser to set destination.
  - After selecting a main destination the UI gives you the option to "also write" to the main destination. That does not make sense, the main destination should not also be in the list of additional pendrives.
  - Change the "also write..." text to "Simultaneous export destination(s)"
3. Checkbox to update the drive.
3. The status information pane about what is to be written including warnings, USB rate and estimated sizes.
4. The Tracklist logo and the list of folders to be included (which has the option to remove some if there is not enough space)
5. The Player Settings options.
   - remove the "Player settings" title text.
   - change "Settings to use:"   to   "User Settings: "
   - change the text "Keep the settings files..."  to  "Add missing User Settings" (default to checked)
   - change the text "Update the settings files..." to "Overwrite All User Settings"
6. The "Register the folder as..." option should be removed, there are better ways to do this in the UI already
7. "Advanced / compatibility testing" check box with its expandable pane at the end. The text should be "Advanced Export Options"
  - N.B. "copy from a real drive " option and the path text field is no longer needed now we have User Settings management.

RE status information (bullet point 3.)

When doing a full write to an empty drive the UI text about disk space is correct but not well presented.
When doing "update" it says "Free on XXXX: 27.67 GiB (needs about 27.48 GiB)"  which is misleading for an update that only need 2MB for the database updates, it reads like its going to write _another_ 27.48GiB.

Proposed solution....

We already have a start estimate of the full virtual pendrive size requirement, and the pendrive free space.
We still want the ability to add warning text in red in this pane, and the current warnings are correct when shown.

For the disk space management, we need a table of information, for each of the locations we are going to export to instead of the incorrect text.  

| <drive name> - mount path              |
|--------------|-------------------------|
| Free         | XX.00 GiB              -|
| Current size | YY.00 Gib (xxx tracks)  | estimate of location's current rekordbox utilization  e.g   `du -sh ./PIONEER/ ./Contents/` (this can take a long time to calculate so it should not block the UI thread)
| Required     | ZZ.00 GiB               | estimate the difference between current rekordbox utilization and what we are to about to write, (never show a negative number)
Then there should be some buttons which apply to each specific drive for managing space for the export...

(clean up)  [ ]  (delete)  (empty trash)

button to `clean up`, "Clean up" will look for unreferenced tracks in /Contents/ and delete them. After cleanup the free space should be updated.
check box to run clean up after export. Tool tip should say "Clean up after export"
button to `delete` deletes just the data we understand how to regenerate i.e.  ./PIONEER/USBANLZ/* ./PIONEER/rekordbox/* ./PIONEER/Artwork/* ./Contents/* directories on the selected pendrive only.
- Clicking it should show a confirmation popup with the two absolute paths to ./PIONEER  and ./Contents that will be deleted in the popup's text.
- If current size > 1GiB popup text should include "This may take a while"
- It should not be possible to export while the delete is happening. This pendrives `Clean up` and `Delete buttons` should be disabled, and the main `Export` button should be disabled.
- After deleting has finished, the free space should be updated, and the main Export button, and this pendrives clean up and delete should be re-enabled.
- It should be possible to delete from two pendrives at the same time,  main Export is only enabled when there is no deleting happening.
- If the popup is closed with the `Cancel` button or by clicking Esc key or closing the dial, the deleting of data to any pendrives should immediately stop.
button to `empty trash` should delete the ./.Trash-1000/ directory. This button should only be enabled if the Trash directory exists. Clicking it should show a confirm popup. After cleanup the free space should be updated.

N.B. We don't have any code to "Clean Up" pendrives yet.  This need careful consideration and testing.  In this phase we want the button and checkbox but they should not do anything yet.
