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
 * File         : exec_stage.c
 * Author       : HPS Research Group
 * Date         : 1/27/1999
 * Description  : CMP support
 ***************************************************************************************/

#include "debug/debug_macros.h"
#include "debug/debug_print.h"
#include "debug/memview.h"
#include "globals/assert.h"
#include "globals/global_defs.h"
#include "globals/global_types.h"
#include "globals/global_vars.h"
#include "globals/utils.h"

#include "bp/bp.h"
#include "exec_ports.h"
#include "exec_stage.h"
#include "map.h"

#include "bp/bp.param.h"
#include "cmp_model.h"
#include "core.param.h"
#include "debug/debug.param.h"
#include "dvfs/perf_pred.h"
#include "general.param.h"
#include "memory/memory.param.h"
#include "statistics.h"


/**************************************************************************************/
/* Macros */

#define DEBUG(proc_id, args...) _DEBUG(proc_id, DEBUG_EXEC_STAGE, ##args)


/**************************************************************************************/
/* Global Variables */

Exec_Stage* exec = NULL;
int         op_type_delays[NUM_OP_TYPES];
int         exec_off_path;
/**************************************************************************************/
/* Prototypes */

static void init_op_type_delays();
void        exec_stage_inc_power_stats(Op* op);

/**************************************************************************************/
/* set_exec_stage: */

void set_exec_stage(Exec_Stage* new_exec) {
  exec = new_exec;
}

/**************************************************************************************/
// init_op_type_delays

static void init_op_type_delays() {
  uns ii;

  if(UNIFORM_OP_DELAY) {
    for(ii = 0; ii < NUM_OP_TYPES; ii++)
      op_type_delays[ii] = UNIFORM_OP_DELAY;
    return;
  }

#define OP_TYPE_DELAY_INIT(op_type) \
  op_type_delays[OP_##op_type] = OP_##op_type##_DELAY;
  OP_TYPE_LIST(OP_TYPE_DELAY_INIT);
#undef OP_TYPE_DELAY_INIT

  /* make sure all op_type_delays were set */
  for(ii = 0; ii < NUM_OP_TYPES; ii++)
    ASSERT(td->proc_id, op_type_delays[ii] != 0);
}

/**************************************************************************************/
/* init_exec_stage: */

void init_exec_stage(uns8 proc_id, const char* name) {
  ASSERT(proc_id, exec);
  DEBUG(proc_id, "Initializing %s stage\n", name);

  memset(exec, 0, sizeof(Exec_Stage));

  exec->proc_id = proc_id;

  exec->sd.name         = (char*)strdup(name);
  exec->sd.max_op_count = NUM_FUS;
  exec->sd.ops          = (Op**)malloc(sizeof(Op*) * NUM_FUS);
  exec->fus_busy        = 0;

  reset_exec_stage();

  init_op_type_delays();
}


/**************************************************************************************/
/* reset_exec_stage: */

void reset_exec_stage() {
  uns ii;
  exec->sd.op_count = 0;
  for(ii = 0; ii < NUM_FUS; ii++) {
    exec->sd.ops[ii] = NULL;
  }
}


/**************************************************************************************/
/* recover_exec_stage: */

void recover_exec_stage() {
  uns ii;
  exec_off_path = 0;
  for(ii = 0; ii < NUM_FUS; ii++) {
    Func_Unit* fu = &exec->fus[ii];
    Op*        op = exec->sd.ops[ii];
    if(op && op->op_num > bp_recovery_info->recovery_op_num) {
      exec->sd.ops[ii] = NULL;
      exec->sd.op_count--;
      fu->avail_cycle = cycle_count + 1;
      fu->idle_cycle  = cycle_count + 1;
    }
  }
}


/**************************************************************************************/
/* debug_exec_stage: */

void debug_exec_stage() {
  int ii;
  DPRINTF("# %-10s  op_count:%d  busy:", exec->sd.name, exec->sd.op_count);
  for(ii = 0; ii < NUM_FUS; ii++) {
    Func_Unit* fu = &exec->fus[ii];
    if(ii % 4 == 0)
      DPRINTF(" ");
    DPRINTF("%d", fu->idle_cycle > cycle_count);
  }
  DPRINTF("  mem_stalls:");
  for(ii = 0; ii < NUM_FUS; ii++) {
    Func_Unit* fu = &exec->fus[ii];
    if(ii % 4 == 0)
      DPRINTF(" ");
    DPRINTF("%d", fu->held_by_mem);
  }
  DPRINTF("\n");
  print_op_array(GLOBAL_DEBUG_STREAM, exec->sd.ops, NUM_FUS, NUM_FUS);
}


/**************************************************************************************/
/* exec_cycle: */

void update_exec_stage(Stage_Data* src_sd) {
  // An array corresponds to src_sd->ops.
  // For each op, if its target FU is busy, one of the following must happen:
  // 1. The op is NULL. No op scheduled for this FU.
  // 2. The op is removed becase the FU is not yet ready.
  // 3. The op is removed becase of a memory stall.
  Flag src_op_assrtions[src_sd->max_op_count];
  memset(src_op_assrtions, FALSE, src_sd->max_op_count);

  uns ii;
  ASSERT(exec->proc_id, exec->sd.op_count <= exec->sd.max_op_count);
  // {{{ phase 1 - success/failure of latching and wake up of dependent ops
  if (!exec_off_path) {
    if (!exec->sd.op_count)
      STAT_EVENT(exec->proc_id, EXEC_STAGE_STARVED);
    else
      STAT_EVENT(exec->proc_id, EXEC_STAGE_NOT_STARVED);
  }
  else
    STAT_EVENT(exec->proc_id, EXEC_STAGE_OFF_PATH);

  for(ii = 0; ii < src_sd->max_op_count; ii++) {
    if (src_sd->ops[ii] && src_sd->ops[ii]->off_path)
      exec_off_path = 1;
  }

  for(ii = 0; ii < src_sd->max_op_count; ii++) {
    Func_Unit* fu  = &exec->fus[ii];
    Op*        op  = src_sd->ops[ii];
    Op*        fop = exec->sd.ops[ii];
    int        latency;
    Counter    exec_cycle;

    if (!op) {
      src_op_assrtions[ii] = TRUE;
    }

    // {{{ rejection/failure to latch cases
    if(cycle_count < fu->avail_cycle) {
      // fu not available, so nullify node stage entry to make
      //   instruction get scheduled again
      if(op != NULL) {
        op->delay_bit   = 1;
        src_sd->ops[ii] = NULL;
        src_sd->op_count--;
        STAT_EVENT(exec->proc_id, FU_UNAVAILABLE);
        if (op->table_info->is_simd)
          STAT_EVENT(exec->proc_id, FU_REJECTED_OP_INV_SIMD + op->table_info->op_type);
        else
          STAT_EVENT(exec->proc_id, FU_REJECTED_OP_INV_NOT_SIMD + op->table_info->op_type);
        src_op_assrtions[ii] = TRUE;
      }
      continue;
    }
    if(fop && fop->table_info->mem_type) {
      if(fop->replay && fop->replay_cycle == cycle_count) {
        // it's a simultaneous replay...need to kill it
        exec->sd.ops[ii] = NULL;
        exec->sd.op_count--;
        ASSERT(exec->proc_id, exec->sd.op_count >= 0);
        STAT_EVENT(exec->proc_id, FU_REPLAY);
      } else {
        // memory stall
        if(op != NULL) {
          op->delay_bit   = 1;
          src_sd->ops[ii] = NULL;
          src_sd->op_count--;
          STAT_EVENT(exec->proc_id, FU_MEM_UNAVAILABLE);
          if (op->table_info->is_simd)
            STAT_EVENT(exec->proc_id, FU_REJECTED_OP_INV_SIMD + op->table_info->op_type);
          else
            STAT_EVENT(exec->proc_id, FU_REJECTED_OP_INV_NOT_SIMD + op->table_info->op_type);
          src_op_assrtions[ii] = TRUE;
        }
        continue;
      }
    } else {
      // remove op currently in the fu
      exec->sd.op_count -= exec->sd.ops[ii] != NULL;
      ASSERT(exec->proc_id, exec->sd.op_count >= 0);
      exec->sd.ops[ii] = NULL;
    }
    if(!op) {
      STAT_EVENT(exec->proc_id, FU_STARVED);
      continue;
    }
    // }}}

    // {{{ dependent instruction wakeup

    // if we get to here, then it means the op is going into the
    // functional unit.  We need to perform wake-ups of all the
    // dependent ops.  This is done before the actual latching of
    // the ops into the execute stage in order to make sure ops
    // flushed or replayed during the current cycle do not sneak
    // into the execute stage because they happened to be
    // processed before the op causing the recovery or replay.

    latency = op->inst_info->latency;
    ASSERTM(exec->proc_id, OP_SRCS_RDY(op), "op_num:%s\n",
            unsstr64(op->op_num));
    ASSERT(
      exec->proc_id,
      get_fu_type(op->table_info->op_type, op->table_info->is_simd) & fu->type);
    exec_cycle      = cycle_count + MAX2(latency, -latency);
    op->sched_cycle = cycle_count;

    DEBUG(exec->proc_id, "op_num:%s fu_num:%d sched_cycle:%s off_path:%d\n",
          unsstr64(op->op_num), op->fu_num, unsstr64(op->sched_cycle),
          op->off_path);
    if(op->table_info->mem_type == NOT_MEM) {
      // non-memory ops will always distribute their results
      // after the op's latency
      op->wake_cycle = exec_cycle;
      wake_up_ops(op, REG_DATA_DEP, model->wake_hook);
    } else if(op->table_info->mem_type == MEM_ST) {
      // stores have their addresses computed in this cycle and
      // also write their data into the store buffer
      if(op->exec_count == 0) {
        // only wake up if this is the first time this op executes
        op->wake_cycle = exec_cycle;
        wake_up_ops(op, MEM_ADDR_DEP, model->wake_hook);
        wake_up_ops(op, MEM_DATA_DEP, model->wake_hook);
      }
    }

    exec_stage_inc_power_stats(op);

    // all other ops (loads) will be handled by the memory system
    // }}}
  }
  // }}}

  // {{{ phase 2 - actual latching of instructions and setting of state
  for(ii = 0; ii < src_sd->max_op_count; ii++) {
    Func_Unit* fu  = &exec->fus[ii];
    Op*        op  = src_sd->ops[ii];
    Op*        fop = exec->sd.ops[ii];
    int        latency;
    Counter    exec_cycle;
    Flag       is_replay = FALSE;

    UNUSED(exec_cycle);

    if(fop) {
      // For explanations, see top of the function
      ASSERT(exec->proc_id, src_op_assrtions[ii]);
      // if there is still an op in the fu, the fu is still busy and there is
      // nothing to latch
      ASSERT(exec->proc_id, !op);
      STAT_EVENT(exec->proc_id, FU_BUSY_0 + ii);
      STAT_EVENT(exec->proc_id, FUS_BUSY_ON_PATH + fop->off_path);
      if(fop->table_info->mem_type) {
        fu->held_by_mem = TRUE;
        STAT_EVENT(exec->proc_id, FU_BUSY_MEM_STALL);
      }
      continue;
    }

    fu->held_by_mem = FALSE;

    if(!op) {
      STAT_EVENT(exec->proc_id, FUS_EMPTY);
      continue;  // there is nothing to latch from the previous stage
    }

    STAT_EVENT(exec->proc_id, FU_BUSY_0 + ii);
    STAT_EVENT(exec->proc_id, FUS_BUSY_ON_PATH + op->off_path);

    // remove the op from the "schedule" list
    src_sd->ops[ii] = NULL;
    src_sd->op_count--;
    ASSERT(exec->proc_id, src_sd->op_count >= 0);

    // busy the functional unit
    latency = op->inst_info->latency;
    ASSERT(0, latency);  // otherwise ready list management breaks
    exec_cycle       = cycle_count + MAX2(latency, -latency);
    exec->sd.ops[ii] = op;
    exec->sd.op_count++;
    ASSERT(exec->proc_id, exec->sd.op_count <= exec->sd.max_op_count);
    // if the op is not pipelined, then busy up the functional unit
    fu->avail_cycle = cycle_count + (latency < 0 ? -latency : 1);
    fu->idle_cycle  = cycle_count + (latency < 0 ? -latency : latency);

    // set the op's state to reflect it's execution
    if(op->table_info->mem_type == NOT_MEM || STALL_ON_WAIT_MEM) {
      op->state = OS_SCHEDULED;
    } else {
      op->state = OS_TENTATIVE;  // mem op may fail if it misses and can't get a
                                 // mem req buffer
    }
    op->exec_cycle = cycle_count + MAX2(latency, -latency);

    char address_as_string[128] = {0};
    sprintf(address_as_string, "%016llX", op->inst_info->addr);

    bool first_time_exec = false;
    unsigned long extra_exec_cycles = 0;
    // update the inst_fetch_exec_truple
    starlab_hash_table* inst_truple_ptr = (starlab_hash_table*) voided_inst_truple_ptr;
    if(inst_truple_ptr == NULL)
    {
      inst_truple_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(inst_fetch_exec_truple));
    }
    // is this already present? 
    if(!starlab_search(inst_truple_ptr, address_as_string))
    {
      // should really ever ever never go here
      printf("went here\n");
      inst_fetch_exec_truple temp_truple_to_insert;
      temp_truple_to_insert.exec_cycle = -1;
      temp_truple_to_insert.fetch_cycle = op->fetch_cycle;
      temp_truple_to_insert.prev_fetch_cycle = op->fetch_cycle;
      starlab_insert(inst_truple_ptr, address_as_string, &temp_truple_to_insert);
    }
    else
    {
      // ((inst_fetch_exec_truple*) starlab_search(inst_truple_ptr, address_as_string))->fetch_cycle = op->fetch_cycle;
      if(((inst_fetch_exec_truple*) starlab_search(inst_truple_ptr, address_as_string))->exec_cycle == -1)
      {
        first_time_exec = true;
        ((inst_fetch_exec_truple*) starlab_search(inst_truple_ptr, address_as_string))->exec_cycle = op->exec_cycle;
      }
      else if(op->exec_cycle > ((inst_fetch_exec_truple*) starlab_search(inst_truple_ptr, address_as_string))->exec_cycle)
      {
        extra_exec_cycles = op->exec_cycle - ((inst_fetch_exec_truple*) starlab_search(inst_truple_ptr, address_as_string))->exec_cycle;
        ((inst_fetch_exec_truple*) starlab_search(inst_truple_ptr, address_as_string))->exec_cycle = op->exec_cycle;
      }
      ((inst_fetch_exec_truple*) starlab_search(inst_truple_ptr, address_as_string))->prev_fetch_cycle = -1;
      // printf("[%016llu] set %lu whenin %llu\n", op->inst_info->addr, ((inst_fetch_exec_truple*) starlab_search(inst_truple_ptr, address_as_string))->exec_cycle, op->exec_cycle);
    }

    char prev_address_as_string[128] = {0};
    
    unsigned long long* starlab_prev_address_for_exec_stage_ptr = (unsigned long long*) starlab_search(voided_address_to_prev_address, address_as_string);
    
    if(starlab_prev_address_for_exec_stage_ptr == NULL)
    {
      // do nothing
    }
    else
    {
      sprintf(prev_address_as_string, "%016llX", *starlab_prev_address_for_exec_stage_ptr);

      inst_fetch_exec_truple* prev_truple_ptr = ((inst_fetch_exec_truple*) starlab_search(inst_truple_ptr, prev_address_as_string));
      inst_fetch_exec_truple* this_truple_ptr = ((inst_fetch_exec_truple*) starlab_search(inst_truple_ptr, address_as_string));
      if(prev_truple_ptr == NULL || this_truple_ptr == NULL)
      {
        // do nothing
      }
      else
      {
        unsigned long cc_to_add = this_truple_ptr->exec_cycle - prev_truple_ptr->fetch_cycle;
        if(!first_time_exec)
        {
          cc_to_add = extra_exec_cycles;
        }
        char* prev_iclass = (char*) starlab_search(voided_address_to_type_ptr, prev_address_as_string);
        char* this_iclass = (char*) starlab_search(voided_address_to_type_ptr, address_as_string);

        if(prev_iclass != NULL && this_iclass != NULL)
        {
          char tuple_string[128] = {0};
          sprintf(tuple_string, "<%s,%s>", prev_iclass, this_iclass);

          if(voided_global_starlab_types_ht == NULL)
          {
            voided_global_starlab_types_ht = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(unsigned long));
          }
          if(!starlab_search(voided_global_starlab_types_ht, tuple_string))
          {
            starlab_insert(voided_global_starlab_types_ht, tuple_string, &cc_to_add);
          }
          else
          {
            
            unsigned long* cc_ptr = (unsigned long*) starlab_search(voided_global_starlab_types_ht, tuple_string);
            // printf("[exec] succesfully added %lu %lu\n", *cc_ptr, cc_to_add);
            // printf("%lu %lu\n %lu %lu    %llu\n", this_truple_ptr->exec_cycle, this_truple_ptr->fetch_cycle,prev_truple_ptr->exec_cycle, prev_truple_ptr->fetch_cycle, op->exec_cycle );
            *cc_ptr+= cc_to_add;
          }
        }
      }
    }
    voided_inst_truple_ptr = (void *) inst_truple_ptr;

    // starlab_hash_table* global_starlab_ht_ptr = (starlab_hash_table*) voided_global_starlab_ht_ptr;

    // if(global_starlab_ht_ptr == NULL)
    // {
    //   global_starlab_ht_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(starlab_table_value));
    // }
    // starlab_hash_table* starlab_types_table_ptr = (starlab_hash_table*) voided_global_starlab_types_ht;
    // if(starlab_types_table_ptr == NULL)
    // {
    //   starlab_types_table_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(unsigned long));
    // }

    // char address_as_string[128] = {0};
    // sprintf(address_as_string, "%016llX%s",  op->inst_info->addr, starlab_get_opcode_string(op->table_info->op_type));
    // if(!starlab_search(global_starlab_ht_ptr,address_as_string))
    // {
    //   printf("[exec] Address ret %s not found [%016llX]\n", address_as_string, op->inst_info->addr);
    //   starlab_table_value temp_val_to_insert = {op->fetch_cycle, op->fetch_cycle};
    //   starlab_insert(global_starlab_ht_ptr, address_as_string, &temp_val_to_insert);
    //   strncpy(prev_address_as_string, address_as_string, 128);
    // }
    // else
    // {
    //   unsigned long prev_inst_prev_fetch;
    //   if(starlab_search(global_starlab_ht_ptr, prev_address_as_string) == NULL)
    //   {
    //     prev_inst_prev_fetch = prev_instruction_time;
    //   }
    //   else
    //     prev_inst_prev_fetch = ((starlab_table_value *) starlab_search(global_starlab_ht_ptr, prev_address_as_string))->prev_fetch;

    //   // unsigned long this_fetch_cc = op->fetch_cycle;
    //   unsigned long cc_taken_by_tuple = op->exec_cycle - prev_inst_prev_fetch;      
    //   printf("[exec] Address %s, %s FOUND ret! [%ld, %lld, %ld] -> <%s,%s>\n", prev_address_as_string, address_as_string, prev_inst_prev_fetch, op->exec_cycle, cc_taken_by_tuple, prev_instruction_class, starlab_get_opcode_string(op->table_info->op_type));
    //   starlab_delete_key(global_starlab_ht_ptr, address_as_string);

    //   char tuple_of_types[256] = {0};
    //   sprintf(tuple_of_types, "<%s,%s>", prev_instruction_class, starlab_get_opcode_string(op->table_info->op_type));

    //   if(!starlab_search(starlab_types_table_ptr, tuple_of_types))
    //   {
    //     unsigned long insert_val = cc_taken_by_tuple;
    //     starlab_insert(starlab_types_table_ptr, tuple_of_types, &insert_val);
    //   }
    //   else
    //   {
    //     unsigned long insert_val = *(unsigned long*) starlab_search(starlab_types_table_ptr, tuple_of_types) + cc_taken_by_tuple;
    //     starlab_insert(starlab_types_table_ptr, tuple_of_types, &insert_val);
    //   }
    //   prev_instruction_time = op->fetch_cycle;
    //   strncpy(prev_instruction_class, starlab_get_opcode_string(op->table_info->op_type), 100);
    //   strncpy(prev_address_as_string, address_as_string, 128);
    // }

    // voided_global_starlab_ht_ptr = (void *) global_starlab_ht_ptr;
    // voided_global_starlab_types_ht = (void *) starlab_types_table_ptr;


    op->exec_count++;

    if(op->table_info->mem_type == NOT_MEM)
      op->done_cycle = op->exec_cycle;

    STAT_EVENT(op->proc_id, EXEC_ON_PATH_INST + op->off_path);
    STAT_EVENT(op->proc_id, EXEC_ON_PATH_INST_MEM +
                              (op->table_info->mem_type == NOT_MEM) +
                              2 * op->off_path);
    STAT_EVENT(op->proc_id, EXEC_ALL_INST);
    exec->fus_busy++;

    DEBUG(exec->proc_id,
          "op_num:%s fu_num:%d exec_cycle:%s done_cycle:%s off_path:%d\n",
          unsstr64(op->op_num), op->fu_num, unsstr64(op->exec_cycle),
          unsstr64(op->done_cycle), op->off_path);

    // {{{ branch recovery/resolution code
    if(op->table_info->cf_type && !is_replay) {
      // branch recovery currently does not like to be done more
      // than 1 time.  since we don't have any way to know if an
      // op is going to be replayed, we have to go with the
      // first recovery (even though it is improper) for the
      // time being

      if(!BP_UPDATE_AT_RETIRE) {
        // this code updates the branch prediction structures
        if(op->table_info->cf_type >= CF_IBR)
          bp_target_known_op(g_bp_data, op);

        bp_resolve_op(g_bp_data, op);
      }

      if (op->oracle_info.recover_at_exec){
        bp_sched_recovery(bp_recovery_info, op, op->exec_cycle,
                          /*late_bp_recovery=*/FALSE, /*force_offpath=*/FALSE);
        if(!op->off_path)
          op->recovery_scheduled = TRUE;

        // stats for the reason of resteer
        if(op->oracle_info.mispred)
          STAT_EVENT(op->proc_id, RESTEER_MISPRED_NOT_CF + op->table_info->cf_type);
        else
          STAT_EVENT(op->proc_id, RESTEER_MISFETCH_NOT_CF + op->table_info->cf_type);

      }
      /*      else if(op->table_info->cf_type >= CF_IBR &&
                op->oracle_info.no_target) {
        ASSERT(bp_recovery_info->proc_id,
               bp_recovery_info->proc_id == op->proc_id);
        bp_sched_redirect(bp_recovery_info, op, op->exec_cycle);
        // stats for the reason of resteer
        STAT_EVENT(op->proc_id, RESTEER_NO_TARGET_CF_IBR + op->table_info->cf_type - CF_IBR);
        ASSERT(0,0);
        }*/
    }
    // }}}

    /* value prediction recovery/resolution code  */  // if we know the value at
                                                      // this point if not ?
                                                      // then we need to wait.
  }
  // }}}

  exec->fus_busy = 0;
  for(ii = 0; ii < src_sd->max_op_count; ii++) {
    Func_Unit* fu = &exec->fus[ii];
    // a functional unit is busy if there's an op in any stage
    // of its pipeline unless it's stalled by memory
    if(fu->idle_cycle > cycle_count && !fu->held_by_mem) {
      exec->fus_busy++;
    }
  }

  memview_fus_busy(exec->proc_id, exec->fus_busy);
}

void exec_stage_inc_power_stats(Op* op) {
  STAT_EVENT(op->proc_id, POWER_ROB_READ);
  STAT_EVENT(op->proc_id, POWER_ROB_WRITE);

  STAT_EVENT(op->proc_id, POWER_OP);

  if(op->table_info->op_type > OP_NOP && op->table_info->op_type < OP_FLD) {
    STAT_EVENT(op->proc_id, POWER_INT_OP);
  } else if(op->table_info->op_type >= OP_FLD) {
    STAT_EVENT(op->proc_id, POWER_FP_OP);
  }

  if(op->table_info->mem_type == MEM_LD || op->table_info->mem_type == MEM_PF) {
    STAT_EVENT(op->proc_id, POWER_LD_OP);
  } else if(op->table_info->mem_type == MEM_ST) {
    STAT_EVENT(op->proc_id, POWER_ST_OP);
  }


  if(!op->off_path) {
    STAT_EVENT(op->proc_id, POWER_COMMITTED_OP);

    if(op->table_info->op_type > OP_NOP && op->table_info->op_type < OP_FLD) {
      STAT_EVENT(op->proc_id, POWER_COMMITTED_INT_OP);
    } else {
      STAT_EVENT(op->proc_id, POWER_COMMITTED_FP_OP);
    }
  }

  if(op->table_info->cf_type == CF_CALL ||
     op->table_info->cf_type == CF_ICALL) {
    STAT_EVENT(op->proc_id, POWER_FUNCTION_CALL);
  }

  if(op->table_info->cf_type > NOT_CF) {
    STAT_EVENT(op->proc_id, POWER_BRANCH_OP);
  }

  if(power_get_fu_type(op->table_info->op_type, op->table_info->is_simd) !=
     POWER_FU_FPU) {
    /*Integer instructions*/
    INC_STAT_EVENT(op->proc_id, POWER_RENAME_READ, 2);
    STAT_EVENT(op->proc_id, POWER_RENAME_WRITE);

    STAT_EVENT(op->proc_id, POWER_INST_WINDOW_READ);
    STAT_EVENT(op->proc_id, POWER_INST_WINDOW_WRITE);
    STAT_EVENT(op->proc_id, POWER_INST_WINDOW_WAKEUP_ACCESS);

    INC_STAT_EVENT(op->proc_id, POWER_INT_REGFILE_READ,
                   op->table_info->num_src_regs);
    INC_STAT_EVENT(op->proc_id, POWER_INT_REGFILE_WRITE,
                   op->table_info->num_dest_regs);

    if(power_get_fu_type(op->table_info->op_type, op->table_info->is_simd) ==
       POWER_FU_MUL_DIV) {
      INC_STAT_EVENT(op->proc_id, POWER_MUL_ACCESS,
                     abs(op_type_delays[op->table_info->type]));
      STAT_EVENT(op->proc_id, POWER_CDB_MUL_ACCESS);
    } else {
      INC_STAT_EVENT(op->proc_id, POWER_IALU_ACCESS,
                     abs(op_type_delays[op->table_info->type]));
      STAT_EVENT(op->proc_id, POWER_CDB_IALU_ACCESS);
    }
  } else {
    /*Floating Point instructions*/
    STAT_EVENT(op->proc_id, POWER_FP_RENAME_WRITE);
    INC_STAT_EVENT(op->proc_id, POWER_FP_RENAME_READ, 2);

    STAT_EVENT(op->proc_id, POWER_FP_INST_WINDOW_READ);
    STAT_EVENT(op->proc_id, POWER_FP_INST_WINDOW_WRITE);
    STAT_EVENT(op->proc_id, POWER_FP_INST_WINDOW_WAKEUP_ACCESS);

    INC_STAT_EVENT(op->proc_id, POWER_FP_REGFILE_READ,
                   op->table_info->num_src_regs);
    INC_STAT_EVENT(op->proc_id, POWER_FP_REGFILE_WRITE,
                   op->table_info->num_dest_regs);

    INC_STAT_EVENT(op->proc_id, POWER_FPU_ACCESS,
                   abs(op_type_delays[op->table_info->type]));
    STAT_EVENT(op->proc_id, POWER_CDB_FPU_ACCESS);
  }


  if(op->table_info->mem_type == MEM_ST) {
    STAT_EVENT(op->proc_id, POWER_DTLB_ACCESS);
  }
}
