#pragma once
// Line-based serial console for development:
//   shot                 dump the screen as raw RGB888 (see tools/screenshot.py)
//   tap X Y              inject a tap
//   drag X Y DY          inject a vertical drag of DY pixels starting at (X, Y)
//   open PATH            open a note or folder
//   back | home          navigation
//   ls [DIR]             list a directory
//   cat PATH             print a file
//   gen N PATH           write a large generated note (performance testing)
//   bt [scan|pair N|forget|raw on|raw off|on|off]   Bluetooth keyboard control/status
//   cal                  run touch calibration
//   mem                  print free heap
//   type TEXT            type text (\n = Enter, \t = Tab)
//   key [MOD+]NAME       press a key, e.g. "key ctrl+s", "key shift+left", "key enter"
//   kbd                  raw keyboard mode: terminal bytes become keys until Ctrl+]

namespace debug_console {
void poll();
}
