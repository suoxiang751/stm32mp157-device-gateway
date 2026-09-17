# STM32MP157 Linux Device Gateway

面向 STM32MP157/OpenSTLinux 的设备通信网关示例。项目把云端 MQTT 控制请求转换为
Modbus RTU 指令，通过 UART/RS-485 与设备控制板通信，并将确认后的设备状态重新上报。

这不是只有界面的演示仓库。核心代码包含可编译的 Linux 串口、增量报文解析、CRC、
Modbus RTU/TCP 转换、MQTT 客户端、线程与有界队列、超时重试、systemd 部署和测试。

## 主要能力

- STM32MP157 Cortex-A7/OpenSTLinux 用户态应用
- C++17、CMake、pthread/`std::thread`
- `termios` 串口配置、非阻塞 I/O、`poll()`、`tcdrain()`
- Linux `TIOCSRS485` 半双工方向控制
- Modbus RTU 0x03/0x06、CRC16、半帧/粘帧/错帧恢复
- Modbus TCP MBAP/PDU 编解码与 RTU/TCP 桥接辅助函数
- 无第三方运行库的 MQTT 3.1.1 基础客户端
- `queued -> sent -> applied/timed_out/failed` 控制状态闭环
- 有界命令队列和事件队列、有限重试、断线退避重连
- OpenSTLinux SDK 交叉编译脚本和 systemd unit
- Linux PTY Modbus 从站模拟器及协议单元测试

## 架构

```text
MQTT Broker
    | command JSON                       state/result JSON
    v                                             ^
MQTT worker -> command queue -> serial worker -> event queue
                                  |       ^
                                  v       |
                         UART / RS-485 / Modbus RTU
                                  |
                         MCU / PLC / device board
```

详细设计见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。
实现与验证边界见 [docs/VERIFICATION.md](docs/VERIFICATION.md)。
简历关键词与代码入口见 [docs/INTERVIEW_MAPPING.md](docs/INTERVIEW_MAPPING.md)。

## 本机构建与测试

Linux、WSL 或 Ubuntu CI：

```bash
./scripts/build_native.sh
```

等价命令：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

单元测试覆盖 CRC 已知向量、半帧、粘帧、CRC 错误恢复、Modbus TCP/RTU 转换、
云端参数范围校验，以及“发送后等待设备回包确认”的状态逻辑。

## 无硬件演示

第一终端启动虚拟 Modbus RTU 从站：

```bash
python3 tools/modbus_rtu_sim.py
```

它会打印类似 `/dev/pts/7` 的虚拟串口。复制配置文件并修改：

```ini
serial.device=/dev/pts/7
serial.rs485=false
mqtt.enabled=false
```

第二终端执行一次写寄存器：

```bash
./build/device_gateway --config ./gateway-local.conf --once-write 0x0010 1
```

网关先输出 `sent`，解析到从站回包后再输出 `applied`。断开模拟器时则在有限重试后
输出 `timed_out`，不会把本地 `write()` 成功误报为设备执行成功。

## MQTT 联调

把 `mqtt.enabled` 改成 `true` 并配置 Broker。向命令主题发布：

```json
{
  "request_id": "demo-001",
  "action": "write_single_register",
  "slave": 1,
  "address": 16,
  "value": 1
}
```

示例：

```bash
mosquitto_sub -h 127.0.0.1 -t device/demo/state -v
mosquitto_pub -h 127.0.0.1 -t device/demo/command \
  -m '{"request_id":"demo-001","action":"write_single_register","address":16,"value":1}'
```

MQTT 当前实现面向可信局域网实验环境，支持 TCP、QoS 0 发布、订阅和 Keep Alive。
生产公网部署应在外层增加 TLS、账号鉴权、设备证书、ACL 与安全更新策略。

## STM32MP157/OpenSTLinux 交叉编译

先安装 ST OpenSTLinux SDK，并 source 对应的 `environment-setup-*` 文件：

```bash
source /opt/st/stm32mp*/environment-setup-cortexa7t2hf-neon-vfpv4-ostl-linux-gnueabi
./scripts/build_openstlinux.sh
```

产物为 `build-stm32mp1/device_gateway`。部署时同时安装配置和
`deploy/stm32mp157-device-gateway.service`。

## 接真实硬件前

1. 根据原理图确认 UART 实例、引脚复用、电平、A/B 极性和公共地。
2. 确认内核设备节点与 RS-485 驱动是否支持 `TIOCSRS485`。
3. 根据设备手册替换寄存器地址、量程、只读/可写属性和异常码。
4. 先验证低风险寄存器，再验证电机、加热、继电器或充电控制。
5. 按 [硬件测试清单](docs/HARDWARE_TEST_CHECKLIST.md)保存日志和结果。

## 当前边界

- 已在代码层实现并由单元测试覆盖协议和状态逻辑。
- Windows 构建用于协议单元测试；真实串口运行目标是 Linux/OpenSTLinux。
- 仓库没有绑定某一客户的私有寄存器表、密钥或服务器地址。
- 尚未替代具体产品的电气安全、BMS、功率变换或实时控制固件。
- 未经真实 STM32MP157 板卡、485 收发器和目标设备联调，不应宣称已完成量产验证。

## License

MIT
