/* Copyright 2020 HPS/SAFARI Research Groups
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/***************************************************************************************
* File         : main.c
* Author       : HPS Research Group
* Date         : 9/30/1997
* Description  :

\mainpage

Scarab is a detailed x86 multicore microarchitectural simulator. See
the [tutorial](../tutorial/tutorial.pdf) for general information on
how to check it out from version control, run experiments, summarize
the results, etc. This documentation deals with the source code of
Scarab.

Scarab's source code is organized as follows:

+ Infrastructure

  + Execution starting point
    + main.c

  + General helpful macros and functions
    + globals/assert.h
    + debug/debug_macros.h
    + globals/enum.h
    + globals/global_defs.h
    + globals/global_types.h
    + globals/utils.h

  + Data structure libraries
    + libs/hash_lib.h
    + libs/list_lib.h
    + libs/malloc_lib.h

  + Parameters and statistics
    + param_globals/enum.headers.h
    + param_parser.h
    + statistics.h

  + Misc
    + version.h

+ Functional model

  + General frontend/frontend.code
    + frontend/frontend.h
    + frontend/frontend_intf.h
    + isa/isa.h
    + isa/isa_macros.h

  + Multi2Sim
    + multi2sim.h
    + multi2sim directory

  + Pin trace
    + ctype_pin_inst.h
    + pin_inst.h
    + frontend/pin_trace_read.h
    + frontend/pin_trace_fe.h
    + gen_trace directory (trace generation)

  + Dependence maintenance
    + map.h

  + Thread
    + thread.h

+ Timing model

  + Top level architectural models
    + sim.h
    + model.h
    + cmp_model.h
    + cmp_model_support.h
    + dumb_model.h

  + Cores

    + Microinstruction data structures
      + op.h
      + op_info.h
      + op_pool.h
      + inst_info.h
      + table_info.h

    + Pipeline
      + stage_data.h
      + icache_stage.h
      + decode_stage.h
      + dcache_stage.h
      + map_stage.h
      + node_stage.h
      + exec_stage.h
      + dcache_stage.h

    + Branch prediction
      + bp/bp.h
      + bp/bp_conf.h
      + bp/gshare.h
      + bp/hybridgp.h
      + bp/tagescl.h
      + bp/bp_targ_mech.h
      + path_id.h

  + %Memory system

    + %Memory request data structures
      + memory/mem_req.h

    + Cache hierarchy
      + memory/memory.h

    + DRAM
      + dram.h
      + Schedulers
        + dram_sched.h
        + atlas.h
        + batch_sched.h
        + bw_sched.h
        + dram_batch.h
        + dram_bw_part.h
        + equal_use_sched.h
        + fair_queuing.h
        + pri_dram_sched.h
        + prob_pri_sched.h
        + tcm_sched.h

    + Prefetching

      + Common prefetching interface
        + prefetcher/pref_common.h
        + prefetcher/pref_2dc.h
        + prefetcher/pref_ghb.h
        + prefetcher/pref_markov.h
        + prefetcher/pref_phase.h
        + prefetcher//pref_stream.h
        + prefetcher//pref_stride.h
        + prefetcher//pref_stridepc.h
        + prefetcher/pref_type.h

      + Others
        + prefetcher/l2l1pref.h
        + prefetcher/l2markv_pref.h
        + prefetcher/l2way_pref.h
        + prefetcher/stream_pref.h

  + Global architecture code
    + addr_trans.h
    + freq.h
    + globals/global_vars.h
    + stat_mon.h
    + trigger.h

  + Studied features
    + dvfs/dvfs.h
    + dvfs/perf_pred.h
    + dvfs/power_pred.h

  + Visualization and debug output
    + debug/debug_print.h
    + debug/memview.h
    + debug/pipeview.h
    + stat_trace.h

  + Power models

    + Wattch
      + cacti_conflict.h
      + power_cache.h
      + power_event.h
      + power_modules.h

    + McPAT
      + power_intf.h

  + Architectural libraries
    + bus_lib.h
    + libs/cache_lib.h
    + libs/port_lib.h

***************************************************************************************/
#include <signal.h>
#include <unistd.h>
#include "globals/assert.h"
#include "globals/global_defs.h"
#include "globals/global_types.h"
#include "globals/global_vars.h"
#include "globals/utils.h"

#include "optimizer2.h"
#include "param_parser.h"
#include "sim.h"
#include "statistics.h"
#include "version.h"

#include "general.param.h"

/**************************************************************************************/

void* voided_global_starlab_ht_ptr = NULL;
void* voided_global_starlab_types_ht = NULL;
void* voided_address_to_type_ptr = NULL;
void* voided_address_to_prev_address = NULL;
void* voided_inst_truple_ptr = NULL;

void* voided_curr_inst_reg_reg_mov_ptr;
void* voided_curr_inst_mem_reg_mov_ptr;
void* voided_curr_inst_mem_mem_mov_ptr;

void* voided_prev_mem_mem_curr_reg_reg_ptr = NULL;
void* voided_prev_mem_mem_curr_reg_mem_ptr = NULL;
void* voided_prev_mem_mem_curr_mem_mem_ptr = NULL;
void* voided_prev_reg_reg_curr_reg_reg_ptr = NULL;
void* voided_prev_reg_reg_curr_reg_mem_ptr = NULL;
void* voided_prev_reg_reg_curr_mem_mem_ptr = NULL;
void* voided_prev_reg_mem_curr_reg_mem_ptr = NULL;
void* voided_prev_reg_mem_curr_mem_mem_ptr = NULL;
void* voided_prev_reg_mem_curr_reg_reg_ptr = NULL;

unsigned long long prev_instruction_time = 0;
char prev_instruction_class[128];
char prev_address_as_string[128];
unsigned long long starlab_prev_address = 0;

int main(int argc, char* argv[], char* envp[]) {
  char** simulated_argv;
  time_t cur_time;

  /* initialize some of my output streams to the standards */
  mystdout = stdout;
  mystderr = stderr;
  mystatus = NULL;

  /* print banner with revision info */
  fprintf(mystdout, "Scarab gitrev: %s\n", version());

  /* make sure all the variable sizes are what we expect */
  ASSERTU(0, sizeof(uns8) == 1);
  ASSERTU(0, sizeof(uns16) == 2);
  ASSERTU(0, sizeof(uns32) == 4);
  ASSERTU(0, sizeof(uns64) == 8);
  ASSERTU(0, sizeof(int8) == 1);
  ASSERTU(0, sizeof(int16) == 2);
  ASSERTU(0, sizeof(int32) == 4);
  ASSERTU(0, sizeof(int64) == 8);

  /* read parameters from PARAMS.in and the command line */
  simulated_argv = get_params(argc, argv);

  /* perform global initialization */
  init_global(simulated_argv, envp);

  /* print PID (sometimes useful for debugging) */
  if(PRINT_PID) {
    fprintf(stderr, "PID: %d\n", getpid());
    sleep(10);
  }

  /* set up signal handler for SIGINT */
  signal(SIGINT, handle_SIGINT);

  /* print startup messages */
  time(&cur_time);
  fprintf(mystdout, "Scarab started at %s\n", ctime(&cur_time));
  WRITE_STATUS("PID %d", getpid());
  WRITE_STATUS("STARTED");

  /* call the function for the type of simulation  */
  switch(SIM_MODE) {
    case UOP_SIM_MODE:
      uop_sim();
      break;
    case FULL_SIM_MODE:
      full_sim();
      break;
#ifdef ENABLE_PT_MEMTRACE
    case TRACE_BBV_MODE:
    case TRACE_BBV_DISTRIBUTED_MODE:
      extract_basic_block_vectors();
      break;
#endif
    default:
      FATAL_ERROR(0, "Unknown simulation mode.");
      break;
  }

  /* all done --- print finish messages */
  time(&cur_time);
  fprintf(mystdout, "Scarab finished at %s\n", ctime(&cur_time));
  WRITE_STATUS("FINISHED");

  close_output_streams();

  if(opt2_in_use())
    opt2_sim_complete();
  
  // Print what % of <MOV, MOV> were due to reg->reg, reg->mem, mem->reg, mem->mem, etc. moves
  // This can be obtained by looking at the curr_inst_reg_reg_mov_ptr, curr_inst_mem_reg_mov_ptr, etc. hash tables
  char **keys;
  void **values_array;

  KeyValuePair *key_value_pairs;
  long count = get_count(voided_global_starlab_types_ht);
  key_value_pairs = (KeyValuePair *)malloc(count * sizeof(KeyValuePair));

  starlab_return_key_value_arr(voided_global_starlab_types_ht, &keys, &values_array);

  for (long i = 0; i < count; i++) {
      key_value_pairs[i].key = keys[i];
      key_value_pairs[i].value = values_array[i];
  }

  qsort(key_value_pairs, count, sizeof(KeyValuePair), compare_key_value_pairs);

  unsigned long total_cc_count = 0;
  for (long i = 0; i < count; i++) {
      total_cc_count += *(unsigned long *)key_value_pairs[i].value;
  }

  unsigned long running_cc_count = 0;
  for (long i = 0; i < count; i++) {
      printf("inst tuple: %s, cumulative CCs: %.2f%%\n", key_value_pairs[i].key, ((double)*(unsigned long *)key_value_pairs[i].value / (double)total_cc_count) * 100);
      running_cc_count += *(unsigned long *)key_value_pairs[i].value;
      if (running_cc_count > ((total_cc_count * 99) / 100)) 
          break;
  }

  // Initialize cycle counters for each category
  unsigned long cc_prev_mem_mem_curr_reg_reg = 0;
  unsigned long cc_prev_mem_mem_curr_reg_mem = 0;
  unsigned long cc_prev_mem_mem_curr_mem_mem = 0;
  unsigned long cc_prev_reg_reg_curr_reg_reg = 0;
  unsigned long cc_prev_reg_reg_curr_reg_mem = 0;
  unsigned long cc_prev_reg_reg_curr_mem_mem = 0;
  unsigned long cc_prev_reg_mem_curr_reg_mem = 0;
  unsigned long cc_prev_reg_mem_curr_mem_mem = 0;
  unsigned long cc_prev_reg_mem_curr_reg_reg = 0;

  // Get the total counts for each category
  unsigned long total_count_prev_mem_mem_curr_reg_reg = get_count(voided_prev_mem_mem_curr_reg_reg_ptr);
  unsigned long total_count_prev_mem_mem_curr_reg_mem = get_count(voided_prev_mem_mem_curr_reg_mem_ptr);
  unsigned long total_count_prev_mem_mem_curr_mem_mem = get_count(voided_prev_mem_mem_curr_mem_mem_ptr);
  unsigned long total_count_prev_reg_reg_curr_reg_reg = get_count(voided_prev_reg_reg_curr_reg_reg_ptr);
  unsigned long total_count_prev_reg_reg_curr_reg_mem = get_count(voided_prev_reg_reg_curr_reg_mem_ptr);
  unsigned long total_count_prev_reg_reg_curr_mem_mem = get_count(voided_prev_reg_reg_curr_mem_mem_ptr);
  unsigned long total_count_prev_reg_mem_curr_reg_mem = get_count(voided_prev_reg_mem_curr_reg_mem_ptr);
  unsigned long total_count_prev_reg_mem_curr_mem_mem = get_count(voided_prev_reg_mem_curr_mem_mem_ptr);
  unsigned long total_count_prev_reg_mem_curr_reg_reg = get_count(voided_prev_reg_mem_curr_reg_reg_ptr);

  // print the counts of each category
  printf("Total counts:\n");
  printf("prev_mem_mem_curr_reg_reg: %lu\n", total_count_prev_mem_mem_curr_reg_reg);
  printf("prev_mem_mem_curr_reg_mem: %lu\n", total_count_prev_mem_mem_curr_reg_mem);
  printf("prev_mem_mem_curr_mem_mem: %lu\n", total_count_prev_mem_mem_curr_mem_mem);
  printf("prev_reg_reg_curr_reg_reg: %lu\n", total_count_prev_reg_reg_curr_reg_reg);
  printf("prev_reg_reg_curr_reg_mem: %lu\n", total_count_prev_reg_reg_curr_reg_mem);
  printf("prev_reg_reg_curr_mem_mem: %lu\n", total_count_prev_reg_reg_curr_mem_mem);
  printf("prev_reg_mem_curr_reg_mem: %lu\n", total_count_prev_reg_mem_curr_reg_mem);
  printf("prev_reg_mem_curr_mem_mem: %lu\n", total_count_prev_reg_mem_curr_mem_mem);
  printf("prev_reg_mem_curr_reg_reg: %lu\n", total_count_prev_reg_mem_curr_reg_reg);

  // get key value pairs from each category


  // prev: mem->mem, curr: reg->reg
  char **keys_prev_mem_mem_curr_reg_reg;
  void **values_prev_mem_mem_curr_reg_reg;

  KeyValuePair *key_value_pairs_prev_mem_mem_curr_reg_reg;
  long count_prev_mem_mem_curr_reg_reg = get_count(voided_prev_mem_mem_curr_reg_reg_ptr);
  key_value_pairs_prev_mem_mem_curr_reg_reg = (KeyValuePair *)malloc(count_prev_mem_mem_curr_reg_reg * sizeof(KeyValuePair));
  starlab_return_key_value_arr(voided_prev_mem_mem_curr_reg_reg_ptr, &keys_prev_mem_mem_curr_reg_reg, &values_prev_mem_mem_curr_reg_reg);

  for(long i = 0; i < count_prev_mem_mem_curr_reg_reg; i++) {
      if (keys_prev_mem_mem_curr_reg_reg[i] == NULL || values_prev_mem_mem_curr_reg_reg[i] == NULL) {
          fprintf(stderr, "Error: NULL key or value at index %ld for prev_mem_mem_curr_reg_reg\n", i);
      }
      key_value_pairs_prev_mem_mem_curr_reg_reg[i].key = keys_prev_mem_mem_curr_reg_reg[i];
      key_value_pairs_prev_mem_mem_curr_reg_reg[i].value = values_prev_mem_mem_curr_reg_reg[i];
  }

  // prev: mem->mem, curr: reg->mem
  char  **keys_prev_mem_mem_curr_reg_mem;
  void **values_prev_mem_mem_curr_reg_mem;

  KeyValuePair *key_value_pairs_prev_mem_mem_curr_reg_mem;
  long count_prev_mem_mem_curr_reg_mem = get_count(voided_prev_mem_mem_curr_reg_mem_ptr);
  key_value_pairs_prev_mem_mem_curr_reg_mem = (KeyValuePair *)malloc(count_prev_mem_mem_curr_reg_mem * sizeof(KeyValuePair));
  starlab_return_key_value_arr(voided_prev_mem_mem_curr_reg_mem_ptr, &keys_prev_mem_mem_curr_reg_mem, &values_prev_mem_mem_curr_reg_mem);

  for(long i = 0; i < count_prev_mem_mem_curr_reg_mem; i++) {
      if (keys_prev_mem_mem_curr_reg_mem[i] == NULL || values_prev_mem_mem_curr_reg_mem[i] == NULL) {
          fprintf(stderr, "Error: NULL key or value at index %ld for prev_mem_mem_curr_reg_mem\n", i);
      }
      key_value_pairs_prev_mem_mem_curr_reg_mem[i].key = keys_prev_mem_mem_curr_reg_mem[i];
      key_value_pairs_prev_mem_mem_curr_reg_mem[i].value = values_prev_mem_mem_curr_reg_mem[i];
  }

  // print the values of each category
  printf("Values for prev_mem_mem_curr_reg_reg:\n");
  for (long i = 0; i < count_prev_mem_mem_curr_reg_reg; i++) {
      printf("key: %s, value: %lu\n", key_value_pairs_prev_mem_mem_curr_reg_reg[i].key, *(unsigned long *)key_value_pairs_prev_mem_mem_curr_reg_reg[i].value);
      cc_prev_mem_mem_curr_reg_reg += *(unsigned long *)key_value_pairs_prev_mem_mem_curr_reg_reg[i].value;
  }

  // prev: mem->mem, curr: mem->mem
  char  **keys_prev_mem_mem_curr_mem_mem;
  void **values_prev_mem_mem_curr_mem_mem;

  KeyValuePair *key_value_pairs_prev_mem_mem_curr_mem_mem;
  long count_prev_mem_mem_curr_mem_mem = get_count(voided_prev_mem_mem_curr_mem_mem_ptr);
  key_value_pairs_prev_mem_mem_curr_mem_mem = (KeyValuePair *)malloc(count_prev_mem_mem_curr_mem_mem * sizeof(KeyValuePair));
  starlab_return_key_value_arr(voided_prev_mem_mem_curr_mem_mem_ptr, &keys_prev_mem_mem_curr_mem_mem, &values_prev_mem_mem_curr_mem_mem);

  for(long i = 0; i < count_prev_mem_mem_curr_mem_mem; i++) {
      if (keys_prev_mem_mem_curr_mem_mem[i] == NULL || values_prev_mem_mem_curr_mem_mem[i] == NULL) {
          fprintf(stderr, "Error: NULL key or value at index %ld for prev_mem_mem_curr_mem_mem\n", i);
      }
      key_value_pairs_prev_mem_mem_curr_mem_mem[i].key = keys_prev_mem_mem_curr_mem_mem[i];
      key_value_pairs_prev_mem_mem_curr_mem_mem[i].value = values_prev_mem_mem_curr_mem_mem[i];
  }

  // print the values of each category
  printf("Values for prev_mem_mem_curr_mem_mem:\n");
  for (long i = 0; i < count_prev_mem_mem_curr_mem_mem; i++) {
      printf("key: %s, value: %lu\n", key_value_pairs_prev_mem_mem_curr_mem_mem[i].key, *(unsigned long *)key_value_pairs_prev_mem_mem_curr_mem_mem[i].value);
      cc_prev_mem_mem_curr_mem_mem += *(unsigned long *)key_value_pairs_prev_mem_mem_curr_mem_mem[i].value;
  }

  // prev: reg->reg, curr: reg->reg
  char  **keys_prev_reg_reg_curr_reg_reg;
  void **values_prev_reg_reg_curr_reg_reg;

  KeyValuePair *key_value_pairs_prev_reg_reg_curr_reg_reg;
  long count_prev_reg_reg_curr_reg_reg = get_count(voided_prev_reg_reg_curr_reg_reg_ptr);
  key_value_pairs_prev_reg_reg_curr_reg_reg = (KeyValuePair *)malloc(count_prev_reg_reg_curr_reg_reg * sizeof(KeyValuePair));
  starlab_return_key_value_arr(voided_prev_reg_reg_curr_reg_reg_ptr, &keys_prev_reg_reg_curr_reg_reg, &values_prev_reg_reg_curr_reg_reg);

  for(long i = 0; i < count_prev_reg_reg_curr_reg_reg; i++) {
      if (keys_prev_reg_reg_curr_reg_reg[i] == NULL || values_prev_reg_reg_curr_reg_reg[i] == NULL) {
          fprintf(stderr, "Error: NULL key or value at index %ld for prev_reg_reg_curr_reg_reg\n", i);
      }
      key_value_pairs_prev_reg_reg_curr_reg_reg[i].key = keys_prev_reg_reg_curr_reg_reg[i];
      key_value_pairs_prev_reg_reg_curr_reg_reg[i].value = values_prev_reg_reg_curr_reg_reg[i];
  }

    // print the values of each category
  printf("Values for prev_reg_reg_curr_reg_reg:\n");
  for (long i = 0; i < count_prev_reg_reg_curr_reg_reg; i++) {
      printf("key: %s, value: %lu\n", key_value_pairs_prev_reg_reg_curr_reg_reg[i].key, *(unsigned long *)key_value_pairs_prev_reg_reg_curr_reg_reg[i].value);
      cc_prev_reg_reg_curr_reg_reg += *(unsigned long *)key_value_pairs_prev_reg_reg_curr_reg_reg[i].value;
  }

  // prev: reg->reg, curr: reg->mem
  char  **keys_prev_reg_reg_curr_reg_mem;
  void **values_prev_reg_reg_curr_reg_mem;

  KeyValuePair *key_value_pairs_prev_reg_reg_curr_reg_mem;
  long count_prev_reg_reg_curr_reg_mem = get_count(voided_prev_reg_reg_curr_reg_mem_ptr);
  key_value_pairs_prev_reg_reg_curr_reg_mem = (KeyValuePair *)malloc(count_prev_reg_reg_curr_reg_mem * sizeof(KeyValuePair));
  starlab_return_key_value_arr(voided_prev_reg_reg_curr_reg_mem_ptr, &keys_prev_reg_reg_curr_reg_mem, &values_prev_reg_reg_curr_reg_mem);

  for(long i = 0; i < count_prev_reg_reg_curr_reg_mem; i++) {
      if (keys_prev_reg_reg_curr_reg_mem[i] == NULL || values_prev_reg_reg_curr_reg_mem[i] == NULL) {
          fprintf(stderr, "Error: NULL key or value at index %ld for prev_reg_reg_curr_reg_mem\n", i);
      }
      key_value_pairs_prev_reg_reg_curr_reg_mem[i].key = keys_prev_reg_reg_curr_reg_mem[i];
      key_value_pairs_prev_reg_reg_curr_reg_mem[i].value = values_prev_reg_reg_curr_reg_mem[i];
  }

    // print the values of each category
  printf("Values for prev_reg_reg_curr_reg_mem:\n");
  for (long i = 0; i < count_prev_reg_reg_curr_reg_mem; i++) {
      printf("key: %s, value: %lu\n", key_value_pairs_prev_reg_reg_curr_reg_mem[i].key, *(unsigned long *)key_value_pairs_prev_reg_reg_curr_reg_mem[i].value);
      cc_prev_reg_reg_curr_reg_mem += *(unsigned long *)key_value_pairs_prev_reg_reg_curr_reg_mem[i].value;
  }

  // prev: reg->reg, curr: mem->mem
  char  **keys_prev_reg_reg_curr_mem_mem;
  void **values_prev_reg_reg_curr_mem_mem;

  KeyValuePair *key_value_pairs_prev_reg_reg_curr_mem_mem;
  long count_prev_reg_reg_curr_mem_mem = get_count(voided_prev_reg_reg_curr_mem_mem_ptr);
  key_value_pairs_prev_reg_reg_curr_mem_mem = (KeyValuePair *)malloc(count_prev_reg_reg_curr_mem_mem * sizeof(KeyValuePair));
  starlab_return_key_value_arr(voided_prev_reg_reg_curr_mem_mem_ptr, &keys_prev_reg_reg_curr_mem_mem, &values_prev_reg_reg_curr_mem_mem);

  for(long i = 0; i < count_prev_reg_reg_curr_mem_mem; i++) {
      if (keys_prev_reg_reg_curr_mem_mem[i] == NULL || values_prev_reg_reg_curr_mem_mem[i] == NULL) {
          fprintf(stderr, "Error: NULL key or value at index %ld for prev_reg_reg_curr_mem_mem\n", i);
      }
      key_value_pairs_prev_reg_reg_curr_mem_mem[i].key = keys_prev_reg_reg_curr_mem_mem[i];
      key_value_pairs_prev_reg_reg_curr_mem_mem[i].value = values_prev_reg_reg_curr_mem_mem[i];
  }

    // print the values of each category
  printf("Values for prev_reg_reg_curr_mem_mem:\n");
  for (long i = 0; i < count_prev_reg_reg_curr_mem_mem; i++) {
      printf("key: %s, value: %lu\n", key_value_pairs_prev_reg_reg_curr_mem_mem[i].key, *(unsigned long *)key_value_pairs_prev_reg_reg_curr_mem_mem[i].value);
      cc_prev_reg_reg_curr_mem_mem += *(unsigned long *)key_value_pairs_prev_reg_reg_curr_mem_mem[i].value;
  }

  // prev: reg->mem, curr: reg->mem
  char  **keys_prev_reg_mem_curr_reg_mem;
  void **values_prev_reg_mem_curr_reg_mem;

  KeyValuePair *key_value_pairs_prev_reg_mem_curr_reg_mem;
  long count_prev_reg_mem_curr_reg_mem = get_count(voided_prev_reg_mem_curr_reg_mem_ptr);
  key_value_pairs_prev_reg_mem_curr_reg_mem = (KeyValuePair *)malloc(count_prev_reg_mem_curr_reg_mem * sizeof(KeyValuePair));
  starlab_return_key_value_arr(voided_prev_reg_mem_curr_reg_mem_ptr, &keys_prev_reg_mem_curr_reg_mem, &values_prev_reg_mem_curr_reg_mem);

  for(long i = 0; i < count_prev_reg_mem_curr_reg_mem; i++) {
      if (keys_prev_reg_mem_curr_reg_mem[i] == NULL || values_prev_reg_mem_curr_reg_mem[i] == NULL) {
          fprintf(stderr, "Error: NULL key or value at index %ld for prev_reg_mem_curr_reg_mem\n", i);
      }
      key_value_pairs_prev_reg_mem_curr_reg_mem[i].key = keys_prev_reg_mem_curr_reg_mem[i];
      key_value_pairs_prev_reg_mem_curr_reg_mem[i].value = values_prev_reg_mem_curr_reg_mem[i];
  }

    // print the values of each category
  printf("Values for prev_reg_mem_curr_reg_mem:\n");
  for (long i = 0; i < count_prev_reg_mem_curr_reg_mem; i++) {
      printf("key: %s, value: %lu\n", key_value_pairs_prev_reg_mem_curr_reg_mem[i].key, *(unsigned long *)key_value_pairs_prev_reg_mem_curr_reg_mem[i].value);
      cc_prev_reg_mem_curr_reg_mem += *(unsigned long *)key_value_pairs_prev_reg_mem_curr_reg_mem[i].value;
  }

  // prev: reg->mem, curr: mem->mem
  char  **keys_prev_reg_mem_curr_mem_mem;
  void **values_prev_reg_mem_curr_mem_mem;

  KeyValuePair *key_value_pairs_prev_reg_mem_curr_mem_mem;
  long count_prev_reg_mem_curr_mem_mem = get_count(voided_prev_reg_mem_curr_mem_mem_ptr);
  key_value_pairs_prev_reg_mem_curr_mem_mem = (KeyValuePair *)malloc(count_prev_reg_mem_curr_mem_mem * sizeof(KeyValuePair));
  starlab_return_key_value_arr(voided_prev_reg_mem_curr_mem_mem_ptr, &keys_prev_reg_mem_curr_mem_mem, &values_prev_reg_mem_curr_mem_mem);

  for(long i = 0; i < count_prev_reg_mem_curr_mem_mem; i++) {
      if (keys_prev_reg_mem_curr_mem_mem[i] == NULL || values_prev_reg_mem_curr_mem_mem[i] == NULL) {
          fprintf(stderr, "Error: NULL key or value at index %ld for prev_reg_mem_curr_mem_mem\n", i);
      }
      key_value_pairs_prev_reg_mem_curr_mem_mem[i].key = keys_prev_reg_mem_curr_mem_mem[i];
      key_value_pairs_prev_reg_mem_curr_mem_mem[i].value = values_prev_reg_mem_curr_mem_mem[i];
  }

    // print the values of each category
  printf("Values for prev_reg_mem_curr_mem_mem:\n");
  for (long i = 0; i < count_prev_reg_mem_curr_mem_mem; i++) {
      printf("key: %s, value: %lu\n", key_value_pairs_prev_reg_mem_curr_mem_mem[i].key, *(unsigned long *)key_value_pairs_prev_reg_mem_curr_mem_mem[i].value);
      cc_prev_reg_mem_curr_mem_mem += *(unsigned long *)key_value_pairs_prev_reg_mem_curr_mem_mem[i].value;
  }

  // prev: reg->mem, curr: reg->reg
  char  **keys_prev_reg_mem_curr_reg_reg;
  void **values_prev_reg_mem_curr_reg_reg;

  KeyValuePair *key_value_pairs_prev_reg_mem_curr_reg_reg;
  long count_prev_reg_mem_curr_reg_reg = get_count(voided_prev_reg_mem_curr_reg_reg_ptr);
  key_value_pairs_prev_reg_mem_curr_reg_reg = (KeyValuePair *)malloc(count_prev_reg_mem_curr_reg_reg * sizeof(KeyValuePair));
  starlab_return_key_value_arr(voided_prev_reg_mem_curr_reg_reg_ptr, &keys_prev_reg_mem_curr_reg_reg, &values_prev_reg_mem_curr_reg_reg);

  for(long i = 0; i < count_prev_reg_mem_curr_reg_reg; i++) {
      if (keys_prev_reg_mem_curr_reg_reg[i] == NULL || values_prev_reg_mem_curr_reg_reg[i] == NULL) {
          fprintf(stderr, "Error: NULL key or value at index %ld for prev_reg_mem_curr_reg_reg\n", i);
      }
      key_value_pairs_prev_reg_mem_curr_reg_reg[i].key = keys_prev_reg_mem_curr_reg_reg[i];
      key_value_pairs_prev_reg_mem_curr_reg_reg[i].value = values_prev_reg_mem_curr_reg_reg[i];
  }

    // print the values of each category
  printf("Values for prev_reg_mem_curr_reg_reg:\n");
  for (long i = 0; i < count_prev_reg_mem_curr_reg_reg; i++) {
      printf("key: %s, value: %lu\n", key_value_pairs_prev_reg_mem_curr_reg_reg[i].key, *(unsigned long *)key_value_pairs_prev_reg_mem_curr_reg_reg[i].value);
      cc_prev_reg_mem_curr_reg_reg += *(unsigned long *)key_value_pairs_prev_reg_mem_curr_reg_reg[i].value;
  }


  // Print the final percentage
  printf("Final percentages:\n");
  printf("cc_prev_mem_mem_curr_reg_reg: %.2f%%\n", ((double)cc_prev_mem_mem_curr_reg_reg / (double)total_cc_count) * 100);
  printf("cc_prev_mem_mem_curr_reg_mem: %.2f%%\n", ((double)cc_prev_mem_mem_curr_reg_mem / (double)total_cc_count) * 100);
  printf("cc_prev_mem_mem_curr_mem_mem: %.2f%%\n", ((double)cc_prev_mem_mem_curr_mem_mem / (double)total_cc_count) * 100);
  printf("cc_prev_reg_reg_curr_reg_reg: %.2f%%\n", ((double)cc_prev_reg_reg_curr_reg_reg / (double)total_cc_count) * 100);
  printf("cc_prev_reg_reg_curr_reg_mem: %.2f%%\n", ((double)cc_prev_reg_reg_curr_reg_mem / (double)total_cc_count) * 100);
  printf("cc_prev_reg_reg_curr_mem_mem: %.2f%%\n", ((double)cc_prev_reg_reg_curr_mem_mem / (double)total_cc_count) * 100);
  printf("cc_prev_reg_mem_curr_reg_mem: %.2f%%\n", ((double)cc_prev_reg_mem_curr_reg_mem / (double)total_cc_count) * 100);
  printf("cc_prev_reg_mem_curr_mem_mem: %.2f%%\n", ((double)cc_prev_reg_mem_curr_mem_mem / (double)total_cc_count) * 100);
  printf("cc_prev_reg_mem_curr_reg_reg: %.2f%%\n", ((double)cc_prev_reg_mem_curr_reg_reg / (double)total_cc_count) * 100);


    free(key_value_pairs);
    return 0;
}