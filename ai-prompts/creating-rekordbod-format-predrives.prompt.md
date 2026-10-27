# creating rekorkdbopedrives

Do we have enough info now to try making our own rekordbox pendrive folders? 

Input should be a folder of .wav and or ,mp3 files, output should be to am emptry directory a ./PIONEER and ./Contents follder with everything in the real databases. 

We should create the data first in a tmpfs, test exporting it and the write to a real empty directior as a single write (rela rekordbox dow snlot of small writes to pendklrives and damages them I want to avoid that) 
 
Start work on study of what to do in a file falled ./doc/rekordbox-db-output.md

We need our own db format for data as the user creates it e.g. metadata and star, beat grid wave forms
We should keep our local format as flat files so other tools can be used to edit data, e.g. use GIMP to exit waver forms. or grep and sed for mass text updates in folders



