# Architecture

## Runtime data flow

```text
MQTT command
    |
    v
JSON validation -> bounded command queue -> Modbus RTU encoder -> UART/RS-485
                                                              |
MQTT state  <- bounded event queue <- device state <- RTU parser/CRC <-+
```

The code intentionally separates these meanings:

- `queued`: accepted by the process.
- `sent`: all bytes were written and `tcdrain()` confirmed UART transmission.
- `applied`: a matching device response was parsed and validated.
- `timed_out`: no matching response arrived before the bounded retry policy ended.
- `failed`: the local transport or queue failed.

Writing bytes is not reported as device success.

## Modules

| Module | Responsibility |
|---|---|
| `SerialPort` | `termios`, non-blocking file descriptor, `poll`, optional `TIOCSRS485` |
| `ModbusRtuParser` | incremental split/concatenated frame handling, bounds and CRC |
| `ModbusTcp` | MBAP/PDU encode/decode and RTU/TCP bridge helpers |
| `MqttClient` | dependency-free MQTT 3.1.1 CONNECT/SUBSCRIBE/PUBLISH/keepalive |
| `DeviceState` | reported registers and pending request correlation |
| `Gateway` | serial worker, MQTT worker, bounded queues, timeout/retry |

## Thread ownership

- Serial thread owns the serial descriptor and RTU parser.
- Network thread owns the MQTT socket.
- Commands cross from network to serial through a bounded queue.
- State events cross from serial to network through another bounded queue.
- `DeviceState` protects its pending map and register cache with a mutex.

This avoids concurrent reads/writes on one parser or one MQTT socket and makes
shutdown order explicit.

## STM32MP157 split

The application is intended for the Cortex-A7/OpenSTLinux side. Hard real-time
sampling or control can stay on the Cortex-M4 side and expose a stable device
protocol to Linux through RPMsg or a board-specific serial interface. This
repository does not pretend that a Linux process replaces safety-critical
real-time firmware.

