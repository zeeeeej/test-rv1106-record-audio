# hd-record-audio

Luckfox RV1106 命令行录音工具。固定输出 16 kHz、16-bit、单声道 PCM WAV，默认启用 Rockchip VQE 的 ANR 降噪，其他声音增强模块保持关闭。

音频术语、ANR 原理及 `log/test-on.wav` / `log/test-off.wav` 对比说明见 [名词解释](名词解释.md)。

## 使用

```sh
/userdata/hd-record-audio/bin/hd_record_audio \
  -o /userdata/recording.wav -d 10
```

- `-o, --output FILE.wav`：必填，不覆盖已有文件。
- `-d, --duration SECONDS`：可选；省略时持续录音，按 `Ctrl-C` 或发送 `SIGTERM` 停止。
- `--no-denoise`：禁用 VQE，录制左侧麦克风原声。
- `--vqe-config FILE`：覆盖默认 ANR 配置路径。

录音期间写入 `FILE.wav.part`，正常结束后发布为 `FILE.wav`。运行异常或空间不足时保留带有效 WAV 头的 `.part` 文件。

## 原生测试

```sh
cmake -S . -B build-native \
  -DHD_RECORD_AUDIO_BUILD_RECORDER=OFF \
  -DBUILD_TESTING=ON
cmake --build build-native
ctest --test-dir build-native --output-on-failure
```

## RV1106 交叉编译

`RK_MPI_ROOT` 指向包含 `include/` 和 `lib/uclibc/` 的 `luckfox_pico_rkmpi_example`；`CROSS_COMPILE` 是完整工具链前缀，末尾包含 `-`。

```sh
export RK_MPI_ROOT=/path/to/luckfox_pico_rkmpi_example
export CROSS_COMPILE=/path/to/bin/arm-rockchip830-linux-uclibcgnueabihf-
scripts/build-arm.sh
```

安装树位于 `build-arm/install/`。部署到当前测试板：

```sh
scripts/deploy-adb.sh f3799b110c647633 build-arm/install
```

程序动态使用板端 `/oem/usr/lib` 中已有的 Rockit、MPP、RGA 和 rkaudio 库，不复制或替换系统库。
