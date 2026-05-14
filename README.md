# jiaozhun_miniastro

独立的 Qt 编码器监视小程序，只在本目录内实现，不修改
`/home/q/workspace/xxgcdx/stellarium_sharedmem/sharedmem`。

程序启动后默认查找 `QHY5III585`，通过 QHYCCD SDK 调用同名
`ReadturntableEncoder(...)` 读取编码器数据，并用折线图实时显示
`TianShanNode_EncoderDEC`。

界面上的 `Interval ms` 可以修改读取编码器数据的速度，范围是 `1-2000 ms`，单位是毫秒。
例如 `200 ms` 表示每 0.2 秒读一次，`1000 ms` 表示每 1 秒读一次。

界面上的 `Window s` 可以修改折线图显示最近多少秒的数据，范围是 `1-2000 s`。

`Bulk DEC` 会把 DEC 的 4 个字节合并成一次 SDK 读取，通常比逐字节读取更快。
`Trigger C0` 保持和原工程一致的触发写入；关闭它会进一步减少 SDK 调用次数，
但如果设备数据不再刷新，请重新打开。

SDK 读取运行在后台线程，`Actual` 显示后台实际采样间隔，`Read` 显示单次读取耗时。

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
