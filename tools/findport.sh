#!/usr/bin/env bash
# Watch for the board's USB-serial port to appear, and name the bridge chip.
#
# Bring-up aid: lets you swap cables and ports and get an answer in a second,
# rather than re-running ls and squinting. Distinguishes the two failure modes
# that look identical from the command line -- a cable with no data pairs (no
# USB device at all) versus a missing driver (device present, no /dev/cu entry).

set -uo pipefail

timeout=${1:-120}
echo "Watching for a USB-serial port (${timeout}s). Plug in / swap cables now."
echo "Ctrl-C to stop."
echo

deadline=$((SECONDS + timeout))
while [ $SECONDS -lt $deadline ]; do
  # The two built-in ports are always present and are not our board.
  port=$(ls /dev/cu.* 2>/dev/null | grep -vE 'Bluetooth-Incoming-Port|debug-console' || true)
  usb=$(ioreg -rc IOUSBHostDevice 2>/dev/null | grep '"USB Product Name"' |
        sed 's/.*= "//; s/"$//' || true)

  if [ -n "$port" ]; then
    echo "PORT FOUND:"
    echo "$port" | sed 's/^/  /'
    [ -n "$usb" ] && { echo "USB device:"; echo "$usb" | sed 's/^/  /'; }
    echo
    case "$port" in
      *usbserial*)   echo "CP210x bridge. macOS has a built-in driver." ;;
      *wchusbserial*|*SLAB*) echo "CH34x/CH910x bridge." ;;
      *usbmodem*)    echo "Native USB (or CDC bridge). No driver needed." ;;
    esac
    echo
    echo "Next:"
    echo "  export PATH=\"\$HOME/.platformio/penv/bin:\$PATH\""
    echo "  cd $(cd "$(dirname "$0")/.." && pwd)"
    echo "  pio run -e bringup -t upload && pio device monitor"
    exit 0
  fi

  if [ -n "$usb" ]; then
    echo "USB device present but NO serial port -- this is a driver problem:"
    echo "$usb" | sed 's/^/  /'
    echo
    echo "Install the bridge driver, then approve it in"
    echo "System Settings > Privacy & Security, and reboot."
    exit 2
  fi

  sleep 1
done

echo "Nothing appeared in ${timeout}s. No USB device reached the Mac at all."
echo "The cable's data pairs are the prime suspect -- try a cable you have"
echo "actually moved files over, and plug straight into the Mac, not a hub."
exit 1
