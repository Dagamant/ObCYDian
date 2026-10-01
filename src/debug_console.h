#pragma once
// Line-based serial console for development:
//   shot                 dump the screen as raw RGB888 (see tools/screenshot.py)
//   tap X Y              inject a tap
//   drag X Y DY          inject a vertical drag of DY pixels starting at (X, Y)
//   open PATH            open a note or folder
//   back | home          navigation
//   ls [DIR]             list a directory
//   cal                  run touch calibration
//   mem                  print free heap

namespace debug_console {
void poll();
}
