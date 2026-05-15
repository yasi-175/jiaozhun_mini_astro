# jiaozhun_miniastro

独立的 Qt 编码器监视小程序，只在本目录内实现，不修改
`/home/q/workspace/xxgcdx/stellarium_sharedmem/sharedmem`。

程序启动后默认查找 `QHY5III585`，通过 QHYCCD SDK 调用同名
`ReadturntableEncoder(...)` 读取编码器数据，并用折线图实时显示
`TianShanNode_EncoderDEC`、由编码器差分换算的实际 DEC 脉冲速度、以及累计位置偏差。

界面上的 `Interval ms` 可以修改读取编码器数据的速度，范围是 `1-2000 ms`，单位是毫秒。
例如 `200 ms` 表示每 0.2 秒读一次，`1000 ms` 表示每 1 秒读一次。

界面上的 `Window s` 可以修改折线图显示最近多少秒的数据，范围是 `1-2000 s`。

`Bulk DEC` 会把 DEC 的 4 个字节合并成一次 SDK 读取，通常比逐字节读取更快。
`Trigger C0` 保持和原工程一致的触发写入；关闭它会进一步减少 SDK 调用次数，
但如果设备数据不再刷新，请重新打开。

SDK 读取运行在后台线程，`Actual` 显示后台实际采样间隔，`Read` 显示单次读取耗时。

## Mount DEC control

本程序现在也可以通过 USB 串口直接控制赤道仪 A 设备的 DEC 轴，不需要在本程序里启动
INDI。参考固件里的 `SerialCommand::process()`，A 设备 USB 串口接收：

- `MOTOR:MODE,0`：切到速度模式
- `MOTOR:SPEED,<kHz>`：设置 DEC 轴速度，正负号决定方向
- `MOTOR:SPEED,0`：停止 DEC 轴

界面第二行选择 A 设备串口，默认 `115200` baud；连接后用 `DEC +` / `DEC -`
按当前 `DEC kHz` 速度转动，`DEC Stop` 停止。编码器读取和赤道仪控制是两条独立链路：
编码器仍然走 QHYCCD SDK，DEC 控制走 USB 串口。

图表包含四路折线：

- `TianShanNode_EncoderDEC`：25 位多摩川编码器原始值。
- `Command DEC Speed`：当前下发给电机的理论/命令速度，单位 Hz；导星脉冲期间会显示 `Base ± Delta`。
- `Actual DEC Speed`：根据相邻编码器采样差值和采样间隔换算出的实际脉冲速度，单位 Hz。
- `Position Error`：理论位置与实际位置的有符号累计偏差，单位角秒。

换算参数：电机 `200` 步/圈，驱动器 `256` 细分，减速器 `1:100`，输出轴编码器满量程
`2^25`。方向按当前设备实测处理：`DEC-` 时编码器值增加，`DEC+` 时编码器值减少。

## Guide simulation

`Guide sim` 用来仿真 PHD2 一类导星脉冲控制：以 `Base kHz` 作为恒星跟踪基础速度，
每隔 `Exposure` 时间读取一次当前有符号位置偏差，并按 `Delta kHz` 对应的修正角速度换算
出脉冲时长。脉冲期间下发 `Base + Delta` 或 `Base - Delta`，脉冲结束自动回到 `Base`。

`DEC RMS` 按导星曝光节拍更新，不按原始编码器采样频率更新。也就是说当 `Exposure = 1000 ms`
时，RMS 每 1 秒更新一次；统计窗口使用当前 `Window s` 范围内的导星误差样本。

默认参数按当前测试值设置：

- `Base kHz = 0.05942`
- `Delta kHz = 0.03500`
- `Exposure = 1000 ms`
- `Agg = 100%`
- `Max pulse = 1000 ms`

位置偏差图仍然以基础速度作为理论参考轨迹；导星脉冲只是修正实际位置，不会被计入新的理论轨迹。

## Build

```bash
cd /home/q/workspace/jiaozhun_mini_astro
/opt/Qt/5.15.2/gcc_64/bin/qmake jiaozhun_miniastro.pro
make -j$(nproc)
```

## Run

```bash
./jiaozhun_miniastro
```

可选参数：

```bash
./jiaozhun_miniastro --device QHY5III585 --interval 200 --library /usr/local/lib/libqhyccd.so
```
