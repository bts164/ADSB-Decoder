// Names the ispc tasks in a Tracy trace (see common/tasksys.cpp: trace). Each task calls
// adsb_trace_kernel(<its ID>) as its first statement. In a build with Tracy that opens a zone named for the
// kernel inside the task's `omp task` zone, closed when the task returns; otherwise it does nothing.
//
// Included by the ispc kernels as well as by tasksys.cpp, so it holds only what both languages accept. A new
// task gets an ID here and a name in tasksys.cpp's kKernels.
#pragma once

#define ADSB_KERNEL_RESAMPLE_F32 0    // dsp/kernels.ispc: resample_task_f32
#define ADSB_KERNEL_RESAMPLE_I16BE 1  // dsp/kernels.ispc: resample_task_i16be
#define ADSB_KERNEL_PREAMBLE 2        // dsp/kernels.ispc: preamble_task
#define ADSB_KERNEL_COUNT 3

#ifdef ISPC
extern "C" void adsb_trace_kernel(uniform int kernel);
#else
extern "C" void adsb_trace_kernel(int kernel);
#endif
