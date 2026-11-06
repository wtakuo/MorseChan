// board.h - target board check
// MorseChan runs only on the M5Stack StackChan (CoreS3, ESP32-S3), built with
// FQBN m5stack:esp32:m5stack_cores3 (defines ARDUINO_M5STACK_CORES3)
#pragma once

#if !defined(ARDUINO_M5STACK_CORES3)
#error "Build for the StackChan (CoreS3): -b m5stack:esp32:m5stack_cores3"
#endif

namespace board {
constexpr const char* NAME = "StackChan (CoreS3)";
}
