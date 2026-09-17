# Hardware test checklist

Record the board revision, kernel image, application commit and configuration
before testing. Use supervised low-risk loads until communication-loss and
fault behavior are proven.

| Test | Action | Expected evidence | Result |
|---|---|---|---|
| UART device | Start service | device node opens with expected baud | |
| RS-485 direction | Send one 0x06 request | complete 8-byte frame, no truncated tail | |
| Split response | Delay response bytes | parser waits and emits one frame | |
| Concatenated response | Return two frames together | parser emits both in order | |
| CRC error | Corrupt one byte | state does not change; error counter grows | |
| Write confirmation | Write register 0x0010 | `sent` then `applied` with same request ID | |
| No response | Disconnect A/B pair | bounded retry then `timed_out` | |
| MQTT reconnect | Restart broker | reconnect, resubscribe and resume state events | |
| Process restart | Kill application | systemd restarts and journal records reason | |
| Clean stop | `systemctl stop` | threads join and descriptors close | |
| Long run | Repeat commands for 8 hours | stable RSS, no queue growth, no control starvation | |

Do not call the system ready for unattended heaters, motors, relays or charging
loads until physical stop/fault/interlock behavior has been verified.

