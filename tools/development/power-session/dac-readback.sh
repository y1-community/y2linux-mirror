echo "t=$(cut -d' ' -f1 /proc/uptime) V=$(cat /sys/class/power_supply/BAT0/voltage_now)"
echo "-- codec runtime pm"
for d in /sys/bus/i2c/devices/1-0030 /sys/devices/platform/11220000.audio-controller /sys/devices/platform/sound; do [ -d $d ] && echo $d $(cat $d/power/runtime_status 2>&1) usage=$(cat $d/power/runtime_usage 2>/dev/null) active_ms=$(cat $d/power/runtime_active_time 2>/dev/null) susp_ms=$(cat $d/power/runtime_suspended_time 2>/dev/null); done
echo "-- regulators (cs43131 chain)"
grep -E "cs43131|1-0030|VGP2|vgp2" /sys/kernel/debug/regulator/regulator_summary
echo "-- gpio 15/18/20"
grep -E "gpio-(15|18|20) " /sys/kernel/debug/gpio
echo "-- pcm"
for p in /proc/asound/card0/pcm*/sub0/status; do echo $p: $(head -1 $p); done
echo "-- dapm"
for f in /sys/kernel/debug/asoc/*/dapm/bias_level /sys/kernel/debug/asoc/*/*/dapm/bias_level; do [ -f $f ] && echo $f $(cat $f); done 2>/dev/null
grep -rl . /sys/kernel/debug/asoc/*/dapm/* 2>/dev/null | head -0
echo "-- audio clocks"
grep -E "aud|afe|i2s" /sys/kernel/debug/clk/clk_summary | cut -c1-110
echo "-- audio irq"
grep -iE "afe|audio|i2s" /proc/interrupts
