#!/bin/bash
# vdj-autoip - ProLink auto-IP helper for vdj-ui
# Runs as root (via pkexec). Claims a 169.254.x.x address on the given
# interface using avahi-autoipd and keeps it alive while the UI is running.
# Stopped by the UI creating /tmp/vdj-autoip-stop.
set -u
iface="$1"
stopfile=/tmp/vdj-autoip-stop
datfile=/var/tmp/autoip.dat
pidfile=/var/tmp/autoip-maintain.pid

rm -f "$stopfile" "$datfile"

avahi-autoipd "$iface" 2>&1 | awk '/^Successfully claimed IP/ {print $5}' > "$datfile" &
avahi_pid=$!
echo $$ > "$pidfile"

cleanup() {
  kill "$avahi_pid" 2>/dev/null
  avahi-autoipd -k "$iface" 2>/dev/null
  ip_addr=$(cat "$datfile" 2>/dev/null)
  if [ -n "$ip_addr" ]; then
    ip addr del "$ip_addr/16" dev "$iface" 2>/dev/null
  fi
  rm -f "$pidfile" "$datfile" "$stopfile"
  exit 0
}
trap cleanup EXIT INT TERM

sleep 11
while true; do
  [ -e "$stopfile" ] && break
  fip=$(cat "$datfile" 2>/dev/null)
  nip=$(ifconfig "$iface" 2>/dev/null | awk '/ *inet/ {print $2}')
  if [[ "$fip" =~ ^169\.254\.* ]]; then
    if ! ifconfig "$iface" | grep -q "inet"; then
      ip addr add "$fip/16" broadcast 169.254.0.0 dev "$iface"
    fi
  elif [[ "$nip" =~ ^169\.254\.* ]]; then
    ip addr add "$nip/16" broadcast 169.254.0.0 dev "$iface" 2>/dev/null
  fi
  sleep 5
done
