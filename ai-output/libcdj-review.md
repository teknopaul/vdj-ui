# libcdj Memory Safety Review

Files reviewed: `cdj.h`, `vdj.h`, `cdj.c`, `vdj.c`, `vdj_net.c`, `vdj_pselect.c`, `vdj_discovery.c`, `vdj_store.c`

---

## CRITICAL — Likely crash causes

### 1. No upper-bound check on `player_id` in backline array access

**Files:** `vdj.c:1084` (`vdj_get_link_member`), `vdj.c:1100` (`vdj_new_link_member`), `vdj_discovery.c:368` (CDJ_ID_SET_REQ handler)

`link_members` is declared as `vdj_link_member_t* link_members[VDJ_MAX_BACKLINE + 1]` — 33 slots, valid indices 0–32. But `player_id` comes from an incoming network packet and is `unsigned char` (0–255). Three places index this array with no upper-bound check:

```c
// vdj.c:1084 — out-of-bounds READ if player_id > 32
return v->backline->link_members[player_id];

// vdj.c:1100 — out-of-bounds WRITE if player_id > 32
v->backline->link_members[d_pkt->player_id] = m;

// vdj_discovery.c:368 — no upper bound despite > 0 check
vdj_link_member_t* sender = v->backline->link_members[d_pkt->player_id];
```

A CDJ or spoofed packet with `player_id = 33..255` causes a heap write up to 223 slots past the end of the array, corrupting whatever Qt heap object follows it. **This is the most probable cause of the `QArrayData` heap corruption assert.**

**Fix:**
```c
vdj_link_member_t*
vdj_get_link_member(vdj_t* v, unsigned char player_id)
{
    if (v->backline && player_id > 0 && player_id <= VDJ_MAX_BACKLINE) {
        return v->backline->link_members[player_id];
    }
    return NULL;
}
```
Apply the same `player_id > 0 && player_id <= VDJ_MAX_BACKLINE` guard before the write in `vdj_new_link_member` and the CDJ_ID_SET_REQ handler.

---

### 2. Uninitialized `length` variable in CDJ_MASTER_RESP handler

**File:** `vdj.c:906–912` (`vdj_handle_managed_beat_unicast_datagram`)

```c
uint16_t length;          // declared but never initialized
uint8_t* pkt;
...
case CDJ_MASTER_RESP: {
    ...
    cdj_fprint_packet(stderr, packet, length, CDJ_BEAT_PORT);  // length is garbage!
```

`length` is declared at the function top alongside `pkt`, used only in the `CDJ_MASTER_REQ` branch to receive a packet length. In the `CDJ_MASTER_RESP` branch it is never set. `cdj_fprint_packet` allocates `(length * 4) + 5 + length/32` bytes and loops `length` times — with an indeterminate large value this can OOM or crash.

This only fires when a master handoff response arrives. The fix is to pass `len` (the already-known UDP datagram length) instead of `length`.

---

## HIGH — Incorrect allocation

### 3. `cdj_ip_decode` allocates pointer-size, not struct-size

**File:** `cdj.c:205`

```c
struct sockaddr_in* ip_addr = (struct sockaddr_in*) calloc(1, sizeof(struct sockaddr_in*));
//                                                              ^^ pointer size (8), not struct size (16)
```

`sizeof(struct sockaddr_in*)` is 8 bytes on 64-bit; `sizeof(struct sockaddr_in)` is 16 bytes. The allocation is half the required size. The two fields written (`sin_family` at offset 0, `sin_addr.s_addr` at offset 4) both land within the 8 bytes, so it doesn't crash in the current code. But any code that copies the full struct, passes it to `bind()`/`connect()` with the wrong length, or is compiled on a platform with a larger alignment will silently corrupt memory.

**Fix:** `calloc(1, sizeof(struct sockaddr_in))`.

---

## MEDIUM — Logic and loop bugs

### 4. `vdj_destroy` and `vdj_expire_players` loop misses index 32

**File:** `vdj.c:204`, `vdj_discovery.c:89`

```c
for (i = 0; i < VDJ_MAX_BACKLINE; i++)   // iterates 0..31, misses index 32
```

A device that claims `player_id = 32` is added to `link_members[32]` but never freed or expired.

**Fix:** Change `< VDJ_MAX_BACKLINE` to `<= VDJ_MAX_BACKLINE`.

---

### 5. `vdj_set_playing` uses XOR to clear flag bits

**File:** `vdj.c:356–368`

```c
v->master_state ^= CDJ_STAT_FLAG_PLAY;   // should be &= ~CDJ_STAT_FLAG_PLAY
v->master_state ^= CDJ_STAT_FLAG_ONAIR;
```

XOR only clears a bit if it is currently set. Calling `vdj_set_playing(v, 0)` twice in a row will first clear the flags, then re-set them, leaving the device in a phantom "playing" state without `v->active` being set.

**Fix:** Use `v->master_state &= ~CDJ_STAT_FLAG_PLAY`.

---

### 6. `vdj_open_sockets` bitmask logic uses `&` instead of `|`

**File:** `vdj.c:178–185`

```c
return (
    vdj_open_discovery_socket(v) &
    vdj_open_discovery_unicast_socket(v) << 1 &
    ...
```

`CDJ_OK = 0`, `CDJ_ERROR = 1`. With `&`, any failed socket anded with a successful one yields 0 (success). The complementary `vdj_close_sockets` correctly uses `|`. This means socket open failures are silently swallowed.

---

## MINOR — Thread safety

### 7. `vdj_update_link_member` free/re-alloc with no lock

**File:** `vdj.c:1108–1113`

```c
void
vdj_update_link_member(vdj_link_member_t* m, uint32_t ip)
{
    if (m->ip_addr) free(m->ip_addr);    // free while another thread may hold pointer
    ...
    m->ip_addr = cdj_ip_decode(ip);      // then replace
```

The discovery thread calls this to update a member's IP. The update thread concurrently calls `vdj_get_link_member` → dereferences `m->ip_addr`. No mutex protects this sequence. A use-after-free is possible, but rare since IP address changes are uncommon.

---

## Summary

| # | Severity | Location | Issue |
|---|----------|----------|-------|
| 1 | CRITICAL | `vdj.c:1084,1100`, `vdj_discovery.c:368` | No upper-bound check on player_id → heap overflow |
| 2 | CRITICAL | `vdj.c:906` | Uninitialized `length` in CDJ_MASTER_RESP → crash on master handoff |
| 3 | HIGH | `cdj.c:205` | `sizeof(struct sockaddr_in*)` instead of `sizeof(struct sockaddr_in)` |
| 4 | MEDIUM | `vdj.c:204`, `vdj_discovery.c:89` | Loop misses index 32, memory leak |
| 5 | MEDIUM | `vdj.c:362-363` | XOR instead of AND-NOT to clear flag bits |
| 6 | MEDIUM | `vdj.c:178` | AND bitmask on open_sockets hides failures |
| 7 | MINOR | `vdj.c:1108` | use-after-free race in update_link_member |

**Most likely cause of the `QArrayData` heap corruption crash: issue #1.** Any real CDJ or malformed packet arriving with `player_id > 32` overwrites heap memory adjacent to the backline array, corrupting Qt containers allocated after `vdj_backline_t`.
