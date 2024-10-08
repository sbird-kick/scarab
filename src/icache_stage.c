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
 * File         : icache_stage.c
 * Author       : HPS Research Group
 * Date         : 12/7/1998
 * Description  :
 ***************************************************************************************/

#include "debug/debug_macros.h"
#include "debug/debug_print.h"
#include "globals/assert.h"
#include "globals/global_defs.h"
#include "globals/global_types.h"
#include "globals/global_vars.h"
#include "globals/utils.h"

#include "bp/bp.h"
#include "icache_stage.h"
#include "map.h"
#include "op_pool.h"
#include "packet_build.h"
#include "thread.h"

#include "bp/bp.param.h"
#include "cmp_model.h"
#include "core.param.h"
#include "debug/debug.param.h"
#include "frontend/frontend.h"
#include "frontend/pin_trace_fe.h"
#include "memory/memory.h"
#include "memory/memory.param.h"
#include "prefetcher/l2l1pref.h"
#include "prefetcher/stream_pref.h"
#include "statistics.h"


/**************************************************************************************/
/* Macros */

#define DEBUG(proc_id, args...) _DEBUG(proc_id, DEBUG_ICACHE_STAGE, ##args)

#define STAGE_MAX_OP_COUNT ISSUE_WIDTH


#define MAX_INSTR_CLASS_SIZE 128
#define TUPLE_BUFFER_SIZE (2 * MAX_INSTR_CLASS_SIZE + 5)


/**************************************************************************************/
/* Global Variables */

Icache_Stage* ic = NULL;

extern Cmp_Model              cmp_model;
extern Memory*                mem;
extern Rob_Stall_Reason       rob_stall_reason;
extern Rob_Block_Issue_Reason rob_block_issue_reason;

/**************************************************************************************/
/* Local prototypes */

static inline Icache_State icache_issue_ops(Break_Reason*, uns*,
                                            Inst_Info** line);
static Inst_Info**         ic_pref_cache_access(void);
int32_t                    inst_lost_get_full_window_reason(void);

/**************************************************************************************/
/* set_icache_stage: */

void set_icache_stage(Icache_Stage* new_ic) {
  ic = new_ic;
}

/**************************************************************************************/
/* set_pb_data: */

void set_pb_data(Pb_Data* new_pb_data) {
  ic_pb_data = new_pb_data;
}

/**************************************************************************************/
/* init_icache_stage: */

void init_icache_stage(uns8 proc_id, const char* name) {
  ASSERT(0, ic);
  DEBUG(proc_id, "Initializing %s stage\n", name);

  memset(ic, 0, sizeof(Icache_Stage));

  ic->proc_id = proc_id;
  ic->sd.name = (char*)strdup(name);

  /* initialize the ops array */
  ic->sd.max_op_count = ISSUE_WIDTH;
  ic->sd.ops          = (Op**)malloc(sizeof(Op*) * ISSUE_WIDTH);

  /* initialize the cache structure */
  init_cache(&ic->icache, "ICACHE", ICACHE_SIZE, ICACHE_ASSOC, ICACHE_LINE_SIZE,
             0, REPL_TRUE_LRU);

  /* init icache_line_info struct - this struct keeps data about corresponding
   * icache lines */
  if(WP_COLLECT_STATS) {
    // init_cache(&ic->icache_line_info, "IC LI", ICACHE_SIZE, ICACHE_ASSOC,
    // ICACHE_LINE_SIZE,
    //           sizeof(Icache_Data), REPL_TRUE_LRU);
    init_cache(&ic->icache_line_info, "IC LI", ICACHE_SIZE, ICACHE_ASSOC,
               ICACHE_LINE_SIZE, sizeof(Icache_Data), ICACHE_REPL);
  }

  // moved the init code from here to reset
  reset_icache_stage();

  if(model->id != CMP_MODEL)
    ic_pb_data = (Pb_Data*)malloc(sizeof(Pb_Data));
  ic_pb_data->proc_id = proc_id;
  init_packet_build(ic_pb_data, PB_ICACHE);

  if(IC_PREF_CACHE_ENABLE)
    init_cache(&ic->pref_icache, "IC_PREF_CACHE", IC_PREF_CACHE_SIZE,
               IC_PREF_CACHE_ASSOC, ICACHE_LINE_SIZE, 0, REPL_TRUE_LRU);

  memset(ic->rand_wb_state, 0, NUM_ELEMENTS(ic->rand_wb_state));
}

/**************************************************************************************/
/* icache_init_trace:  */

void init_icache_trace() {
  ic->next_fetch_addr = frontend_next_fetch_addr(ic->proc_id);
  ASSERT_PROC_ID_IN_ADDR(ic->proc_id, ic->next_fetch_addr)
}

/**************************************************************************************/
/* reset_icache_stage: */

void reset_icache_stage() {
  uns ii;
  for(ii = 0; ii < ISSUE_WIDTH; ii++)
    ic->sd.ops[ii] = NULL;
  ic->sd.op_count = 0;

  /* set up the initial fetch state */
  ic->next_fetch_addr = td->inst_addr;
  ASSERT_PROC_ID_IN_ADDR(ic->proc_id, ic->next_fetch_addr);
  ic->off_path                       = FALSE;
  ic->back_on_path                   = FALSE;
  op_count[ic->proc_id]              = 1;
  unique_count_per_core[ic->proc_id] = 1;
}

/**************************************************************************************/
/* reset_all_ops_icache_stage: */
// CMP used for bogus run: may be combined with reset_icache_stage
void reset_all_ops_icache_stage() {
  uns ii;
  for(ii = 0; ii < ISSUE_WIDTH; ii++)
    ic->sd.ops[ii] = NULL;
  ic->sd.op_count = 0;

  /* set up the initial fetch state */
  ic->next_fetch_addr = td->inst_addr;
  ASSERT_PROC_ID_IN_ADDR(ic->proc_id, ic->next_fetch_addr);
  ic->off_path     = FALSE;
  ic->back_on_path = FALSE;
}

/**************************************************************************************/
/* recover_icache_stage: */

void recover_icache_stage() {
  Stage_Data* cur = &ic->sd;
  uns         ii;

  ASSERT(ic->proc_id, ic->proc_id == bp_recovery_info->proc_id);
  DEBUG(ic->proc_id,
        "Icache stage recovery signaled.  recovery_fetch_addr: 0x%s\n",
        hexstr64s(bp_recovery_info->recovery_fetch_addr));

  cur->op_count = 0;
  for(ii = 0; ii < ic->sd.max_op_count; ii++) {
    if(cur->ops[ii]) {
      if(FLUSH_OP(cur->ops[ii])) {
        free_op(cur->ops[ii]);
        cur->ops[ii] = NULL;
      } else {
        cur->op_count++;
      }
    }
  }

  ic->back_on_path = !bp_recovery_info->recovery_force_offpath;

  Op* op = bp_recovery_info->recovery_op;
  if(bp_recovery_info->late_bp_recovery && op->oracle_info.btb_miss &&
     !op->oracle_info.btb_miss_resolved) {
    // Late branch predictor recovered before btb miss is resolved (i.e., icache
    // stage should still wait for redirect)
  } else {
    if(ic->next_state != IC_FILL && ic->next_state != IC_WAIT_FOR_MISS) {
      ic->next_state = IC_FETCH;
    }
    if(SWITCH_IC_FETCH_ON_RECOVERY && model->id == CMP_MODEL) {
      ic->next_state = IC_FETCH;
    }
  }
  op_count[ic->proc_id] = bp_recovery_info->recovery_op_num + 1;
  ic->next_fetch_addr   = bp_recovery_info->recovery_fetch_addr;
  ASSERT(ic->proc_id, ic->next_fetch_addr);
}


/**************************************************************************************/
/* redirect_icache_stage: */

void redirect_icache_stage() {
  ASSERT(bp_recovery_info->proc_id, bp_recovery_info->proc_id == ic->proc_id);
  Op*  op                = bp_recovery_info->redirect_op;
  Addr next_fetch_addr   = op->oracle_info.pred_npc;
  op->redirect_scheduled = FALSE;

  DEBUG(ic->proc_id, "Icache stage redirect signaled. next_fetch_addr: 0x%s\n",
        hexstr64s(next_fetch_addr));
  ASSERT(ic->proc_id, ic->state == IC_WAIT_FOR_REDIRECT);

  Flag main_predictor_wrong = op->oracle_info.mispred ||
                              op->oracle_info.misfetch;
  Flag late_predictor_wrong = (USE_LATE_BP && (op->oracle_info.late_mispred ||
                                               op->oracle_info.late_misfetch));
  ic->back_on_path          = !(op->off_path || main_predictor_wrong ||
                       late_predictor_wrong);
  ic->next_fetch_addr       = next_fetch_addr;
  ASSERT_PROC_ID_IN_ADDR(ic->proc_id, ic->next_fetch_addr);
  ic->next_state = IC_FETCH;
}


/**************************************************************************************/
/* debug_icache_stage: */

void debug_icache_stage() {
  DPRINTF("# %-10s  op_count:%d ", ic->sd.name, ic->sd.op_count);
  DPRINTF(
    "fetch_addr:0x%s  next_fetch_addr:0x%s  path:%s  state:%s  next_state:%s\n",
    hexstr64s(ic->fetch_addr), hexstr64s(ic->next_fetch_addr),
    ic->off_path ? "OFF_PATH" : "ON_PATH ", icache_state_names[ic->state],
    icache_state_names[ic->next_state]);

  // print icache stage
  DPRINTF("# %-10s  op_count:%d\n", "ICache", ic->sd.op_count);
  print_op_array(GLOBAL_DEBUG_STREAM, ic->sd.ops, STAGE_MAX_OP_COUNT,
                 ic->sd.op_count);
}


/**************************************************************************************/
/* icache_cycle: */

void update_icache_stage() {
  uns          cf_num    = 0;
  Icache_Data* line_info = NULL;
  Addr         dummy_addr;

  STAT_EVENT(ic->proc_id, ICACHE_CYCLE);
  STAT_EVENT(ic->proc_id, ICACHE_CYCLE_ONPATH + ic->off_path);
  INC_STAT_EVENT(ic->proc_id, INST_LOST_TOTAL, ISSUE_WIDTH);

  ic->state = ic->next_state;

  if(ic->sd.op_count) {
    STAT_EVENT(ic->proc_id, FETCH_0_OPS);
    INC_STAT_EVENT(ic->proc_id,
                   INST_LOST_FULL_WINDOW + inst_lost_get_full_window_reason(),
                   ISSUE_WIDTH);
    return;
  }

  switch(ic->state) {
    case IC_FETCH: {
      Break_Reason break_fetch = BREAK_DONT;

      ic->off_path &= !ic->back_on_path;
      ic->back_on_path = FALSE;

      if(!FETCH_OFF_PATH_OPS && ic->off_path)
        return;

      STAT_EVENT(ic->proc_id, FETCH_ON_PATH + ic->off_path);

      reset_packet_build(ic_pb_data);  // reset packet build counters

      while(!break_fetch) {
        ic->fetch_addr = ic->next_fetch_addr;
        ASSERT_PROC_ID_IN_ADDR(ic->proc_id, ic->fetch_addr)

        if(ic->proc_id)
          ASSERTM(ic->proc_id, ic->fetch_addr, "ic fetch addr: %llu\n",
                  ic->fetch_addr);

        ic->line = (Inst_Info**)cache_access(&ic->icache, ic->fetch_addr,
                                             &ic->line_addr, TRUE);

        if(PERFECT_ICACHE && !ic->line)
          ic->line = INIT_CACHE_DATA_VALUE;

        if(WP_COLLECT_STATS)  // CMP remove?
          line_info = (Icache_Data*)cache_access(
            &ic->icache_line_info, ic->fetch_addr, &dummy_addr, TRUE);


        if(IC_PREF_CACHE_ENABLE && (!ic->line))
          ic->line = ic_pref_cache_access();


        STAT_EVENT(ic->proc_id, POWER_ITLB_ACCESS);
        STAT_EVENT(ic->proc_id, POWER_ICACHE_ACCESS);
        STAT_EVENT(ic->proc_id, POWER_BTB_READ);

        // ideal L2 Icache prefetcher
        if(IDEAL_L2_ICACHE_PREFETCHER) {
          Addr     line_addr;
          L1_Data* data;
          Cache*   l1_cache = model->mem == MODEL_MEM ?
                              &mem->uncores[ic->proc_id].l1->cache :
                              NULL;
          data = l1_cache ? (L1_Data*)cache_access(l1_cache, ic->fetch_addr,
                                                   &line_addr, TRUE) :
                            NULL;
          if(data) {  // second level cache hit
            STAT_EVENT(ic->proc_id, L2_IDEAL_FILL_ICACHE);
          } else
            STAT_EVENT(ic->proc_id, L2_IDEAL_MISS_ICACHE);
        }

        if(ic->line == NULL) { /* cache miss */
          DEBUG(ic->proc_id, "Cache miss on op_num:%s @ 0x%s\n",
                unsstr64(op_count[ic->proc_id]), hexstr64s(ic->fetch_addr));

          STAT_EVENT(ic->proc_id, ICACHE_MISS);
          STAT_EVENT(ic->proc_id, POWER_ICACHE_MISS);
          STAT_EVENT(ic->proc_id, ICACHE_MISS_ONPATH + ic->off_path);

          /* if the icache is available, wait for a miss */
          /* otherwise, refetch next cycle */
          if(model->mem == MODEL_MEM) {
            if(ic->proc_id)
              ASSERTM(ic->proc_id, ic->line_addr, "ic fetch addr: %llu\n",
                      ic->fetch_addr);

            ASSERT_PROC_ID_IN_ADDR(ic->proc_id, ic->line_addr)
            if(new_mem_req(MRT_IFETCH, ic->proc_id, ic->line_addr,
                           ICACHE_LINE_SIZE, 0, NULL, icache_fill_line,
                           unique_count,
                           0)) {  // CMP maybe unique_count_per_core[proc_id]?
              ic->next_state = IC_WAIT_FOR_MISS;

              if(ONE_MORE_CACHE_LINE_ENABLE) {
                Addr         one_more_addr;
                Addr         extra_line_addr;
                Icache_Data* extra_line;

                one_more_addr =
                  ((ic->line_addr >> LOG2(ICACHE_LINE_SIZE)) & 1) ?
                    ((ic->line_addr >> LOG2(ICACHE_LINE_SIZE)) - 1)
                      << LOG2(ICACHE_LINE_SIZE) :
                    ((ic->line_addr >> LOG2(ICACHE_LINE_SIZE)) + 1)
                      << LOG2(ICACHE_LINE_SIZE);

                extra_line = (Icache_Data*)cache_access(
                  &ic->icache, one_more_addr, &extra_line_addr, FALSE);
                ASSERT(ic->proc_id, one_more_addr == extra_line_addr);
                if(!extra_line) {
                  if(new_mem_req(MRT_IFETCH, ic->proc_id, extra_line_addr,
                                 ICACHE_LINE_SIZE, 0, NULL, NULL, unique_count,
                                 0))
                    STAT_EVENT_ALL(ONE_MORE_SUCESS);
                  else
                    STAT_EVENT_ALL(ONE_MORE_DISCARDED_MEM_REQ_FULL);
                } else
                  STAT_EVENT_ALL(ONE_MORE_DISCARDED_L0CACHE);
              }
            }
          }
          break_fetch = BREAK_ICACHE_MISS;
        } else { /* cache hit */
          DEBUG(ic->proc_id, "Cache hit on op_num:%s @ 0x%s \n",
                unsstr64(op_count[ic->proc_id]), hexstr64s(ic->fetch_addr));
          STAT_EVENT(ic->proc_id, ICACHE_HIT);
          STAT_EVENT(ic->proc_id, ICACHE_HIT_ONPATH + ic->off_path);
          if(WP_COLLECT_STATS) {
            ASSERT(ic->proc_id, line_info);
            wp_process_icache_hit(line_info, ic->fetch_addr);
          }
          ic->next_state = icache_issue_ops(&break_fetch, &cf_num, ic->line);
        }
      }
      INC_STAT_EVENT(ic->proc_id, INST_LOST_BREAK_DONT + break_fetch,
                     ISSUE_WIDTH - ic->sd.op_count);
      STAT_EVENT(ic->proc_id, FETCH_0_OPS + ic->sd.op_count);
      STAT_EVENT(ic->proc_id, ST_BREAK_DONT + break_fetch);
    } break;

    case IC_WAIT_FOR_MISS: {
      INC_STAT_EVENT(ic->proc_id, INST_LOST_BREAK_ICACHE_MISS, ISSUE_WIDTH);
      STAT_EVENT(ic->proc_id, FETCH_0_OPS);
    } break;

    case IC_WAIT_FOR_REDIRECT: {
      INC_STAT_EVENT(ic->proc_id, INST_LOST_WAIT_FOR_REDIRECT, ISSUE_WIDTH);
      STAT_EVENT(ic->proc_id, FETCH_0_OPS);
    } break;

    case IC_WAIT_FOR_EMPTY_ROB: {
      DEBUG(ic->proc_id, "Ifetch barrier: Waiting for ROB to become empty \n");
      INC_STAT_EVENT(ic->proc_id, INST_LOST_WAIT_FOR_EMPTY_ROB, ISSUE_WIDTH);
      STAT_EVENT(ic->proc_id, FETCH_0_OPS);
      if(td->seq_op_list.count == 0)
        ic->next_state = IC_FETCH;
    } break;

    case IC_WAIT_FOR_TIMER: {
      INC_STAT_EVENT(ic->proc_id, INST_LOST_WAIT_FOR_TIMER, ISSUE_WIDTH);
      STAT_EVENT(ic->proc_id, FETCH_0_OPS);
      if(cycle_count >= ic->timer_cycle)
        ic->next_state = IC_FETCH;
    } break;

    default:
      FATAL_ERROR(ic->proc_id, "Invalid icache state.\n");
  }
}


/**************************************************************************************/
/* icache_issue_ops: On a cache hit, select ops to pass down to the decode
   stage.  Each op that gets issued is executed by the oracle. It will only
   process up to the first control flow operation and then return.  If
   FETCH_ACROSS_CACHE_LINES is set, it will be called again until break_fetch
   becomes true. */

static inline Icache_State icache_issue_ops(Break_Reason* break_fetch,
                                            uns* cf_num, Inst_Info** line) {
  Packet_Build_Condition packet_break = PB_BREAK_DONT;
  static uns last_icache_issue_time = 0; /* for computing fetch break latency */
  static Counter issued_real_inst   = 0;
  static Counter issued_uop         = 0;
  uns            fetch_lag;

  ASSERT(ic->proc_id, ic->proc_id == td->proc_id);

  fetch_lag              = cycle_count - last_icache_issue_time;
  last_icache_issue_time = cycle_count;

  while(1) {
    Op*        op   = alloc_op(ic->proc_id);
    Inst_Info* inst = 0;
    UNUSED(inst);

    if(frontend_can_fetch_op(ic->proc_id)) {
      frontend_fetch_op(ic->proc_id, op);
      ASSERTM(ic->proc_id, ic->next_fetch_addr == op->inst_info->addr,
              "Fetch address 0x%llx does not match op address 0x%llx\n",
              ic->next_fetch_addr, op->inst_info->addr);
      op->fetch_addr = ic->next_fetch_addr;
      ASSERT_PROC_ID_IN_ADDR(ic->proc_id, op->fetch_addr)
      op->off_path  = ic->off_path;
      td->inst_addr = op->inst_info->addr;  // FIXME: BUG 54
      ASSERT_PROC_ID_IN_ADDR(ic->proc_id, td->inst_addr);
      if(!op->off_path) {
        if(op->eom)
          issued_real_inst++;
        issued_uop++;
      }
      inst = op->inst_info;
    } else {
      free_op(op);
      *break_fetch = BREAK_BARRIER;
      return IC_FETCH;
    }

    if(!op->off_path &&
       (op->table_info->mem_type == MEM_LD ||
        op->table_info->mem_type == MEM_ST) &&
       op->oracle_info.va == 0) {
      // don't care if the va is 0x0 if mem_type is MEM_PF(SW prefetch),
      // MEM_WH(write hint), or MEM_EVICT(cache block eviction hint)
      print_func_op(op);
      FATAL_ERROR(ic->proc_id, "Access to 0x0\n");
    }

    if(DUMP_TRACE && DEBUG_RANGE_COND(ic->proc_id))
      print_func_op(op);

    if(DIE_ON_CALLSYS && !op->off_path) {
      ASSERT(ic->proc_id, op->table_info->cf_type != CF_SYS);
    }

    packet_break = packet_build(ic_pb_data, break_fetch, op, 0);
    if(packet_break == PB_BREAK_BEFORE) {
      free_op(op);
      break;
    }

    /* add to sequential op list */
    add_to_seq_op_list(td, op);

    ASSERT(ic->proc_id, td->seq_op_list.count <= op_pool_active_ops);

    /* map the op based on true dependencies & set information in
     * op->oracle_info */
    /* num cycles since last group issued */
    op->fetch_lag = fetch_lag;

    thread_map_op(op);

    STAT_EVENT(op->proc_id, FETCH_ALL_INST);
    STAT_EVENT(op->proc_id, ORACLE_ON_PATH_INST + op->off_path);
    STAT_EVENT(op->proc_id, ORACLE_ON_PATH_INST_MEM +
                              (op->table_info->mem_type == NOT_MEM) +
                              2 * op->off_path);

    thread_map_mem_dep(op);
    op->fetch_cycle = cycle_count;

    // printf("[%016llX] fetch cycle: %lld with OP_TYPE: %s\n", op->fetch_addr, op->fetch_cycle, starlab_get_opcode_string(op->table_info->op_type));

    static char prev_fetch_addr_str[128] = {0};
    static char prev_instr_optype[128] = {0};
    static unsigned long prev_macro_inst_fetch_cycle = 0;
    static unsigned long prev_macro_inst_exec_cycle = 0;
    static unsigned long prev_inst_prev_fetch = 0;

    static char current_fetch_addr_str[128] = {0};
    static char curr_instr_optype[128] = {0};
    static unsigned long curr_macro_inst_fetch_cycle = 0;
    static unsigned long curr_macro_inst_exec_cycle = 0;
    static int count_second_inst = 0;
    char tuple_of_types[TUPLE_BUFFER_SIZE] = {0};
    unsigned long cc_taken_by_tuple = 0;


    starlab_hash_table* starlab_types_table_ptr = (starlab_hash_table*) voided_global_starlab_types_ht;
    if (starlab_types_table_ptr == NULL)
    {
        starlab_types_table_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(unsigned long));
    }

    starlab_hash_table* voided_global_starlab_ht_ptr = (starlab_hash_table*) voided_global_starlab_ht;
    if (voided_global_starlab_ht_ptr == NULL) 
    {
        voided_global_starlab_ht_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(starlab_table_value));
    }

    starlab_hash_table* macro_inst_ht = (starlab_hash_table*) voided_macro_inst_ht;

    sprintf(current_fetch_addr_str, "%016lX", (unsigned long)op->fetch_addr);
    int* macro_inst_op_type_ptr = (int*) starlab_search(macro_inst_ht, current_fetch_addr_str);
    int macro_inst_op_type = *macro_inst_op_type_ptr;
    sprintf(curr_instr_optype, "%s", starlab_get_opcode_string(macro_inst_op_type));

    // First instruction in the trace
    if (prev_fetch_addr_str[0] == '\0') 
    {
      curr_macro_inst_fetch_cycle = op->fetch_cycle;
      curr_macro_inst_exec_cycle = op->exec_cycle;

      sprintf(prev_fetch_addr_str, "%016lX", (unsigned long)op->fetch_addr);
      sprintf(prev_instr_optype, "%s", curr_instr_optype);
      prev_macro_inst_fetch_cycle = op->fetch_cycle;

      starlab_table_value temp_val_to_insert;
      temp_val_to_insert.prev_fetch = 0;
      temp_val_to_insert.this_fetch = op->fetch_cycle;
      starlab_insert(voided_global_starlab_ht_ptr, current_fetch_addr_str, &temp_val_to_insert);

      // Deepanjali Debugging
      unsigned long this_fetch = temp_val_to_insert.this_fetch;
      unsigned long prev_fetch = temp_val_to_insert.prev_fetch;
      printf("Icache: Instruction Address: %s, Previous Fetch Cycle: %lu, This Fetch Cycle: %lu\n", current_fetch_addr_str, prev_fetch, this_fetch);
      
    } 

    else 
    {
        if (strcmp(prev_fetch_addr_str, current_fetch_addr_str) == 0) // Same macro instruction
        {  
            curr_macro_inst_fetch_cycle = op->fetch_cycle;
            curr_macro_inst_exec_cycle = op->exec_cycle;

            // Don't do anything
        } 
        else // Different macro instruction
        {  
            sprintf(curr_instr_optype, "%s", starlab_get_opcode_string(macro_inst_op_type));
            curr_macro_inst_fetch_cycle = op->fetch_cycle;
            curr_macro_inst_exec_cycle = op->exec_cycle;

            if (count_second_inst == 0) // Second instruction (no previous instance)
            {  
                starlab_table_value temp_val_to_insert;
                temp_val_to_insert.prev_fetch = 0;
                temp_val_to_insert.this_fetch = op->fetch_cycle;
                starlab_insert(voided_global_starlab_ht_ptr, current_fetch_addr_str, &temp_val_to_insert);
                count_second_inst++;
                cc_taken_by_tuple = curr_macro_inst_fetch_cycle;

                // Deepanjali Debugging
                unsigned long this_fetch = temp_val_to_insert.this_fetch;
                unsigned long prev_fetch = temp_val_to_insert.prev_fetch;
                printf("Icache: Second Instruction Address: %s, Previous Fetch Cycle: %lu, This Fetch Cycle: %lu\n", current_fetch_addr_str, prev_fetch, this_fetch);
            } 
            
            else // Beyond the second instruction
            {  
                starlab_table_value* temp_val = (starlab_table_value*) starlab_search(voided_global_starlab_ht_ptr, current_fetch_addr_str);
                if (temp_val == NULL) // No previous instance
                {  
                    starlab_table_value temp_val_to_insert;
                    temp_val_to_insert.prev_fetch = 0;
                    temp_val_to_insert.this_fetch = op->fetch_cycle;
                    starlab_insert(voided_global_starlab_ht_ptr, current_fetch_addr_str, &temp_val_to_insert);

                    prev_inst_prev_fetch = ((starlab_table_value*) starlab_search(voided_global_starlab_ht_ptr, prev_fetch_addr_str))->prev_fetch;
                    cc_taken_by_tuple = (prev_inst_prev_fetch == 0) ? curr_macro_inst_fetch_cycle : curr_macro_inst_fetch_cycle - prev_inst_prev_fetch;

                    // Deepanjali Debugging
                    printf("Does not have a previous instance\n");
                    unsigned long this_fetch = temp_val_to_insert.this_fetch;
                    unsigned long prev_fetch = temp_val_to_insert.prev_fetch;
                    printf("Instruction Address: %s, Previous Fetch Cycle: %lu, This Fetch Cycle: %lu\n", current_fetch_addr_str, prev_fetch, this_fetch);
                } 
            
            
                else // Has a previous instance
                {  
                    unsigned long this_fetch_value = temp_val->this_fetch;
                    starlab_table_value temp_val_to_insert;
                    temp_val_to_insert.prev_fetch = this_fetch_value;
                    temp_val_to_insert.this_fetch = op->fetch_cycle;
                    starlab_insert(voided_global_starlab_ht_ptr, current_fetch_addr_str, &temp_val_to_insert);

                    prev_inst_prev_fetch = ((starlab_table_value*) starlab_search(voided_global_starlab_ht_ptr, prev_fetch_addr_str))->prev_fetch;
                    cc_taken_by_tuple = (prev_inst_prev_fetch == 0) ? curr_macro_inst_fetch_cycle : curr_macro_inst_fetch_cycle - prev_inst_prev_fetch;

                    // Deepanjali Debugging
                    printf("Has a previous instance\n");
                    unsigned long this_fetch = temp_val_to_insert.this_fetch;
                    unsigned long prev_fetch = temp_val_to_insert.prev_fetch;
                    printf("Instruction Address: %s, Previous Fetch Cycle: %lu, This Fetch Cycle: %lu\n", current_fetch_addr_str, prev_fetch, this_fetch);
                    printf("Previous Instruction: %s, Previous Fetch Cycle: %lu\n", prev_fetch_addr_str, prev_inst_prev_fetch);
                    printf("CC taken by tuple: %lu - %lu = %lu\n", curr_macro_inst_fetch_cycle, prev_inst_prev_fetch, cc_taken_by_tuple);
                }
           }
        
       
            snprintf(tuple_of_types, sizeof(tuple_of_types), "<%s,%s>", prev_instr_optype, curr_instr_optype);
            printf("[DEBUG] Generated tuple_of_types: <%s,%s>\n", prev_instr_optype, curr_instr_optype);

            if (!starlab_search(starlab_types_table_ptr, tuple_of_types)) 
            {
                printf("[DEBUG] Tuple <%s> not found in starlab_types_table_ptr. Inserting with initial value: %lu\n", tuple_of_types, cc_taken_by_tuple);

                unsigned long insert_val = cc_taken_by_tuple;
                starlab_insert(starlab_types_table_ptr, tuple_of_types, &insert_val);
                printf("[DEBUG] Inserted tuple <%s> with value %lu into starlab_types_table_ptr\n", tuple_of_types, insert_val);
            } 
            else 
            {
                unsigned long *current_val_ptr = (unsigned long *)starlab_search(starlab_types_table_ptr, tuple_of_types);
                unsigned long updated_val = *current_val_ptr + cc_taken_by_tuple;
                printf("[DEBUG] Tuple <%s> found in starlab_types_table_ptr with current value %lu. Updating with new value: %lu\n", tuple_of_types, *current_val_ptr, updated_val);

                starlab_insert(starlab_types_table_ptr, tuple_of_types, &updated_val);
                printf("[DEBUG] Updated tuple <%s> in starlab_types_table_ptr with new value %lu\n", tuple_of_types, updated_val);
            }

          printf("\n\n");

      
    }
        strncpy(prev_fetch_addr_str, current_fetch_addr_str, 128);
        sprintf(prev_instr_optype, "%s", curr_instr_optype);
        prev_macro_inst_fetch_cycle = curr_macro_inst_fetch_cycle;
        prev_macro_inst_exec_cycle = curr_macro_inst_exec_cycle;
}

    // Update global pointers
    voided_macro_inst_ht = (void*)macro_inst_ht;
    voided_global_starlab_types_ht = (void*)starlab_types_table_ptr;
    voided_global_starlab_ht = (void*)voided_global_starlab_ht_ptr;

    ic->sd.ops[ic->sd.op_count] = op; /* put op in the exit list */
    op_count[ic->proc_id]++;          /* increment instruction counters */
    unique_count_per_core[ic->proc_id]++;
    unique_count++;

    /* check trigger */
    if(op->inst_info->trigger_op_fetched_hook)
      model->op_fetched_hook(op);

    /* move on to next instruction in the cache line */
    ic->sd.op_count++;
    INC_STAT_EVENT(ic->proc_id, INST_LOST_FETCH + ic->off_path, 1);

    DEBUG(ic->proc_id,
          "Fetching op from Icache addr: %s off: %d inst_info: %p ii_addr: %s "
          "dis: %s opnum: (%s:%s)\n",
          hexstr64s(op->inst_info->addr), op->off_path, op->inst_info,
          hexstr64s(op->inst_info->addr), disasm_op(op, TRUE),
          unsstr64(op->op_num), unsstr64(op->unique_num));

    /* figure out next address after current instruction */
    if(op->table_info->cf_type) {
      // For pipeline gating
      if(op->table_info->cf_type == CF_CBR)
        td->td_info.fetch_br_count++;

      if(*break_fetch == BREAK_BARRIER) {
        // for fetch barriers (including syscalls), we do not want to do
        // redirect/recovery, BUT we still want to update the branch predictor.
        bp_predict_op(g_bp_data, op, (*cf_num)++, ic->fetch_addr);
        op->oracle_info.mispred   = 0;
        op->oracle_info.misfetch  = 0;
        op->oracle_info.btb_miss  = 0;
        op->oracle_info.no_target = 0;
        ic->next_fetch_addr       = ADDR_PLUS_OFFSET(
          ic->next_fetch_addr, op->inst_info->trace_info.inst_size);
        ASSERT_PROC_ID_IN_ADDR(ic->proc_id, ic->next_fetch_addr)
      } else {
        //
        ic->next_fetch_addr = bp_predict_op(g_bp_data, op, (*cf_num)++,
                                            ic->fetch_addr);
        // initially bp_predict_op can return a garbage, for multi core run,
        // addr must follow cmp addr convention
        ic->next_fetch_addr = convert_to_cmp_addr(ic->proc_id,
                                                  ic->next_fetch_addr);
        ASSERT_PROC_ID_IN_ADDR(ic->proc_id, ic->next_fetch_addr)
      }

      ASSERT(ic->proc_id,
             (op->oracle_info.mispred << 2 | op->oracle_info.misfetch << 1 |
              op->oracle_info.btb_miss) <= 0x7);

      const uns8 mispred       = op->oracle_info.mispred;
      const uns8 late_mispred  = op->oracle_info.late_mispred;
      const uns8 misfetch      = op->oracle_info.misfetch;
      const uns8 late_misfetch = op->oracle_info.late_misfetch;

      /* if it's a mispredict, kick the oracle off path */
      if(mispred || misfetch ||
         (USE_LATE_BP && (late_mispred || late_misfetch))) {
        ic->off_path = TRUE;

        if(FETCH_OFF_PATH_OPS) {
          if(mispred || misfetch) {
            DEBUG(ic->proc_id,
                  "Cycle %llu: redirected frontend because of the "
                  "early branch predictor to 0x%s\n",
                  cycle_count, hexstr64s(ic->next_fetch_addr));
            frontend_redirect(td->proc_id, op->inst_uid, ic->next_fetch_addr);
          }

          if(USE_LATE_BP) {
            if((mispred || misfetch) && !late_mispred && !late_misfetch) {
              bp_sched_recovery(bp_recovery_info, op, cycle_count,
                                /*late_bp_recovery=*/TRUE,
                                /*force_offpath=*/FALSE);
              DEBUG(ic->proc_id,
                    "Scheduled a recovery to correct addr for cycle %llu\n",
                    cycle_count + LATE_BP_LATENCY);
            } else if((late_mispred || late_misfetch) &&
                      op->oracle_info.pred_npc !=
                        op->oracle_info.late_pred_npc) {
              bp_sched_recovery(bp_recovery_info, op, cycle_count,
                                /*late_bp_recovery=*/TRUE,
                                /*force_offpath=*/TRUE);
              DEBUG(ic->proc_id,
                    "Scheduled a recovery to wrong addr for cycle %llu\n",
                    cycle_count + LATE_BP_LATENCY);
            }
          }
        } else {
          packet_break = PB_BREAK_AFTER;
          *break_fetch = BREAK_OFFPATH;
        }

        // pipeline gating
        if(!op->off_path)
          td->td_info.last_bp_miss_op = op;
        ///////////////////////////////////////
      }


      /* if it's a btb miss, quit fetching and wait for redirect */
      if(op->oracle_info.btb_miss) {
        *break_fetch = BREAK_BTB_MISS;
        DEBUG(ic->proc_id, "Changed icache to wait for redirect %llu\n",
              cycle_count);
        return IC_WAIT_FOR_REDIRECT;
      }

      /* if it's a taken branch, wait for timer */
      if(FETCH_BREAK_ON_TAKEN && op->oracle_info.pred &&
         *break_fetch != BREAK_BARRIER) {
        *break_fetch = BREAK_TAKEN;
        if(FETCH_TAKEN_BUBBLE_CYCLES >= 1) {
          ic->timer_cycle = cycle_count + FETCH_TAKEN_BUBBLE_CYCLES;
          return IC_WAIT_FOR_TIMER;
        } else
          return IC_FETCH;
      }
    } else {
      if(op->eom) {
        ic->next_fetch_addr = ADDR_PLUS_OFFSET(
          ic->next_fetch_addr, op->inst_info->trace_info.inst_size);
        ASSERT_PROC_ID_IN_ADDR(ic->proc_id, ic->next_fetch_addr)
      }
      // pass the global branch history to all the instructions
      op->oracle_info.pred_global_hist = g_bp_data->global_hist;
      ASSERT_PROC_ID_IN_ADDR(ic->proc_id, ic->next_fetch_addr)
    }

    if(packet_break == PB_BREAK_AFTER)
      break;
  }

  if(*break_fetch == BREAK_BARRIER) {
    return IC_WAIT_FOR_EMPTY_ROB;
  }

  return IC_FETCH;
}

/**************************************************************************************/
/* icache_fill_line: */

Flag icache_fill_line(Mem_Req* req)  // cmp FIXME maybe needed to be optimized
{
  Addr         repl_line_addr;
  Addr         repl_line_addr2;
  Inst_Info**  line;
  Addr         dummy_addr;
  Addr         dummy_addr2;
  Icache_Data* line_info = NULL;
  UNUSED(line);

  // cmp
  if(model->id == CMP_MODEL) {
    set_icache_stage(&cmp_model.icache_stage[req->proc_id]);
  }

  ASSERT(ic->proc_id, ic->proc_id == req->proc_id);

  if(req->dirty_l0) {
    STAT_EVENT(ic->proc_id, DIRTY_WRITE_TO_ICACHE);
    printf("fetch_addr:%s line_addr:%s req_addr:%s off:%d\n",
           hexstr64s(ic->fetch_addr), hexstr64s(ic->line_addr),
           hexstr64s(req->addr), ic->off_path);
  }

  /* get new line in the cache */
  if((ic->line_addr == req->addr) && ((ic->state == IC_WAIT_FOR_MISS) ||
                                      (ic->next_state == IC_WAIT_FOR_MISS))) {
    if(IC_PREF_CACHE_ENABLE &&  // cmp FIXME prefetchers
       (USE_CONFIRMED_OFF ? req->off_path_confirmed : req->off_path)) {
      Addr pref_line_addr;

      line = (Inst_Info**)cache_insert(&ic->pref_icache, ic->proc_id,
                                       ic->fetch_addr, &pref_line_addr,
                                       &repl_line_addr);
      DEBUG(
        ic->proc_id,
        "Insert PREF_ICACHE fetch_addr0x:%s line_addr:%s index:%ld addr:0x%s\n",
        hexstr64(ic->fetch_addr), hexstr64(pref_line_addr),
        (long int)(req - mem->req_buffer), hexstr64s(req->addr));
      STAT_EVENT(ic->proc_id, IC_PREF_CACHE_FILL);

      ic->next_state = IC_FETCH;
      return TRUE;
    }

    ic->line = (Inst_Info**)cache_insert(&ic->icache, ic->proc_id,
                                         ic->fetch_addr, &ic->line_addr,
                                         &repl_line_addr);

    STAT_EVENT(ic->proc_id, ICACHE_FILL);

    if(WP_COLLECT_STATS) {  // cmp IGNORE
      line_info = (Icache_Data*)cache_insert(&ic->icache_line_info, ic->proc_id,
                                             ic->fetch_addr, &dummy_addr2,
                                             &repl_line_addr2);

      line_info->fetched_by_offpath = USE_CONFIRMED_OFF ?
                                        req->off_path_confirmed :
                                        req->off_path;
      line_info->offpath_op_addr   = req->oldest_op_addr;
      line_info->offpath_op_unique = req->oldest_op_unique_num;
      line_info->fetch_cycle       = cycle_count;
      line_info->onpath_use_cycle  = req->off_path ? 0 : cycle_count;
      line_info->HW_prefetch       = (req->type == MRT_IPRF);
      wp_process_icache_fill(line_info, req);
    }

    ic->next_state = IC_FETCH;
    STAT_EVENT(ic->proc_id, ICACHE_FILL_CORRECT_REQ);
  } else {
    if(IC_PREF_CACHE_ENABLE &&  // cmp FIXME prefetchers
       (USE_CONFIRMED_OFF ? req->off_path_confirmed : req->off_path)) {
      Addr pref_line_addr;

      line = (Inst_Info**)cache_insert(&ic->pref_icache, ic->proc_id, req->addr,
                                       &pref_line_addr, &repl_line_addr);
      DEBUG(
        ic->proc_id,
        "Insert PREF_ICACHE fetch_addr0x:%s line_addr:%s index:%ld addr:0x%s\n",
        hexstr64(req->addr), hexstr64(pref_line_addr),
        (long int)(req - mem->req_buffer), hexstr64s(req->addr));
      STAT_EVENT(ic->proc_id, IC_PREF_CACHE_FILL);

      return TRUE;
    }

    line = (Inst_Info**)cache_insert(&ic->icache, ic->proc_id, req->addr,
                                     &dummy_addr, &repl_line_addr);

    if(WP_COLLECT_STATS) {  // cmp IGNORE
      line_info = (Icache_Data*)cache_insert(&ic->icache_line_info, ic->proc_id,
                                             req->addr, &dummy_addr2,
                                             &repl_line_addr2);
      STAT_EVENT(ic->proc_id, ICACHE_FILL);

      line_info->fetched_by_offpath = USE_CONFIRMED_OFF ?
                                        req->off_path_confirmed :
                                        req->off_path;
      line_info->offpath_op_addr   = req->oldest_op_addr;
      line_info->offpath_op_unique = req->oldest_op_unique_num;
      line_info->fetch_cycle       = cycle_count;
      line_info->onpath_use_cycle  = req->off_path ? 0 : cycle_count;
      line_info->HW_prefetch       = (req->type == MRT_IPRF);
      wp_process_icache_fill(line_info, req);
    }

    STAT_EVENT(ic->proc_id, ICACHE_FILL_INCORRECT_REQ);
  }

  return TRUE;
}

/**************************************************************************************/
/* icache_off_path: */

inline Flag icache_off_path(void) {
  return ic->off_path;
}

/*************************************************************************************/
/* ic_pref_cace_access() */

Inst_Info** ic_pref_cache_access(void) {
  Addr        repl_line_addr, inval_line_addr;
  Inst_Info** inserted_line = NULL;

  ASSERT_PROC_ID_IN_ADDR(ic->proc_id, ic->fetch_addr)
  Inst_Info** line = (Inst_Info**)cache_access(&ic->pref_icache, ic->fetch_addr,
                                               &ic->line_addr, FALSE);

  if(ic->off_path && !PREFCACHE_MOVE_OFFPATH) {
    if(line) {
      DEBUG(ic->proc_id, "off_path ic_pref cache hit:fetch_addr:0x%s \n",
            hexstr64(ic->fetch_addr));
      STAT_EVENT(ic->proc_id, IC_PREF_CACHE_HIT_PER_OFFPATH);
      STAT_EVENT(ic->proc_id, IC_PREF_CACHE_HIT_OFFPATH);
    }
    return line;
  }

  if(line) {
    inserted_line = (Inst_Info**)cache_insert(&ic->icache, ic->proc_id,
                                              ic->fetch_addr, &ic->line_addr,
                                              &repl_line_addr);
    DEBUG(ic->proc_id, "ic_pref cache hit:fetch_addr:0x%s \n",
          hexstr64(ic->fetch_addr));
    STAT_EVENT(ic->proc_id, IC_PREF_MOVE_IC);

    STAT_EVENT(ic->proc_id, ICACHE_FILL_CORRECT_REQ);

    STAT_EVENT(ic->proc_id, IC_PREF_CACHE_HIT_PER + MIN2(ic->off_path, 1));
    STAT_EVENT(ic->proc_id, IC_PREF_CACHE_HIT + MIN2(ic->off_path, 1));
    cache_invalidate(&ic->pref_icache, ic->fetch_addr, &inval_line_addr);

    if(PREF_ICACHE_HIT_FILL_L1) {
      if(model->mem == MODEL_MEM) {
        Addr     line_addr;
        Cache*   l1_cache = &mem->uncores[ic->proc_id].l1->cache;
        L1_Data* l1_data  = (L1_Data*)cache_access(l1_cache, ic->fetch_addr,
                                                  &line_addr, TRUE);
        if(!l1_data) {
          Mem_Req tmp_req;
          tmp_req.addr     = ic->fetch_addr;
          tmp_req.off_path = FALSE;
          tmp_req.op_count = 0;
          FATAL_ERROR(0, "This fill code is wrong. Writebacks may be lost.");
          l1_fill_line(&tmp_req);
          STAT_EVENT(ic->proc_id, IC_PREF_MOVE_L1);
        }
      }
    }
  }

  return inserted_line;
}

/**************************************************************************************/
/* wp_process_icache_hit: */

void wp_process_icache_hit(Icache_Data* line, Addr fetch_addr) {
  L1_Data* l1_line;

  if(!WP_COLLECT_STATS)
    return;

  if(icache_off_path() == FALSE) {
    if(line->fetched_by_offpath) {
      STAT_EVENT(ic->proc_id, ICACHE_HIT_ONPATH_SAT_BY_OFFPATH);
      STAT_EVENT(ic->proc_id, ICACHE_USE_OFFPATH);
      STAT_EVENT(ic->proc_id, DIST_ICACHE_FILL_OFFPATH_USED);
      STAT_EVENT(ic->proc_id, DIST_REQBUF_OFFPATH_USED);
      STAT_EVENT(ic->proc_id, DIST2_REQBUF_OFFPATH_USED_FULL);

      l1_line = do_l1_access_addr(fetch_addr);
      if(l1_line) {
        if(l1_line->fetched_by_offpath) {
          STAT_EVENT(ic->proc_id, L1_USE_OFFPATH);
          STAT_EVENT(ic->proc_id, DIST_L1_FILL_OFFPATH_USED);
          STAT_EVENT(ic->proc_id, L1_USE_OFFPATH_IFETCH);
          l1_line->fetched_by_offpath             = FALSE;
          l1_line->l0_modified_fetched_by_offpath = TRUE;
        }
      }
    } else {
      STAT_EVENT(ic->proc_id, ICACHE_HIT_ONPATH_SAT_BY_ONPATH);
      STAT_EVENT(ic->proc_id, ICACHE_USE_ONPATH);
    }
  } else {
    if(line->fetched_by_offpath) {
      STAT_EVENT(ic->proc_id, ICACHE_HIT_OFFPATH_SAT_BY_OFFPATH);
    } else {
      STAT_EVENT(ic->proc_id, ICACHE_HIT_OFFPATH_SAT_BY_ONPATH);
    }
  }

  if(icache_off_path() == FALSE)
    line->fetched_by_offpath = FALSE;
}

/**************************************************************************************/
/* wp_process_icache_fill: */

void wp_process_icache_fill(Icache_Data* line, Mem_Req* req) {
  if(!WP_COLLECT_STATS)
    return;

  if((req->type == MRT_WB) || (req->type == MRT_WB_NODIRTY) ||
     (req->type == MRT_IPRF)) /* for now we don't consider prefetches */
    return;

  if(req->off_path) {
    STAT_EVENT(ic->proc_id, ICACHE_FILL_OFFPATH);
  } else {
    STAT_EVENT(ic->proc_id, ICACHE_FILL_ONPATH);
    if(req->onpath_match_offpath)
      STAT_EVENT(ic->proc_id, DIST_ICACHE_FILL_ONPATH_PARTIAL);
    else
      STAT_EVENT(ic->proc_id, DIST_ICACHE_FILL_ONPATH);
  }
  STAT_EVENT(ic->proc_id, DIST_ICACHE_FILL);
}

/**************************************************************************************/
/* inst_lost_get_full_window_reason(): */

int32_t inst_lost_get_full_window_reason() {
  if(rob_stall_reason != ROB_STALL_NONE) {
    return rob_stall_reason;
  }

  if(rob_block_issue_reason != ROB_BLOCK_ISSUE_NONE) {
    return rob_block_issue_reason;
  }

  return 0;
}
