/*
Copyright 2018 Embedded Microprocessor Benchmark Consortium (EEMBC)

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

Original Author: Shay Gal-on
*/
#include <sys/time.h>
#include "coremark.h"
#include "core_portme.h"
#include <metal/hpm.h>

#if VALIDATION_RUN
	volatile ee_s32 seed1_volatile=0x3415;
	volatile ee_s32 seed2_volatile=0x3415;
	volatile ee_s32 seed3_volatile=0x66;
#endif
#if PERFORMANCE_RUN
	volatile ee_s32 seed1_volatile=0x0;
	volatile ee_s32 seed2_volatile=0x0;
	volatile ee_s32 seed3_volatile=0x66;
#endif
#if PROFILE_RUN
	volatile ee_s32 seed1_volatile=0x8;
	volatile ee_s32 seed2_volatile=0x8;
	volatile ee_s32 seed3_volatile=0x8;
#endif
	volatile ee_s32 seed4_volatile=ITERATIONS;
	volatile ee_s32 seed5_volatile=0;

/* Porting : Timing functions
	How to capture time and convert to seconds must be ported to whatever is supported by the platform.
	e.g. Read value from on board RTC, read value from cpu clock cycles performance counter etc.
	Sample implementation for standard time.h and windows.h definitions included.
*/
CORETIMETYPE barebones_clock() {
  return clock();
}
/* Define : TIMER_RES_DIVIDER
	Divider to trade off timer resolution and total time that can be measured.

	Use lower values to increase resolution, but make sure that overflow does not occur.
	If there are issues with the return value overflowing, increase this value.
	*/
#define GETMYTIME(_t) (*_t=barebones_clock())
#define MYTIMEDIFF(fin,ini) ((fin)-(ini))
#define TIMER_RES_DIVIDER 1
#define SAMPLE_TIME_IMPLEMENTATION 1
#define EE_TICKS_PER_SEC (CLOCKS_PER_SEC / TIMER_RES_DIVIDER)

/** Define Host specific (POSIX), or target specific global time variables. */
static CORETIMETYPE start_time_val, stop_time_val;

// Default to basic version, where performance data is not automatically
// dumped from the hardware perfmon counters. A compile flag
// -DCOREMARK_ENABLE_PERFMON=1 will enable the perfmon support, or uncomment
// the following line.
// #define COREMARK_ENABLE_PERFMON 1
#ifdef COREMARK_ENABLE_PERFMON
struct metal_cpu *cpu;
int cycles_before;
int insts_before;
#endif

/* Function : start_time
	This function will be called right before starting the timed portion of the benchmark.

	Implementation may be capturing a system timer (as implemented in the example code)
	or zeroing some system parameters - e.g. setting the cpu clocks cycles to 0.
*/
void start_time(void) {
#ifdef COREMARK_ENABLE_PERFMON
  cpu = metal_cpu_get(metal_cpu_get_current_hartid());
  // This will both set up some things, and also clear all the counters.
  if (metal_hpm_init(cpu) != 0) {
    printf("ERROR: Could not initialize hpm hardware performance monitor system!\n");
    return;
  }

  // By default, count JAL (call) instructions on counter 3, and conditional
  // branches on counter 4, for testing purposes. Allow overrides via the
  // compilation flags.
#ifndef COREMARK_PERFMON_EVENT_SEL3
#define COREMARK_PERFMON_EVENT_SEL3 (METAL_HPM_EVENTID_15 | METAL_HPM_EVENTCLASS_0)
#endif

#ifndef COREMARK_PERFMON_EVENT_SEL4
#define COREMARK_PERFMON_EVENT_SEL4 (METAL_HPM_EVENTID_14 | METAL_HPM_EVENTCLASS_0)
#endif

  metal_hpm_set_event(cpu, METAL_HPM_COUNTER_3, COREMARK_PERFMON_EVENT_SEL3);
  metal_hpm_set_event(cpu, METAL_HPM_COUNTER_4, COREMARK_PERFMON_EVENT_SEL4);
#endif

	GETMYTIME(&start_time_val );

#ifdef COREMARK_ENABLE_PERFMON
  // Do this as the absolute last thing, because these are much faster than GETMYTIME().
  cycles_before = metal_hpm_read_counter(cpu, METAL_HPM_CYCLE);
  insts_before = metal_hpm_read_counter(cpu, METAL_HPM_INSTRET);
#endif
}
/* Function : stop_time
	This function will be called right after ending the timed portion of the benchmark.

	Implementation may be capturing a system timer (as implemented in the example code)
	or other system parameters - e.g. reading the current value of cpu cycles counter.
*/
void stop_time(void) {
#ifdef COREMARK_ENABLE_PERFMON
  // Grab the values for cycles and instructions from the free-running
  // counters, so we don't also count the time to do all these actions, since
  // those can't be frozen. This is very fast, so we do it before GETMYTIME(),
  // which is slower due to frequency adjustments etc.
  int cycles_after = metal_hpm_read_counter(cpu, METAL_HPM_CYCLE);
  int insts_after = metal_hpm_read_counter(cpu, METAL_HPM_INSTRET);
#endif

	GETMYTIME(&stop_time_val );

#ifdef COREMARK_ENABLE_PERFMON
  // First, stop all the counters by writing event 0 to them.
  // Some counts may be a bit inflated, due to them continuing to count during
  // the above code.
  for (int i = METAL_HPM_COUNTER_3; i <= METAL_HPM_COUNTER_4; i++) {
    metal_hpm_clr_event(cpu, i, 0xffffffff);
  }
  printf ("Counter %d holds %d (cycles) for a delta of %d\n", METAL_HPM_CYCLE,
      cycles_after, cycles_after - cycles_before);
  printf ("Counter %d holds %d (instret) for a delta of %d\n", METAL_HPM_INSTRET,
      insts_after, insts_after - insts_before);
  printf("Counter %d holds %d for event 0x%lx\n", METAL_HPM_COUNTER_3,
      metal_hpm_read_counter(cpu, METAL_HPM_COUNTER_3), COREMARK_PERFMON_EVENT_SEL3);
  printf("Counter %d holds %d for event 0x%lx\n", METAL_HPM_COUNTER_4,
      metal_hpm_read_counter(cpu, METAL_HPM_COUNTER_4), COREMARK_PERFMON_EVENT_SEL4);
#endif
}
/* Function : get_time
	Return an abstract "ticks" number that signifies time on the system.

	Actual value returned may be cpu cycles, milliseconds or any other value,
	as long as it can be converted to seconds by <time_in_secs>.
	This methodology is taken to accomodate any hardware or simulated platform.
	The sample implementation returns millisecs by default,
	and the resolution is controlled by <TIMER_RES_DIVIDER>
*/
CORE_TICKS get_time(void) {
	CORE_TICKS elapsed=(CORE_TICKS)(MYTIMEDIFF(stop_time_val, start_time_val));
	return elapsed;
}
/* Function : time_in_secs
	Convert the value returned by get_time to seconds.

	The <secs_ret> type is used to accomodate systems with no support for floating point.
	Default implementation implemented by the EE_TICKS_PER_SEC macro above.
*/
secs_ret time_in_secs(CORE_TICKS ticks) {
	secs_ret retval=((secs_ret)ticks) / (secs_ret)EE_TICKS_PER_SEC;
	return retval;
}

ee_u32 default_num_contexts=1;

/* Function : portable_init
	Target specific initialization code
	Test for some common mistakes.
*/
void portable_init(core_portable *p, int *argc, char *argv[])
{
	if (sizeof(ee_ptr_int) != sizeof(ee_u8 *)) {
		ee_printf("ERROR! Please define ee_ptr_int to a type that holds a pointer!\n");
	}
	if (sizeof(ee_u32) != 4) {
		ee_printf("ERROR! Please define ee_u32 to a 32b unsigned type!\n");
	}

	p->portable_id=1;
}
/* Function : portable_fini
	Target specific final code
*/
void portable_fini(core_portable *p)
{
	p->portable_id=0;
}


