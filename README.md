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

## 语音命令客户端

`hd_voice_client` 复用相同的 16 kHz 单声道录音和 ANR，录音结束后上传到业务网关，校验并保存返回的 WAV，然后通过板端 `aplay` 播放：

```sh
HD_VOICE_API_KEY=devkey-gateway \
/userdata/hd-record-audio/bin/hd_voice_client \
  --server-url http://192.168.1.10:8080 \
  -o /userdata/command.wav \
  --reply-output /userdata/reply.wav
```

- 默认按回车结束录音；也可使用 `-d SECONDS` 定时结束。
- `--playback-device` 默认是 `plughw:0,0`。
- 录音、应答及其 `.part` 路径都不会被覆盖。
- `HD_VOICE_API_KEY` 只通过 curl 标准输入传递，不会出现在命令行参数中。
- 退出码：`2` 参数、`3` 录音、`4` 文件、`5` 配置、`6` 网络、`7` 服务、`8` 响应格式、`9` 播放。

板端安装后也可以直接运行包装脚本：

```sh
/userdata/hd-record-audio/bin/hd_voice_command
```

脚本自动创建 `/userdata/voice`，并依次使用 `command-1.wav` / `reply-1.wav`、
`command-2.wav` / `reply-2.wav` 等未占用文件名。参数会继续传给客户端，例如
`hd_voice_command -d 5`。以下环境变量可覆盖默认设置：

- `HD_VOICE_SERVER_URL`：默认 `http://192.168.1.115:8080`
- `HD_VOICE_API_KEY`：默认 `devkey-gateway`
- `HD_VOICE_DIR`：默认 `/userdata/voice`
