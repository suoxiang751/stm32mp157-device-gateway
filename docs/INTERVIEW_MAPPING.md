# Resume and interview mapping

Use this page to connect every project statement to code you can explain.

| Interview keyword | Code entry | What to explain |
|---|---|---|
| STM32MP157 Linux application | `README.md`, toolchain file | A7 runs OpenSTLinux; M4 remains suitable for real-time work |
| pthread/thread model | `Gateway::start`, `serial_loop`, `network_loop` | thread ownership, join order and why the queues are bounded |
| termios | `SerialPort::open_port` | raw mode, baud, 8N1, VMIN/VTIME and device node |
| select/poll | `SerialPort::wait_readable` | readiness is not a complete application frame |
| RS-485 | `TIOCSRS485` setup | kernel controls DE/RE; `tcdrain` waits for UART transmission |
| half frame/sticky frame | `ModbusRtuParser::feed` | preserve partial bytes, calculate expected length, loop over complete frames |
| CRC and bounds | `decode_rtu_frame`, parser max buffer | reject corruption before changing state and resynchronize safely |
| Modbus RTU/TCP | `modbus_rtu.cpp`, `modbus_tcp.cpp` | RTU CRC versus TCP MBAP/Transaction ID and shared PDU |
| MQTT | `MqttClient` | CONNECT/CONNACK, SUBSCRIBE/SUBACK, QoS 0, keepalive and reconnect |
| command closed loop | `DeviceState`, `Gateway::handle_frame` | distinguish local send from device-confirmed applied state |
| timeout/retry | `Gateway::serial_loop` | fixed deadline, bounded retries and no infinite resend |
| cross compilation | `build_openstlinux.sh` | SDK sysroot, target compiler and why host build is insufficient |
| systemd | service unit | start order, restart policy, journal and clean stop |

## Recommended code-reading order

1. `tests/test_main.cpp`: see observable behavior first.
2. `modbus_rtu.cpp`: understand the byte-level protocol.
3. `device_state.cpp`: understand sent versus applied.
4. `gateway.cpp`: connect queues, serial and network threads.
5. `serial_port.cpp`: review Linux system calls.
6. `mqtt_client.cpp`: review packet framing and reconnect boundaries.
7. Deployment and cross-compilation files.

For each item, prepare four sentences: the problem, the design choice, the
failure case, and the verification method.

