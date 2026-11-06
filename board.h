// board.h - target board check
// MorseChan runs on the M5Stack StackChan (CoreS3 with the robot body) and on
// a CoreS3 / CoreS3 SE without the body (e.g. CoreS3 SE + Base M5GO Bottom3).
// All are built with FQBN m5stack:esp32:m5stack_cores3 (defines
// ARDUINO_M5STACK_CORES3); which one it is, is detected at run time
// (robot::begin, robot::board())
#pragma once

#if !defined(ARDUINO_M5STACK_CORES3)
#error "Build for CoreS3 / StackChan: -b m5stack:esp32:m5stack_cores3"
#endif
