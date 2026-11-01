# CLI Import/Update

## Automated import of one of my own tracks

- Import a new track I have just written
  - Create New, or Add to existing, playlist
- Give the track an image
  - crop the image to rekordbox expected size and format
  - add a watermark image on top in the bottom left corner
- Analise it, create waveforms etc.
- Optionally, Update a USB drive with the new version of the track


Input to this script will be 

- trakcrate Library base
- the path of an .mp3
- playlist name
- an image & watermark
- (optionally) mount point of the USB media

I would like to be able to do this in a bash script, using trakcrate cli, and imagemagik

## Automated import by watching folders

I would also like to automate import by watching a specific folder for wavs or mp3s
If a file changes or is added to the folder script should fetch meta data from the file itself and add it to the trakcrate library.


- Get author, track name and BPM from the wav or mp3 metadata
- Extract the embeded track an image from the file as artwork.
  - crop the image to rekordbox expected size and format
  - (optionally) add a watermark image on top in the bottom left corner
- Analise it, create waveforms etc.
- Add it to one or more Playlists

Input to this script will be 

- trakcrate Library base
- the path of folder to watch
- destination playlist name, (accept multiple args)
- optionally the watermark

