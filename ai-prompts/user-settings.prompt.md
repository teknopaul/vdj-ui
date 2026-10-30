The users settings things are NOT optional the drive needs them all.

Currently we only write them in advanced settings, we need to write them by default (if they are missing)

We need some options...

So the User Settings tree view in trakcrate needs another level of tree structure.
N.B. this breaks our doc/trakcrate/trakcrate-db-format.md design a bit but fortunately its not published yet so its still changeable.  Code and docs need updating.  


## Settings options for the user in trakcrate

1. Copy them from a real rekordbox pendrive.
  - User should be able to save multiple named settings sets.
  - User should be able to view the contents, limited read only since we dont know the full format details yet.
2. When a user installs a fresh copy of vdj-ui they will have no settings
  - Need Create new settings option
    -  Ask their user name for the djprofile.nxs file and populate it.
    -  Other than that use setting files that we have in this repo copied from real rekordbox

One set of settings should be marked the default.  Many users will only have one set.

## Exporting options

When exporting we should always either ....

1. leave existing if they exist
2. copy new files if they are missing
3. update settings with trakcrate versions

The should be a combobox drop down to choose which settings to use when we are going to have to add or update for the export.

Virtual Pendrives should support linking to a set of settings to be the default for that virtual pendrive, (that needs a trakcrate db extension that needs code and documenting too)


# Settings viewer

Since we have different sets of setting we need a viewer so the user can see the differences.
We dont know the full fomrat of setting files yet.  But we know a bit.  We should show what we can and not much should be editable.
The username in djprofil.nxs needs to be editable or all users get my username.  We need to work out that format so we can edit it

