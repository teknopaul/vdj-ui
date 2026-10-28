# Linux Networking (AutoIP)

## Why AutoIP

Pioneer DJ gear (Pro DJ Link) has no DHCP server and no fixed address. Each deck, mixer and
vdj-ui picks a random link-local IPv4 address in `169.254.0.0/16`, probes it with ARP to check
nobody else has it, and then announces it. This is IPv4 link-local addressing, also called
AutoIP or IPv4LL ([RFC 3927](https://datatracker.ietf.org/doc/html/rfc3927)).

So your computer needs a `169.254.x.x` address on the NIC that is cabled to the decks, or the
decks will not see it (and it will not see them).

## Use a second network card

A second NIC (a cheap USB ethernet dongle is fine) is recommended. Put the decks on it and leave
your normal NIC for internet and DHCP. A NIC can have a link-local address next to a DHCP one,
but it is easy to confuse routing that way.

- vdj-ui asks **once** which NIC is cabled to the decks, then remembers it.
- If the NIC changes (new dongle, different port), use **Network > Forget Saved Network
  Interface** and you will be asked again on the next start.

## Per session or permanent

- **Per session**: **Network > AutoIP...** claims an address with `avahi-autoipd` for as long as
  the app is running (falling back to NetworkManager if that fails). It is released when you stop
  it or quit.
- **Permanent**: **Network > Auto IP (permanent)...** shows the commands and sets up a
  NetworkManager profile that gets a link-local address every time the cable is plugged in or you
  boot, so the app has nothing to do at start-up.

## Doing it permanently by hand (Ubuntu / NetworkManager)

Find the interface and the profile on it (`enx...` or `eth1` is typically the dongle):

```bash
ip -br link
nmcli -g GENERAL.CONNECTION dev show eth1
```

Make the existing profile link-local only (no waiting for DHCP, no IPv6) and bring it up:

```bash
nmcli con mod "Wired connection 2" ipv4.method link-local ipv6.method disabled connection.autoconnect yes connection.interface-name eth1
nmcli con up "Wired connection 2"
```

If the NIC has no profile yet, create one:

```bash
nmcli con add type ethernet ifname eth1 con-name vdj-prolink-eth1 ipv4.method link-local ipv6.method disabled connection.autoconnect yes
nmcli con up vdj-prolink-eth1
```

Check it worked:

```bash
ip -4 addr show eth1        # expect inet 169.254.x.x/16
```

Undo it (back to DHCP):

```bash
nmcli con mod vdj-prolink-eth1 ipv4.method auto
# or remove the profile entirely
nmcli con delete vdj-prolink-eth1
```

## Per session by hand (no NetworkManager changes)

```bash
sudo apt install avahi-autoipd
sudo avahi-autoipd --no-chroot eth1      # claims a 169.254.x.x address, keep it running
sudo avahi-autoipd --kill eth1           # release it
```

## Troubleshooting

- Make sure the cable goes to the decks or a switch/hub, not directly to your router.
- Decks that never appear: check `ip -4 addr show <nic>` has a `169.254.` address on the right
  NIC, and that no firewall blocks UDP 50000-50002 and TCP 1051.
