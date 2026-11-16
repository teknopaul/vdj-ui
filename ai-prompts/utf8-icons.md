#app

🎧 - vdj-ui

![img/lock-16x16.png](../img/lock-16x16.png) - lock bpm

# Player

▶️⏸️⏹️⏺️⏯️◀️
⏭️⏮️⏩⏪⏏️

🔴⚪ - recording

## Player states

```
snprintf(state, 24, "%s %s%s%s ", 
    flags & CDJ_STAT_FLAG_PLAY   ? "▶️" : "⏸️",
    flags & CDJ_STAT_FLAG_SYNC   ? "🟡" : "⚪",
    flags & CDJ_STAT_FLAG_MASTER ? "🔴" : "⚪",
    flags & CDJ_STAT_FLAG_ONAIR  ? "🔇" : "🔊"
    );
```            
