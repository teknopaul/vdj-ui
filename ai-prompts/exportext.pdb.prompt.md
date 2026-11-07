I seems exportExt.pdb is the "My Tags" database.

"A tag or category that can be assigned to tracks for the purpose of categorization and finding similar music."

This enables users to tag their music with strings other than the mp3 standard genres.

/home/teknopaul/github_workspace/rekordcrate/src/ext.rs has some example code.

exportExt.pdb is a file of named links to the export.pdb.

When we save User Settings from pendrives, we need to clean up the exportExt.pdb before its ok to use on other pendrives.

It is probably best to just "not break", i.e understand the format and copy empty files that do not cause issues for the real decks.

# To implmement the Tags feature functionally

We should have one set of our own Tags for the whole trakcrate DB.
UI should have a new Tree entry "Tags", with CRUD for the categories and tags.
We should save the category and tag names data in trackrate as a flat file in the root of the trakcrate repo.
We should then have the option to export a exportExt.pdb file with no links, if desired.

A track's tags can be stores in a tracks `meta.txt`, TODO find wheich characters ar available for delimiters.

```
tags: [Category1]Tag1,Tag2[Category3]Tag5,Tag9
```

On Virtual Pendrive Export

- by default export the db with no track links at all.
- for users that do want my tag
  - for each track exported, read the meta.txt
  - string match the meta tags to the master category / tag pair
  - build up the exportExt.pdb database that points to the correct trackIds
  - copy it after the main db is copied.
  - same file should work for all cloned pendrives when exporting mulitples.

Since renaming tags would mean they become unused, we would need an option to "rename tag and all meta data" and all the meta.txt files in the whole db would have to be read and edited.
And another option to find and add all new categories and tags from meta.txt files.  This way you can make free edits to meta.txt and build a tag database out of your data files.
