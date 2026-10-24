# UDP port 111 for the LINK export

A deck that wants the files of a player asks that player's NFS portmapper on **UDP port 111**, a privileged port (below 1024). The LINK export needs it: without it
the deck never mounts the stick and nothing appears (see `doc/link-export.md`). A rekordbox *PC* is asked on 50111 instead, which needs no privilege; that is not what
a player is asked. vdj-ui's file service binds 111 and 50111; if 111 is refused it keeps working on 50111 and shows a warning box with the two commands below.

Check what is in place:

    cat /proc/sys/net/ipv4/ip_unprivileged_port_start     # 111 or lower: any program may bind 111
    getcap <path to the vdj-ui binary>                    # cap_net_bind_service=ep: this binary may
    ss -ulnp | grep ':111 '                               # who holds UDP 111 (rpcbind/portmap from another package?)

If `rpcbind` or another portmapper already holds 111 (`sudo systemctl stop rpcbind rpcbind.socket`), vdj-ui cannot bind it.

## Option 1: lower the first unprivileged port (simple, survives rebuilds)

Until the next reboot:

    sudo sysctl -w net.ipv4.ip_unprivileged_port_start=111

Permanently:

    echo 'net.ipv4.ip_unprivileged_port_start=111' | sudo tee /etc/sysctl.d/99-vdj-ui.conf
    sudo sysctl --system

Trade-off: it applies to **every program of every user** on the machine: any of them may now listen on ports 111-1023 (111 itself, and others such as 443 or 80
if you set it lower). Use 111, not 0, and not on a shared or internet-facing machine.

## Option 2: give the binary the capability (best for an installed program)

    sudo setcap cap_net_bind_service=+ep /full/path/to/the/real/vdj-ui

- Use the **real file**, not a symlink (`readlink -f $(which vdj-ui)`); `setcap` on a symlink fails.
- The capability is stored on the file and **is lost whenever the file is replaced**: every rebuild with `make`, every package upgrade. For a development tree redo it after each
  build; for a Debian package put it in `postinst` (`setcap cap_net_bind_service=+ep /usr/bin/vdj-ui`, `make-deb.sh` builds the package).
- A binary with file capabilities ignores `LD_LIBRARY_PATH` and similar variables (the dynamic loader runs in secure-exec mode); programs that rely on them will behave differently.
- Remove it again: `sudo setcap -r /full/path/to/vdj-ui`.

## Option 3: redirect 111 to 50111 in the firewall (no capability, no sysctl)

The portmapper always also listens on 50111, so inbound packets for 111 can be redirected there:

    # nftables
    sudo nft add table ip vdjui
    sudo nft add chain ip vdjui pre '{ type nat hook prerouting priority dstnat; }'
    sudo nft add rule ip vdjui pre iifname "enx00e04c68019a" udp dport 111 redirect to :50111

    # or iptables
    sudo iptables -t nat -A PREROUTING -i enx00e04c68019a -p udp --dport 111 -j REDIRECT --to-ports 50111

Replace the interface name with the one cabled to the decks. This needs root once (and again after a reboot unless you persist the rules) and no change to vdj-ui. Untested with the decks
so far: if something else is wrong the deck-side symptom looks the same as having no portmapper, so test with Option 1 or 2 first.

## What a permanent, built-in solution could look like (not implemented)

The app already asks for root through polkit for AutoIP. The same mechanism could run `setcap cap_net_bind_service=+ep` on the real binary once, after a confirmation, and the warning
box could offer a "Grant" button instead of printing commands. Caveats: it must resolve the real path, the capability disappears on every rebuild or upgrade (so the app should check
`getcap` at start and offer again), and packages are better served by `postinst`. A second approach is a small separate helper that owns port 111 and forwards to vdj-ui, which keeps the
privilege out of the GUI program entirely.
