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
 * File         : globals/global_vars.h
 * Author       : HPS Research Group
 * Date         : 2/3/1998
 * Description  : This file is for global variable externs.
 ***************************************************************************************/

#ifndef __GLOBAL_VARS_H__
#define __GLOBAL_VARS_H__

/**************************************************************************************/

#include <stdio.h>
#include <stdbool.h>
#include "globals/global_types.h"
#include "statistics.h"

#include "libs/hash_lib.h"

/**************************************************************************************/

extern Counter  unique_count;
extern Counter* unique_count_per_core;
extern Counter* op_count;
extern Counter* inst_count;
extern Counter* inst_count_fetched;
extern Counter  cycle_count;
extern Counter  sim_time;
extern Counter* uop_count;
extern Counter* pret_inst_count;
extern uns      operating_mode;
extern Counter  pw_count;
extern Counter  unique_pws_since_recovery;

extern Counter* period_last_inst_count;
extern Counter  period_last_cycle_count;
extern Counter  period_ID;

extern Flag* warmup_dump_done;

extern Flag* trace_read_done;
extern Flag* reached_exit;
extern Flag* retired_exit;
extern Flag* sim_done;

extern FILE* mystderr;
extern FILE* mystdout;
extern FILE* mystatus;
extern int   mystatus_fd;

extern Flag frontend_gated;
extern uns  num_fetched_lowconf_brs;

extern Hash_Table per_branch_stat;
extern Uop_Queue_Fill_Time uop_queue_fill_time;

extern Flag roi_dump_began;
extern Counter roi_dump_ID;

extern void* voided_global_starlab_ht_ptr;
extern void* voided_global_starlab_types_ht;

extern void* voided_inst_truple_ptr;

extern void* voided_address_to_type_ptr;
extern void* voided_address_to_prev_address;

extern void* is_candidate_ptr;

// extern const char starlab_do_write;

extern unsigned long long prev_instruction_time;
extern char prev_instruction_class[128];

extern char prev_address_as_string[128];
extern unsigned long long starlab_prev_address;

extern unsigned long long consec_prev_instr_alu;
extern unsigned long long first_inst_in_trace;
extern unsigned long long consec_prev_instr_jump; 
extern bool is_first_inst;
extern unsigned long long consec_curr_instr; 
extern bool consec_is_alu; 

extern bool consec_icache_hit_prev_mov; 
extern unsigned long long mov_inst_icache_hit_prev_mov; 
extern char deep_curr_address_as_string[128]; 
extern char alu_address_as_string[128]; 
extern char next_addr_address_as_string[128]; 
extern bool consec_icache_hit_prev_alu;
extern bool consec_icache_hit_prev_jump;
extern unsigned long long jump_inst_from_prev_alu;
extern unsigned long long next_addr_after_alu_jump;
extern unsigned long long alu_inst_icache_hit_prev_alu; 
extern char jump_address_as_string[128]; 
extern void* voided_alu_jump_ht;
extern bool consec_prev_alu;
extern bool consec_prev_jump;
extern unsigned long long consec_next_inst;


/**************************************************************************************/

#endif /* #ifndef __GLOBAL_VARS_H__ */