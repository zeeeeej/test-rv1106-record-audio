#!/bin/sh
set -eu

voice_dir=${HD_VOICE_DIR:-/userdata/voice}
server_url=${HD_VOICE_SERVER_URL:-http://192.168.1.115:8080}
voice_client=${HD_VOICE_CLIENT:-/userdata/hd-record-audio/bin/hd_voice_client}
export HD_VOICE_API_KEY=${HD_VOICE_API_KEY:-devkey-gateway}

mkdir -p "$voice_dir"

index=1
while [ -e "$voice_dir/command-$index.wav" ] || \
      [ -e "$voice_dir/command-$index.wav.part" ] || \
      [ -e "$voice_dir/reply-$index.wav" ] || \
      [ -e "$voice_dir/reply-$index.wav.part" ]; do
    index=$((index + 1))
done

echo "Voice command index: $index"
echo "Recording: $voice_dir/command-$index.wav"
echo "Reply:    $voice_dir/reply-$index.wav"

"$voice_client" \
    --server-url "$server_url" \
    -o "$voice_dir/command-$index.wav" \
    --reply-output "$voice_dir/reply-$index.wav" \
    "$@"
