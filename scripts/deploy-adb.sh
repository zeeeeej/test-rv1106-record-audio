#!/bin/sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "Usage: $0 ADB_SERIAL INSTALL_DIR" >&2
    exit 2
fi

serial=$1
install_dir=$2
adb -s "$serial" shell 'mkdir -p /userdata/hd-record-audio/bin /userdata/hd-record-audio/share/hd-record-audio'
adb -s "$serial" push "$install_dir/bin/hd_record_audio" /userdata/hd-record-audio/bin/hd_record_audio
adb -s "$serial" push "$install_dir/bin/hd_voice_client" /userdata/hd-record-audio/bin/hd_voice_client
adb -s "$serial" push "$install_dir/bin/hd_voice_command" /userdata/hd-record-audio/bin/hd_voice_command
adb -s "$serial" push "$install_dir/share/hd-record-audio/config_aivqe_anr.json" /userdata/hd-record-audio/share/hd-record-audio/config_aivqe_anr.json
adb -s "$serial" shell 'chmod 755 /userdata/hd-record-audio/bin/hd_record_audio /userdata/hd-record-audio/bin/hd_voice_client /userdata/hd-record-audio/bin/hd_voice_command; chmod 644 /userdata/hd-record-audio/share/hd-record-audio/config_aivqe_anr.json'
