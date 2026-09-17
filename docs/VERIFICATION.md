# Verification status

This file keeps implementation, local verification and hardware evidence separate.

| Item | Status | Evidence |
|---|---|---|
| CMake project and host build | verified | MinGW GCC build completed on 2026-09-17 |
| Static analysis | verified | cppcheck warning/performance/portability scan completed with no findings |
| CRC16 known vector | verified | `gateway_tests` |
| RTU split/concatenated frames | verified | `gateway_tests` |
| CRC corruption recovery | verified | `gateway_tests` |
| Modbus TCP MBAP encode/decode | verified | `gateway_tests` |
| TCP PDU to RTU frame conversion | verified | `gateway_tests` |
| MQTT JSON command bounds | verified | `gateway_tests` |
| pending/sent/applied/timeout state model | verified | `gateway_tests` |
| Linux PTY simulator syntax | verified | Python `py_compile` |
| Native Linux compile | CI-configured | GitHub Actions workflow; run after repository push |
| OpenSTLinux SDK cross-compile | implemented, not locally run | requires installed ST SDK |
| STM32MP157 UART/RS-485 | not yet hardware-verified | requires target board and transceiver |
| MQTT broker interoperability | implemented, not yet integration-tested | requires broker test |
| Product register map | intentionally absent | replace with target-device specification |

The current repository is suitable for learning, code review and host protocol
tests. Hardware acceptance requires completing `HARDWARE_TEST_CHECKLIST.md`.
