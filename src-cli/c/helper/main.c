/*
 * trakcrate-helper: the compiled part of the trakcrate command line tools (doc/trakcrate/cli.md).
 * Installed in /usr/lib/trakcrate, not on $PATH. The bash commands call it for what bash cannot do.
 */
#include <stdio.h>

#include "dispatch.h"

int cmd_version(int argc, char **argv);
int cmd_natsort(int argc, char **argv);
int cmd_probe(int argc, char **argv);
int cmd_sha256(int argc, char **argv);
int cmd_kv(int argc, char **argv);
int cmd_analyse(int argc, char **argv);
int cmd_export(int argc, char **argv);
int cmd_verify(int argc, char **argv);
int cmd_settings_check(int argc, char **argv);
int cmd_pdb(int argc, char **argv);
int cmd_anlz(int argc, char **argv);
int cmd_rbdrive(int argc, char **argv);
int cmd_rbtrack(int argc, char **argv);

static const struct helper_cmd commands[] = {
    { "version", cmd_version, "print the version and the API version" },
    { "natsort", cmd_natsort, "sort lines (or -z NUL separated names) in the natural order of a library" },
    { "sha256", cmd_sha256, "SHA-256 of files, like sha256sum" },
    { "kv", cmd_kv, "get, set or unset a key of a key/value file (meta.txt ...)" },
    { "analyse", cmd_analyse, "beat grid, waveform and loudness of one track directory" },
    { "export", cmd_export, "build a rekordbox format drive from lists of track directories" },
    { "verify", cmd_verify, "check a rekordbox format drive with libcdj's readers" },
    { "settings-check", cmd_settings_check, "check the CRC of DEVSETTING.DAT / MYSETTING.DAT files" },
    { "pdb", cmd_pdb, "tables or pages of an export.pdb as TSV" },
    { "anlz", cmd_anlz, "tags and decoded content of an ANLZ file as TSV" },
    { "rbdrive", cmd_rbdrive, "the database of a rekordbox drive as TSV lines (tracks, playlists, entries)" },
    { "rbtrack", cmd_rbtrack, "write the rekordbox values of one drive track into a new track directory" },
    { "probe", cmd_probe, "tags, format and cover of an audio file as meta.txt lines" },
    { NULL, NULL, NULL }
};

int main(int argc, char **argv)
{
    return helper_run(commands, stdout, stderr, argc, argv);
}
