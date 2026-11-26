/* Copyright (c) 2009 - http://ruinwesen.com/ */

#pragma once

#include <inttypes.h>
#include <helpers.h>

/** Represents a task that is executed at a regular interval. **/
class Task {
	/**
	 * \addtogroup helpers_task
	 * @{
	 **/

public:
  uint16_t interval;
  uint16_t lastExecution;
  bool starting;

  void (*taskFunction)();

	/** Create a task to be executed approximately every _interval ticks, calling the function _taskFunction. **/
  // constexpr: global tasks are constant-initialised (no start-up code).
  constexpr Task(uint16_t _interval, void (*_taskFunction)() = NULL)
      : interval(_interval), lastExecution(0), starting(true),
        taskFunction(_taskFunction) {}

  // Only for a global that is not constant-initialised (it starts zeroed in
  // .bss): sets `starting` and leaves the zero interval, clock and function
  // to .bss, so the global constructor stores nothing else.
  struct ZeroInit {};
  explicit Task(ZeroInit) : starting(true) {}

	/** Execute the task by calling the taskFunction. **/
  virtual void run() {
    if (taskFunction != NULL) {
      taskFunction();
    }
  }

	/** Check if the task needs to be executed. **/
  void checkTask();

  /** Remove the task, calling its cleanup code (empty for now). **/
  virtual void destroy() {
  }

#ifdef HOST_MIDIDUINO
  virtual ~Task() {
  }
#endif

	/* @} */
};

