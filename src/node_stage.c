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
 * File         : node_stage.c
 * Author       : HPS Research Group
 * Date         : 1/28/1999
 * Description  :
 ***************************************************************************************/

#include <unistd.h>

#include "debug/debug_macros.h"
#include "debug/debug_print.h"
#include "debug/memview.h"
#include "globals/assert.h"
#include "globals/global_defs.h"
#include "globals/global_types.h"
#include "globals/global_vars.h"
#include "globals/utils.h"
#include "op_pool.h"

#include "bp/bp.h"
#include "exec_ports.h"
#include "frontend/frontend.h"
#include "memory/memory.h"
#include "node_stage.h"
#include "thread.h"

#include "bp/bp.param.h"
#include "core.param.h"
#include "debug/debug.param.h"
#include "map.h"
#include "memory/memory.param.h"
#include "sim.h"
#include "statistics.h"

#include "bp/tagescl.h"
#include "decoupled_frontend.h"

/* Macros */

#define DEBUG(proc_id, args...) _DEBUG(proc_id, DEBUG_NODE_STAGE, ##args)
#define PRINT_RETIRED_UOP(proc_id, args...) \
  _DEBUG_LEAN(proc_id, DEBUG_RETIRED_UOPS, ##args)

#define DEBUG_NODE_WIDTH ISSUE_WIDTH
#define OP_IS_IN_RS(op) (op->state >= OS_IN_RS && op->state < OS_SCHEDULED)

/**************************************************************************************/
/* Global Variables */

Node_Stage*            node                   = NULL;
Rob_Stall_Reason       rob_stall_reason       = ROB_STALL_NONE;
Rob_Block_Issue_Reason rob_block_issue_reason = ROB_BLOCK_ISSUE_NONE;

static bool is_first_op = true; 


/**************************************************************************************/
/* Prototypes */

void debug_print_retired_uop(Op* op);
void flush_ready_list(void);
void flush_scheduling_buffer(void);
void flush_rs(void);
void flush_window(void);
void debug_print_node_table(void);
void debug_print_rs(void);
void debug_print_ready_list(void);
Flag op_not_ready_for_retire(Op* op);
Flag is_node_table_empty(void);
void collect_not_ready_to_retire_stats(Op* op);
Flag is_node_table_full(void);
void collect_node_table_full_stats(Op* op);
int is_alu_op(unsigned int op_type); 

/**************************************************************************************/
/* set_node_stage:*/

void set_node_stage(Node_Stage* new_node) {
  node = new_node;
}

/**************************************************************************************/
/* init_node_stage:*/

void init_node_stage(uns8 proc_id, const char* name) {
  ASSERT(proc_id, node);
  DEBUG(proc_id, "Initializing %s stage\n", name);

  node->proc_id = proc_id;
  node->sd.name = (char*)strdup(name);

  // allocate wires to functional units
  node->sd.max_op_count = NUM_FUS;  // Bandwidth between schedule and FUS
  node->sd.ops          = (Op**)malloc(sizeof(Op*) * node->sd.max_op_count);

  reset_node_stage();
}

/**************************************************************************************/
/* reset_node_stage:*/

void reset_node_stage() {
  uns ii;
  for(ii = 0; ii < NUM_FUS; ii++)
    node->sd.ops[ii] = NULL;
  node->sd.op_count = 0;

  node->node_head       = NULL;
  node->node_tail       = NULL;
  node->rdy_head        = NULL;
  node->next_op_into_rs = NULL;

  node->node_count           = 0;
  node->ret_op               = 1;
  node->last_scheduled_opnum = 0;
  node->mem_blocked          = FALSE;
  node->mem_block_length     = 0;
  node->ret_stall_length     = 0;
}

/**************************************************************************************/
/* reset_node_stage:*/
// CMP used for bogus run: may be combined with reset_node_stage
void reset_all_ops_node_stage() {
  uns ii;
  for(ii = 0; ii < NUM_FUS; ii++)
    node->sd.ops[ii] = NULL;
  node->sd.op_count = 0;

  node->node_head       = NULL;
  node->node_tail       = NULL;
  node->rdy_head        = NULL;
  node->next_op_into_rs = NULL;

  node->node_count       = 0;
  node->node_count       = 0;
  node->mem_blocked      = FALSE;
  node->ret_stall_length = 0;
}

/**************************************************************************************/
/* recover_node_stage:*/

void recover_node_stage() {
  ASSERT(node->proc_id, node->proc_id == bp_recovery_info->proc_id);

  DEBUG(node->proc_id, "Recovering '%s' stage\n", node->sd.name);
  if(ENABLE_GLOBAL_DEBUG_PRINT && DEBUG_NODE_STAGE &&
     DEBUG_RANGE_COND(node->proc_id))
    debug_node_stage();

  flush_ready_list();
  flush_scheduling_buffer();
  flush_rs();
  flush_window();

  // recover last_scheduled_opnum
  if(node->last_scheduled_opnum >= bp_recovery_info->recovery_op_num)
    node->last_scheduled_opnum = bp_recovery_info->recovery_op_num;

  if(ENABLE_GLOBAL_DEBUG_PRINT && DEBUG_NODE_STAGE &&
     DEBUG_RANGE_COND(node->proc_id))
    debug_node_stage();
}

void flush_ready_list() {
  Op*  op;
  Op** last;
  for(op = node->rdy_head, last = &node->rdy_head; op; op = op->next_rdy) {
    ASSERT(node->proc_id, node->proc_id == op->proc_id);
    if(FLUSH_OP(op)) {
      ASSERT(node->proc_id, op->op_num > bp_recovery_info->recovery_op_num);
      *last           = op->next_rdy;
      op->in_rdy_list = FALSE;
    } else
      last = &op->next_rdy;
  }
}

void flush_scheduling_buffer() {
  uns ii;
  for(ii = 0; ii < node->sd.max_op_count; ii++) {
    Op* op = node->sd.ops[ii];
    if(op && FLUSH_OP(op)) {
      ASSERT(node->proc_id, node->proc_id == op->proc_id);
      ASSERTM(node->proc_id, op->op_num > bp_recovery_info->recovery_op_num,
              "op_num:%s\n", unsstr64(op->op_num));

      node->sd.ops[ii] = NULL;
      node->sd.op_count--;

      ASSERT(node->proc_id, node->sd.op_count >= 0);
    }
  }
}

void flush_rs() {
  Op* op = node->next_op_into_rs;
  if(op && FLUSH_OP(op)) {
    ASSERT(node->proc_id, node->proc_id == op->proc_id);
    ASSERTM(node->proc_id, op->op_num > bp_recovery_info->recovery_op_num,
            "op_num:%s\n", unsstr64(op->op_num));
    node->next_op_into_rs = NULL;  // all later ops will also be flushed
  }
}

void flush_window() {
  Op*  op;
  Op** last;
  uns  flush_ops = 0;
  uns  keep_ops  = 0;

  node->node_tail = NULL;
  for(op = node->node_head, last = &node->node_head; op; op = *last) {
    ASSERT(node->proc_id, node->proc_id == op->proc_id);

    if(FLUSH_OP(op)) {
      DEBUG(node->proc_id, "Node flushing  op:%s\n", unsstr64(op->op_num));
      flush_ops++;
      ASSERT(node->proc_id, op->op_num > bp_recovery_info->recovery_op_num);
      op->in_node_list = FALSE;
      *last            = op->next_node;
      if(op->state == OS_IN_RS || op->state == OS_READY ||
         op->state == OS_WAIT_FWD) {
        ASSERT(op->proc_id, node->rs[op->rs_id].rs_op_count > 0);
        node->rs[op->rs_id].rs_op_count--;
      }
      free_op(op);
    } else {
      /* Keep op */

      if(IS_FLUSHING_OP(op)) {
        /* Mark that the scheduled recovery has occurred */
        op->recovery_scheduled = FALSE;
      }
      DEBUG(node->proc_id, "Node keeping  op:%s node_id:%llu\n",
            unsstr64(op->op_num), op->node_id);
      keep_ops++;
      last            = &op->next_node;
      node->node_tail = op;
    }
  }

  ASSERT(node->proc_id, flush_ops + keep_ops == node->node_count);
  node->node_count = keep_ops;
  ASSERT(node->proc_id, node->node_count <= NODE_TABLE_SIZE);
}


/**************************************************************************************/
/* debug_node_stage:*/

void debug_node_stage() {
  DPRINTF("# %-10s  node_count:%d\n", node->sd.name, node->node_count);

  debug_print_node_table();
  debug_print_rs();
  debug_print_ready_list();
}

void debug_print_node_table() {
  Op* op;

  Counter row      = 0;
  Flag    empty    = TRUE;
  uns32   slot_num = 0;
  uns     printed  = 0;

  Op** temp = (Op**)calloc(DEBUG_NODE_WIDTH, sizeof(Op*));

  for(op = node->node_head; op; op = op->next_node, ++row) {
    slot_num = row % DEBUG_NODE_WIDTH;
    ASSERT(node->proc_id, node->proc_id == op->proc_id);
    ASSERT(node->proc_id, temp[slot_num] == NULL);
    temp[slot_num] = op;
    printed++;
    empty = FALSE;

    // we have populated entire row, print and reinitialize
    if(slot_num == DEBUG_NODE_WIDTH - 1) {
      if(!empty) {
        print_open_op_array(GLOBAL_DEBUG_STREAM, temp, DEBUG_NODE_WIDTH,
                            DEBUG_NODE_WIDTH);
      }
      // For some reason this does not zero out the entire array.
      // (Assert fails and verified in gdb).
      // memset(temp, 0, DEBUG_NODE_WIDTH * sizeof(Op*));
      for (int i=0; i<DEBUG_NODE_WIDTH; i++) temp[i] = 0;
      empty = TRUE;
    }
  }

  ASSERTM(node->proc_id, printed == node->node_count,
          "printed=%d, node_count=%d", printed, node->node_count);

  // If node table is empty, print a blank row. Or if there is a remainder,
  // print that too
  if(printed == 0 || slot_num < DEBUG_NODE_WIDTH - 1)
    print_open_op_array(GLOBAL_DEBUG_STREAM, temp, DEBUG_NODE_WIDTH,
                        DEBUG_NODE_WIDTH);

  print_open_op_array_end(GLOBAL_DEBUG_STREAM, DEBUG_NODE_WIDTH);

  free(temp);
}

void debug_print_rs() {
  Op*   op;
  uns   printed = 0;
  int32 i, j;

  ASSERT(node->proc_id, node->rs);

  for(i = 0; i < NUM_RS; ++i) {
    Reservation_Station* rs = &node->rs[i];
    printed                 = 0;
    DPRINTF("%s (%d/%s): ", rs->name, rs->rs_op_count,
            rs->size == 0 ? "inf" : unsstr64((uns64)rs->size));

    for(j = 0; j < rs->num_fus; ++j) {
      DPRINTF("%s, ", rs->connected_fus[j]->name);
    }

    DPRINTF("\n");

    for(op = node->node_head; op && op != node->next_op_into_rs;
        op = op->next_node) {
      if(op->rs_id == i && OP_IS_IN_RS(op)) {
        // Op belongs to this RS
        DPRINTF("%lld ", op->op_num);
        printed++;
        if(printed % 8 == 0)
          DPRINTF("\n");
      }
    }

    if(printed % 8)
      DPRINTF("\n");

    ASSERTM(node->proc_id, printed == rs->rs_op_count,
            "printed=%d, rs_op_count=%d\n", printed, rs->rs_op_count);
  }
}

void debug_print_ready_list() {
  Op* op;

  DPRINTF("Ready list:");

  for(op = node->rdy_head; op; op = op->next_rdy) {
    DPRINTF(" %s", unsstr64(op->op_num));
  }

  DPRINTF("\n");

  print_op_array(GLOBAL_DEBUG_STREAM, node->sd.ops, node->sd.max_op_count,
                 node->sd.max_op_count);
}

/**************************************************************************************/
/* node_cycle: */

void update_node_stage(Stage_Data* src_sd) {
  DEBUG(node->proc_id, "Beginning '%s' stage\n", node->sd.name);
  STAT_EVENT(node->proc_id, NODE_CYCLE);
  STAT_EVENT(node->proc_id, POWER_CYCLE);

  /* insert ops coming from the previous stage*/
  node_issue(src_sd);

  /* remove scheduled ops from RS and ready list */
  node_handle_scheduled_ops();

  /* fill RS with oldest ops waiting for it */
  node_fill_rs();

  /* first schedule 1 ready op per NUM_FUS  */
  // node_sched_ops();

  /* get rid of the ops that are finished */
  node_retire();

  memview_core_stall(node->proc_id, is_node_stage_stalled(), node->mem_blocked);
}


/**************************************************************************************/
/* node_issue:This function takes ops from the map stage and allocates them into
 *    the node table. Note, this function does not place the Op in the RS, that
 * is done later.*/

void node_issue(Stage_Data* src_sd) {
  Flag on_path = FALSE;
  uns  ii;

  /* if nothing to process, return */
  if(src_sd->op_count == 0)
    return;

  // Go through all the ops in the issue buffer and stick them into the Node
  // Table.
  // We will stick them into the RS later
  for(ii = 0; ii < src_sd->max_op_count; ii++) {
    /* if node table is full, stall */
    if(is_node_table_full()) {
      collect_node_table_full_stats(node->node_head);
      rob_block_issue_reason = ROB_BLOCK_ISSUE_FULL;
      return;
    }
    rob_block_issue_reason = ROB_BLOCK_ISSUE_NONE;

    // If it is not full, issue the next op
    Op* op = src_sd->ops[ii];
    if(!op)
      continue;

    ASSERT(node->proc_id, node->proc_id == op->proc_id);
    /* check if it's a synchronizing op that can't issue  */
    if((op->table_info->bar_type & BAR_ISSUE) && (node->node_count > 0))
      break;

    /* remove op from previous stage */
    src_sd->ops[ii] = NULL;
    src_sd->op_count--;
    ASSERT(node->proc_id, src_sd->op_count >= 0);

    /* set op fields */
    op->node_id     = node->node_count;
    op->issue_cycle = cycle_count;

    /* add to node list & update node state*/
    ASSERT(node->proc_id, !op->in_node_list);
    if(node->node_tail)
      node->node_tail->next_node = op;
    if(node->node_head == NULL)
      node->node_head = op;
    op->next_node    = NULL;
    op->in_node_list = TRUE;
    node->node_tail  = op;

    STAT_EVENT(node->proc_id, OP_ISSUED);

    if(!node->next_op_into_rs)    /* if there are no ops waiting to enter RS */
      node->next_op_into_rs = op; /* this will be the first one */

    node->node_count++;
    ASSERTM(node->proc_id, node->node_count <= NODE_TABLE_SIZE,
            "node_count: %d src_max_op_count: %d src_op_count: %d\n",
            node->node_count, src_sd->max_op_count, src_sd->op_count);

    on_path |= !op->off_path;

    DEBUG(node->proc_id, "Issuing the op op_num:%s off_path:%d\n",
          unsstr64(op->op_num), op->off_path);

    op->state = OS_ISSUED;

    /* always stop issuing after a synchronizing op */
    if(op->table_info->bar_type & BAR_ISSUE)
      break;
  }
}

/**************************************************************************************/
/* check_if_mem_blocked: Memory is blocked when there are no more MSHRs in the
 * L1 Q (i.e., there is no way to handle a D-Cache miss). This function checks
 * to see if any of the L1 MSHRs have become available.*/

void check_if_mem_blocked() {
  /* if we are stalled due to lack of MSHRs to the L1, check to see if there is
   * space now. */
  if(node->mem_blocked &&
     mem_can_allocate_req_buffer(node->proc_id, MRT_DFETCH, FALSE)) {
    node->mem_blocked = FALSE;
    STAT_EVENT(node->proc_id,
               MEM_BLOCK_LENGTH_0 + MIN2(node->mem_block_length, 5000) / 100);
    if(DIE_ON_MEM_BLOCK_THRESH) {
      if(node->proc_id == DIE_ON_MEM_BLOCK_CORE) {
        ASSERTM(node->proc_id, node->mem_block_length < DIE_ON_MEM_BLOCK_THRESH,
                "Core blocked on memory for %u cycles (%llu--%llu)\n",
                node->mem_block_length, cycle_count - node->mem_block_length,
                cycle_count);
      }
    }
    node->mem_block_length = 0;
  }
  INC_STAT_EVENT(node->proc_id, CORE_MEM_BLOCKED, node->mem_blocked);
  node->mem_block_length += node->mem_blocked;
}

/**************************************************************************************/
/* bubble: bubble pass for the ready list. Part of the bubble sort function
 * below. */

static Flag bubble(Op* head, int n) {
  Flag swapped     = FALSE;
  Flag swapped_now = FALSE;
  int  i;
  Op*  prev = NULL;
  Op*  cur  = head;
  Op*  next = head->next_rdy;

  for(i = 0; i < n - 1; i++) {
    swapped_now = FALSE;
    if(cur->op_num > next->op_num) {
      if(prev)
        prev->next_rdy = next;
      cur->next_rdy  = next->next_rdy;
      next->next_rdy = cur;
      swapped        = TRUE;
      swapped_now    = TRUE;
      if(!prev)
        node->rdy_head = next;
    }
    if(swapped_now) {
      prev = next;
      next = cur->next_rdy;
    } else {
      prev = cur;
      cur  = next;
      next = next->next_rdy;
    }
  }

  return swapped;
}

/**************************************************************************************/
/* sort_node_ready_list: sorts the ready list */

inline static void sort_node_ready_list() {
  Op* op;
  int i;
  int count_rdy = 0;
  for(op = node->rdy_head; op; op = op->next_rdy) {
    count_rdy++;
  }

  DEBUG(node->proc_id, "Unsorted ready list: count: %d\n", count_rdy);
  if(DEBUG_NODE_STAGE && DEBUG_RANGE_COND(node->proc_id)) {
    for(op = node->rdy_head; op; op = op->next_rdy) {
      DPRINTF("%s ", unsstr64(op->op_num));
    }
    DPRINTF("\n");
  }

  for(i = count_rdy; (i > 1) && bubble(node->rdy_head, i); i--)
    ;

  count_rdy = 0;
  for(op = node->rdy_head; op; op = op->next_rdy)
    count_rdy++;

  DEBUG(node->proc_id, "Sorted ready list: count: %d\n", count_rdy);
  if(DEBUG_NODE_STAGE && DEBUG_RANGE_COND(node->proc_id)) {
    for(op = node->rdy_head; op; op = op->next_rdy) {
      DPRINTF("%s ", unsstr64(op->op_num));
    }
    DPRINTF("\n");
  }
}

/**************************************************************************************/
/* Schedulers:
 *      The interface to the schedule functions is that Scarab will pass the
 * function the ready op, and the scheduler will return the selected ops in
 * node->sd. See OLDEST_FIRST_SCHED for an example. Note, it is not
 * necessary to look at FU availability in this stage, if the FU is busy,
 * then the op will be ignored and available to schedule again in the next
 * stage.
 *
 *      +OLDEST_FIRST_SCHED: will always select the oldest ready ops to schedule
 */

void oldest_first_sched(Op* op) {
  int32 youngest_slot_op_id = -1;  //-1 means not found

  // Iterate through the FUs that this RS is connected to.
  Reservation_Station* rs = &node->rs[op->rs_id];
  for(uns32 i = 0; i < rs->num_fus; ++i) {
    Func_Unit* fu    = rs->connected_fus[i];
    uns32      fu_id = fu->fu_id;

    // check if this op can be executed by this FU
    if(get_fu_type(op->table_info->op_type, op->table_info->is_simd) &
       fu->type) {
      Op* s_op = node->sd.ops[fu_id];
      if(!s_op) {  // nobody has been scheduled to this FU yet
        DEBUG(node->proc_id,
              "Scheduler selecting    op_num:%s  fu_id:%d op:%s l1:%d\n",
              unsstr64(op->op_num), fu_id, disasm_op(op, TRUE),
              op->engine_info.l1_miss);
        ASSERT(node->proc_id, fu_id < node->sd.max_op_count);
        op->fu_num                 = fu_id;
        node->sd.ops[op->fu_num]   = op;
        node->last_scheduled_opnum = op->op_num;
        node->sd.op_count += !s_op;
        ASSERT(node->proc_id, node->sd.op_count <= node->sd.max_op_count);
        youngest_slot_op_id = -1;
        break;
      } else if(op->op_num < s_op->op_num) {
        // The slot is not empty, but we are older than the op that is in the
        // slot
        if(youngest_slot_op_id == -1) {
          youngest_slot_op_id = fu_id;
        } else {
          Op* youngest_op = node->sd.ops[youngest_slot_op_id];
          if(s_op->op_num > youngest_op->op_num) {
            // this slot is younger than the youngest known op
            youngest_slot_op_id = fu_id;
          }
        }
      }
    }
  }

  if(youngest_slot_op_id != -1) {
    /*Did not find an empty slot, but we did find a slot that is younger that
     * us*/
    uns32 fu_id = youngest_slot_op_id;
    DEBUG(node->proc_id,
          "Scheduler selecting    op_num:%s  fu_id:%d op:%s l1:%d\n",
          unsstr64(op->op_num), fu_id, disasm_op(op, TRUE),
          op->engine_info.l1_miss);
    ASSERT(node->proc_id, fu_id < node->sd.max_op_count);
    op->fu_num                 = fu_id;
    node->sd.ops[op->fu_num]   = op;
    node->last_scheduled_opnum = op->op_num;
    node->sd.op_count += 0;  // replacing an op, not adding a new one.
    ASSERT(node->proc_id, node->sd.op_count <= node->sd.max_op_count);
  } else {
    /*Did not find an empty slot or a slot that is younger than me, do nothing*/
  }
}

/**************************************************************************************/
/* node_sched_ops: schedule read ops (ops that are currently in the ready list).
 *   All of the scheduling algs take the ready_list as input and produce
 * node->sd as output. node->sd are the ops that are being passed to the
 * functional units. If the FUs are availible, they will grab the op and it
 * will be removed from the ready_list. If they are not available, then the op
 * will remain in the ready_list for the next round of scheduling. There is
 * only one ready list that holds all of the ops that are ready from each of
 * the reservation stations.*/

void node_sched_ops() {
  Op* op;

  /* the next stage is supposed to clear them out, regardless of
     whether they are actually sent to a functional unit */
  ASSERT(node->proc_id, node->sd.op_count == 0);

  // Check to see if the L1 Q is (still) full
  check_if_mem_blocked();

  starlab_hash_table* map1_ptr = (starlab_hash_table*) voided_mapping_rs_instr1_to_instr2;
  if(map1_ptr == NULL){
    map1_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_mapping_entry));
  }

  starlab_hash_table* map2_ptr = (starlab_hash_table*) voided_mapping_rs_instr2_to_instr1; 
  if(map2_ptr == NULL){
    map2_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_mapping_entry)); 
  }

  starlab_hash_table* mov_mov_ptr = (starlab_hash_table*) voided_mov_mov_rs_cycles_table; 
  if(mov_mov_ptr == NULL){
    mov_mov_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry)); 
  }

  starlab_hash_table* mov_alu_ptr = (starlab_hash_table*) voided_mov_alu_rs_cycles_table; 
  if(mov_alu_ptr == NULL){ 
    mov_alu_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry)); 
  }

  starlab_hash_table* mov_jmp_ptr = (starlab_hash_table*) voided_mov_jmp_rs_cycles_table; 
  if(mov_alu_ptr == NULL){ 
    mov_alu_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry)); 
  }

  starlab_hash_table* alu_alu_ptr = (starlab_hash_table*) voided_alu_alu_rs_cycles_table; 
  if(alu_alu_ptr == NULL){ 
    alu_alu_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry));
  }

  starlab_hash_table* alu_mov_ptr = (starlab_hash_table*) voided_alu_mov_rs_cycles_table; 
  if(alu_mov_ptr == NULL){ 
    alu_mov_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry));
  }

  starlab_hash_table* alu_jmp_ptr = (starlab_hash_table*) voided_alu_jmp_rs_cycles_table; 
  if(alu_jmp_ptr == NULL){ 
    alu_jmp_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry));
  }

  starlab_hash_table* jmp_jmp_ptr = (starlab_hash_table*) voided_jmp_jmp_rs_cycles_table; 
  if(jmp_jmp_ptr == NULL){ 
    jmp_jmp_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry));
  }

  starlab_hash_table* jmp_mov_ptr = (starlab_hash_table*) voided_jmp_mov_rs_cycles_table; 
  if(jmp_mov_ptr == NULL){
    jmp_mov_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry)); 
  }

  starlab_hash_table* jmp_alu_ptr = (starlab_hash_table*) voided_jmp_alu_rs_cycles_table; 
  if(jmp_alu_ptr == NULL){
    jmp_alu_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry));
  }

  for(op = node->rdy_head; op; op = op->next_rdy) {
    ASSERT(node->proc_id, node->proc_id == op->proc_id);
    ASSERTM(node->proc_id, op->in_rdy_list, "op_num %llu\n", op->op_num);
    if(op->state == OS_WAIT_MEM) {
      if(node->mem_blocked)
        continue;
      else
        op->state = OS_READY;
    }
    if(op->state == OS_TENTATIVE || op->state == OS_WAIT_DCACHE)
      continue;
    ASSERTM(node->proc_id,
            op->state == OS_IN_RS || op->state == OS_READY ||
              op->state == OS_WAIT_FWD,
            "op_num: %llu, op_state: %s\n", op->op_num,
            Op_State_str(op->state));
    DEBUG(node->proc_id,
          "Scheduler examining    op_num:%s op:%s l1:%d st:%s rdy:%s exec:%s "
          "done:%s\n",
          unsstr64(op->op_num), disasm_op(op, TRUE), op->engine_info.l1_miss,
          Op_State_str(op->state), unsstr64(op->rdy_cycle),
          unsstr64(op->exec_cycle), unsstr64(op->done_cycle));


    /* op will be ready next cycle, try to schedule  */
    if(cycle_count >= op->rdy_cycle - 1) {
      ASSERT(node->proc_id, op->srcs_not_rdy_vector == 0x0);
      DEBUG(node->proc_id, "Scheduler considering  op_num:%s op:%s l1:%d\n",
            unsstr64(op->op_num), disasm_op(op, TRUE), op->engine_info.l1_miss);

      // Put your own scheduling algorithm here
      if(OLDEST_FIRST_SCHED) {
        oldest_first_sched(op);
      } else {
        // oldest first is currently the only scheduling algorithm
        oldest_first_sched(op);
      }
    }

    printf("[in node_sched_ops()] op address: %lld\t op type: %d\t retire cycle: %lld\n", op->inst_info->addr, op->table_info->op_type, cycle_count);

    /* here, we update the cycle count for when this op was scheduled to a FU 
       find the corresponding tuple entry of this op in either  
    */

   /* A given op can be both the first instruction in a tuple and the second 
      instruction in another tuple. Therefore, we need to update its cycle 
      count in two different tuples.
   */

    char curr_op_addr[21];
    char tuple_as_key[64], fetched_addr[21]; 

    sprintf(curr_op_addr, "%lld", op->inst_info->addr); 

    // Case 1: when op is the first instruction in a tuple
    rs_mapping_entry* map_entry = (rs_mapping_entry*) malloc(sizeof(rs_mapping_entry));
    printf("[in node_sched_ops()] Case 1: looking for address: %s\n", curr_op_addr);
    map_entry = starlab_search(map1_ptr, curr_op_addr); 
    if(map_entry){
      printf("[in node_sched_ops()] Case 1: found address: %s\n", curr_op_addr);
      strcpy(fetched_addr, map_entry->instr2); 
      sprintf(tuple_as_key, "%s%s", curr_op_addr, fetched_addr); 
    

      unsigned int fetched_op_type = map_entry->instr2_op_type; 
      if(map_entry->instr1_op_type == op->table_info->op_type){

      // <MOV, MOV> 
      if(op->table_info->op_type == 3 && fetched_op_type == 3){
        rs_cycles_entry* temp_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry));
        temp_entry = starlab_search(mov_mov_ptr, tuple_as_key); 

        if(temp_entry){
          temp_entry->instr1_rs_issue_to_fu_cycle = cycle_count; 
        }
        }

      // <MOV, ALU>
      else if(op->table_info->op_type == 3 && is_alu_op(fetched_op_type)){
        rs_cycles_entry* temp_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry));
        temp_entry = starlab_search(mov_alu_ptr, tuple_as_key);
        if(temp_entry){
          temp_entry->instr1_rs_issue_to_fu_cycle = cycle_count; 
        }
      }

      // <MOV, JMP>
      else if(op->table_info->op_type == 3 && fetched_op_type == 2){
         rs_cycles_entry* temp_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry));
         temp_entry = starlab_search(mov_jmp_ptr, tuple_as_key);
          if(temp_entry){
          temp_entry->instr1_rs_issue_to_fu_cycle = cycle_count; 
        }
      }

      // <ALU, ALU>
      else if( (is_alu_op(op->table_info->op_type)) && is_alu_op(fetched_op_type)){
        rs_cycles_entry* temp_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry));
        temp_entry =  starlab_search(alu_alu_ptr, tuple_as_key);
         if(temp_entry){
          temp_entry->instr1_rs_issue_to_fu_cycle = cycle_count; 
        }
      }
           

      // <ALU, MOV>
      else if((is_alu_op(op->table_info->op_type)) && fetched_op_type == 3){
        rs_cycles_entry* temp_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry));
        temp_entry = starlab_search(alu_mov_ptr, tuple_as_key);
         if(temp_entry){
          temp_entry->instr1_rs_issue_to_fu_cycle = cycle_count; 
          
                    printf("[In node_sched_ops()] <ALU, JMP> after updating FU issue count:\n");
                    printf("Current cycle count: %lld\n", cycle_count);
                    printf("  instr1_addr: %s\n", temp_entry->instr1_addr);
                    printf("  instr2_addr: %s\n", temp_entry->instr2_addr);
                    printf("  instr1_rs_insertion_cycle: %lld\n", temp_entry->instr1_rs_insertion_cycle);
                    printf("  instr2_rs_insertion_cycle: %lld\n", temp_entry->instr2_rs_insertion_cycle);
                    printf("  instr1_rs_issue_to_fu_cycle: %lld\n", temp_entry->instr1_rs_issue_to_fu_cycle);
                    printf("  instr2_rs_issue_to_fu_cycle: %lld\n", temp_entry->instr2_rs_issue_to_fu_cycle);
        }
      }          

      // <ALU, JMP>
      else if((is_alu_op(op->table_info->op_type)) && fetched_op_type == 2)
      {
        // rs_cycles_entry* temp_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry));
        // temp_entry = starlab_search(alu_jmp_ptr, tuple_as_key);
        rs_cycles_entry* temp_entry = starlab_search(alu_jmp_ptr, tuple_as_key);
         if(temp_entry){
          temp_entry->instr1_rs_issue_to_fu_cycle = cycle_count; 
          printf("[in node_sched_ops()] updated %s issue to %lld, current cycle count: %lld\n", temp_entry->instr1_addr, temp_entry->instr1_rs_issue_to_fu_cycle, cycle_count);
        }
      }           

      // <JMP, JMP>
      else if(op->table_info->op_type == 2 && fetched_op_type == 2){
        rs_cycles_entry* temp_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry));
        temp_entry =  starlab_search(jmp_jmp_ptr, tuple_as_key);
         if(temp_entry){
          temp_entry->instr1_rs_issue_to_fu_cycle = cycle_count; 
        }
      }           

      // <JMP, MOV>
      else if(op->table_info->op_type == 2 && fetched_op_type == 3){
        rs_cycles_entry* temp_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry));
        temp_entry = starlab_search(jmp_mov_ptr, tuple_as_key);
         if(temp_entry){
          temp_entry->instr1_rs_issue_to_fu_cycle = cycle_count; 
        }
      }

      // <JMP, ALU> 
      else if(op->table_info->op_type == 2 && is_alu_op(fetched_op_type)){
        rs_cycles_entry* temp_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry));
        temp_entry = starlab_search(jmp_alu_ptr, tuple_as_key);
         if(temp_entry){
          temp_entry->instr1_rs_issue_to_fu_cycle = cycle_count; 
        }
      }

      }

    }

    // Case 2: when the current op is a second instruction in a tuple (both cases likely exist, except if this is the first op)
    // if an entry is found, we know that this op is the second instruction in the tuple 
    rs_mapping_entry* new_map_entry = (rs_mapping_entry*) malloc(sizeof(rs_mapping_entry)); 
    printf("[in node_sched_ops()] Case 2: looking for address: %s\n", curr_op_addr);
    new_map_entry = starlab_search(map2_ptr, curr_op_addr); 

    if(new_map_entry){
       printf("[in node_sched_ops()] Case 2: found address: %s\n", curr_op_addr);
      // since this is the second instruction, we need to fetch the first instruction
      strcpy(fetched_addr, new_map_entry->instr1); 
      sprintf(tuple_as_key, "%s%s", fetched_addr, curr_op_addr ); 

      printf("In case 2, op addr: %lld, and cycle count: %lld op type: %d\n", op->inst_info->addr, cycle_count, op->table_info->op_type);

      unsigned int fetched_op_type = new_map_entry->instr1_op_type; 
      printf("fetched op type: %d\n", fetched_op_type);
      // if(new_map_entry->instr2_op_type == op->table_info->op_type){

      // <MOV, MOV> 
      if(op->table_info->op_type == 3 && fetched_op_type == 3){
        printf("[In case 2: <MOV, MOV>]");
        rs_cycles_entry* map2_cycles_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry)); 
        map2_cycles_entry = starlab_search(mov_mov_ptr, tuple_as_key); 

        if(map2_cycles_entry){
          map2_cycles_entry->instr2_rs_issue_to_fu_cycle = cycle_count; 
        }
        } 

      // <MOV, ALU>
        else if(op->table_info->op_type == 3 && is_alu_op(fetched_op_type)){
        printf("[In case 2: <MOV, ALU>]");
        rs_cycles_entry* map2_cycles_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry)); 
        map2_cycles_entry = starlab_search(mov_alu_ptr, tuple_as_key); 
        if(map2_cycles_entry){
          map2_cycles_entry->instr2_rs_issue_to_fu_cycle = cycle_count; 
        }
      }

      // <MOV, JMP>
      else if(op->table_info->op_type == 3 && fetched_op_type == 2){

         rs_cycles_entry* map2_cycles_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry)); 
        map2_cycles_entry = starlab_search(mov_jmp_ptr, tuple_as_key); 
          if(map2_cycles_entry){
          map2_cycles_entry->instr2_rs_issue_to_fu_cycle = cycle_count; 
        }
      }

      // <ALU, ALU>
       else if( (is_alu_op(op->table_info->op_type)) && is_alu_op(fetched_op_type)){

        printf("in <ALU, ALU> op addr: %lld\n", op->inst_info->addr);
        rs_cycles_entry* map2_cycles_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry)); 
        map2_cycles_entry = starlab_search(alu_alu_ptr, tuple_as_key); 
         if(map2_cycles_entry){
          printf("before changing the instr2 FU issue cycle: %lld\n", map2_cycles_entry->instr2_rs_issue_to_fu_cycle); 
          map2_cycles_entry->instr2_rs_issue_to_fu_cycle = cycle_count; 
        }
      }

      // <ALU, MOV>
       else if((is_alu_op(op->table_info->op_type)) && fetched_op_type == 3){

        rs_cycles_entry* map2_cycles_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry)); 
        map2_cycles_entry = starlab_search(alu_mov_ptr, tuple_as_key); 
         if(map2_cycles_entry){
          map2_cycles_entry->instr2_rs_issue_to_fu_cycle = cycle_count; 
        }
      }

      // <ALU, JMP>
         else if((is_alu_op(op->table_info->op_type)) && fetched_op_type == 2)
      {
        printf("[in node_sched_ops()] <ALU, JMP>\n");
        rs_cycles_entry* map2_cycles_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry)); 
        map2_cycles_entry = starlab_search(alu_jmp_ptr, tuple_as_key); 
         if(map2_cycles_entry){
          map2_cycles_entry->instr2_rs_issue_to_fu_cycle = cycle_count; 
          printf("[in node_sched_ops()] updated %s issue to %lld\n", map2_cycles_entry->instr2_addr, map2_cycles_entry->instr2_rs_issue_to_fu_cycle);
        }
      }

      // <JMP, JMP>
      else if(op->table_info->op_type == 2 && fetched_op_type == 2){

        rs_cycles_entry* map2_cycles_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry)); 
        map2_cycles_entry = starlab_search(jmp_jmp_ptr, tuple_as_key); 
         if(map2_cycles_entry){
          map2_cycles_entry->instr2_rs_issue_to_fu_cycle = cycle_count; 
        }
      }

      // <JMP, MOV>
      else if(op->table_info->op_type == 2 && fetched_op_type == 3){

        rs_cycles_entry* map2_cycles_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry)); 
        map2_cycles_entry = starlab_search(jmp_mov_ptr, tuple_as_key); 
         if(map2_cycles_entry){
          map2_cycles_entry->instr2_rs_issue_to_fu_cycle = cycle_count; 
        }
      }

      // <JMP, ALU> 
        else if(op->table_info->op_type == 2 && is_alu_op(fetched_op_type)){
        rs_cycles_entry* map2_cycles_entry = (rs_cycles_entry*) malloc(sizeof(rs_cycles_entry)); 
        map2_cycles_entry = starlab_search(jmp_alu_ptr, tuple_as_key); 
         if(map2_cycles_entry){
          map2_cycles_entry->instr2_rs_issue_to_fu_cycle = cycle_count; 
        }
      }

      // }

    }

   }

  voided_mapping_rs_instr1_to_instr2 = (void*) map1_ptr; 
  voided_mapping_rs_instr2_to_instr1 = (void*) map2_ptr; 

  voided_mov_mov_rs_cycles_table = (void*) mov_mov_ptr; 
  voided_mov_alu_rs_cycles_table = (void*) mov_alu_ptr; 
  voided_mov_jmp_rs_cycles_table = (void*) mov_jmp_ptr; 

  voided_alu_alu_rs_cycles_table = (void*) alu_alu_ptr; 
  voided_alu_mov_rs_cycles_table = (void*) alu_mov_ptr; 
  voided_alu_jmp_rs_cycles_table = (void*) alu_jmp_ptr; 

  voided_jmp_jmp_rs_cycles_table = (void*) jmp_jmp_ptr; 
  voided_jmp_mov_rs_cycles_table = (void*) jmp_mov_ptr; 
  voided_jmp_alu_rs_cycles_table = (void*) jmp_alu_ptr; 
  }




/**************************************************************************************/
/* node_retire:*/

void node_retire() {
  uns ret_count = 0;
  Op* op        = NULL;

  // If node table is empty, then there is nothing to retire
  if(is_node_table_empty())
    return;

  // Iterate through the first NODE_RET_WIDTH number of ops and try to retire
  // them
  for(op = node->node_head; op && ret_count < NODE_RET_WIDTH;
      op = op->next_node) {
    ASSERT(node->proc_id, node->proc_id == op->proc_id);

    // check to see if the head of the node table is ready to retire
    if(op_not_ready_for_retire(op)) {
      // op is not ready to retire
      collect_not_ready_to_retire_stats(op);
      break;
    }

    rob_stall_reason = ROB_STALL_NONE;

    /**op is ready to retire**/
    ASSERTM(node->proc_id, op->state != OS_TENTATIVE, "op_num: %llu\n",
            op->op_num);
    ret_count++;
    DEBUG(node->proc_id, "Retiring op:%llu\n", op->op_num);

    // Debug prints mainly used for testing the uop generation of PIN frontend
    debug_print_retired_uop(op);

    // count number of stall cycles
    STAT_EVENT(node->proc_id,
               RET_STALL_LENGTH_0 + MIN2(node->ret_stall_length, 5000) / 100);
    if(DIE_ON_RET_STALL_THRESH) {
      // time out code
      if(node->proc_id == DIE_ON_RET_STALL_CORE) {
        ASSERTM(node->proc_id, node->ret_stall_length < DIE_ON_RET_STALL_THRESH,
                "Retire stalled for %u cycles (%llu--%llu)\n",
                node->ret_stall_length, cycle_count - node->ret_stall_length,
                cycle_count);
      }
    }
    node->ret_stall_length = 0;

    // retire the ops
    Counter real_rdy_cycle = MAX2(op->rdy_cycle, op->issue_cycle);

    ASSERT(node->proc_id, node->proc_id == op->proc_id);
    ASSERT(node->proc_id, op->in_node_list);
    ASSERT(node->proc_id, !op->off_path);
    STAT_EVENT(op->proc_id,
               OP_WAIT_0 + MIN2(op->sched_cycle - real_rdy_cycle, 31));
    STAT_EVENT(op->proc_id, OP_RETIRED);  // Counts all ops retired, not just
                                          // those in primary thread

    DEBUG(node->proc_id, "Retiring op_num:%s\n", unsstr64(op->op_num));

    ASSERTM(node->proc_id, op->op_num == node->ret_op, "op_num=%s  ret_op=%s\n",
            unsstr64(op->op_num), unsstr64(node->ret_op));

    if(op->eom) {
      /*We need to retire sys calls, bar fetch instructions, and the last
       * instruction. All other retires are "optional" to release resources in
       * the PIN frontend*/
      inst_count[node->proc_id]++;
      STAT_EVENT(op->proc_id, NODE_INST_COUNT);

      if(op->fetched_instruction) {
        inst_count_fetched[node->proc_id]++;
        STAT_EVENT(op->proc_id, NODE_INST_COUNT_FETCHED);
      }

      Flag retire_op = IS_CALLSYS(op->table_info) ||
                       op->table_info->bar_type & BAR_FETCH ||
                       (inst_count[node->proc_id] % NODE_RETIRE_RATE == 0);

      if(op->exit) {
        retired_exit[op->proc_id] = TRUE;
        decoupled_fe_retire(op, op->proc_id, -1);
      } else if(retire_op) {
        decoupled_fe_retire(op, op->proc_id, op->inst_uid);
      }
    }
    uop_count[node->proc_id]++;
    STAT_EVENT(op->proc_id, NODE_UOP_COUNT);
    ASSERTM(node->proc_id, uop_count[node->proc_id] == node->ret_op,
            "%s  %s op_num: %s\n", unsstr64(uop_count[node->proc_id]),
            unsstr64(node->ret_op), unsstr64(op->op_num));

    node->ret_op++;

    STAT_EVENT(op->proc_id, RET_ALL_INST);

    remove_from_seq_op_list(td, op);

    if(op->table_info->cf_type) {
      if(BP_UPDATE_AT_RETIRE) {
        // this code updates the branch prediction structures
        if(op->table_info->cf_type >= CF_IBR)
          bp_target_known_op(g_bp_data, op);

        bp_resolve_op(g_bp_data, op);
      }
      bp_retire_op(g_bp_data, op);
    }

    if(op->table_info->mem_type == MEM_LD &&
       (op->done_cycle - op->sched_cycle) < 5) {
      STAT_EVENT(op->proc_id,
                 LD_EXEC_CYCLES_0 + (op->done_cycle - op->sched_cycle));
    }
    if(op->table_info->mem_type == MEM_LD) {
      STAT_EVENT(op->proc_id, LD_NO_DEPENDENTS + (op->wake_up_head ? 1 : 0));
    }
    STAT_EVENT(op->proc_id, RET_OP_EXEC_COUNT_0 + MIN2(32, op->exec_count));

    op->retire_cycle = cycle_count;

    // free the previous register entries with same architectural destination
    rename_table_commit(op);

    if(model->op_retired_hook)
      model->op_retired_hook(op);
    else
      free_op(op);

    node->node_count--;
    ASSERT(node->proc_id, node->node_count >= 0);
  }

  STAT_EVENT(node->proc_id, ROW_SIZE_0 + ret_count);

  // op should be pointing to first op that was not retired because of the above
  // for-loop
  node->node_head = op;
  if(node->node_head)
    DEBUG(node->proc_id, "Op op_num:%s is now head of the node table\n",
          unsstr64(node->node_head->op_num));
  if(op == NULL) {
    node->node_tail = NULL;
    ASSERTM(node->proc_id, node->node_count == 0,
            "Node table must be empty if next node is null!\n");
  }
}


/**************************************************************************************/
/* Issuers:
 *      The interface to the issue functions is that Scarab will pass the
 * function the op to be issued, and the issuer will return the RS id that the
 * op should be issued to, or -1 meaning that there is no RS for the op to
 * be issued to. See FIND_EMPTIEST_RS for an example.
 *
 *      +FIND_EMPTIEST_RS: will always select the RS with the most empty slots
 */
int64 find_emptiest_rs(Op* op) {
  int64 emptiest_rs_id    = -1;
  int64 emptiest_rs_slots = -1;

  /*Iterate through RSs looking for an available RS that is connected
    to an FU that can execute the OP.*/
  for(int64 rs_id = 0; rs_id < NUM_RS; ++rs_id) {
    Reservation_Station* rs = &node->rs[rs_id];
    ASSERT(node->proc_id, !rs->size || rs->rs_op_count <= rs->size);
    ASSERTM(node->proc_id, rs->size,
            "Infinite RS not suppoted by find_emptiest_rs issuer.");
    for(uns32 i = 0; i < rs->num_fus; ++i) {
      Func_Unit* fu = rs->connected_fus[i];

      // This FU can execute this op
      if(get_fu_type(op->table_info->op_type, op->table_info->is_simd) &
         fu->type) {
        // Find the emptiest RS
        int32 num_empty_slots = rs->size - rs->rs_op_count;
        if(num_empty_slots != 0) {
          if(emptiest_rs_slots < num_empty_slots) {
            // Found a new emptiest rs
            emptiest_rs_id    = rs_id;
            emptiest_rs_slots = num_empty_slots;
          }
        }
      }
    }
  }

  return emptiest_rs_id;
}

/**************************************************************************************/
/* node_fill_rs: fill the scheduling window (RS) with oldest available ops.
 * Adding ops to their reservation stations. If they are ready, also add them to
 * the ready list.*/

void node_fill_rs() {
  int64 rs_id;
  Op*   op          = NULL;
  uns32 num_fill_rs = 0;

  starlab_hash_table* metadata_ptr = (starlab_hash_table*) voided_metadata_rs_cycles_table; 
  if(metadata_ptr == NULL){ 
    metadata_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_prev_op)); 
  }

  starlab_hash_table* map1_ptr = (starlab_hash_table*) voided_mapping_rs_instr1_to_instr2;
  if(map1_ptr == NULL){
    map1_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_mapping_entry));
  }

  starlab_hash_table* map2_ptr = (starlab_hash_table*) voided_mapping_rs_instr2_to_instr1; 
  if(map2_ptr == NULL){
    map2_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_mapping_entry)); 
  }

  starlab_hash_table* mov_mov_ptr = (starlab_hash_table*) voided_mov_mov_rs_cycles_table; 
  if(mov_mov_ptr == NULL){
    mov_mov_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry)); 
  }

  starlab_hash_table* mov_alu_ptr = (starlab_hash_table*) voided_mov_alu_rs_cycles_table; 
  if(mov_alu_ptr == NULL){ 
    mov_alu_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry)); 
  }

  starlab_hash_table* mov_jmp_ptr = (starlab_hash_table*) voided_mov_jmp_rs_cycles_table; 
  if(mov_alu_ptr == NULL){ 
    mov_alu_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry)); 
  }

  starlab_hash_table* alu_alu_ptr = (starlab_hash_table*) voided_alu_alu_rs_cycles_table; 
  if(alu_alu_ptr == NULL){ 
    alu_alu_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry));
  }

  starlab_hash_table* alu_mov_ptr = (starlab_hash_table*) voided_alu_mov_rs_cycles_table; 
  if(alu_mov_ptr == NULL){ 
    alu_mov_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry));
  }

  starlab_hash_table* alu_jmp_ptr = (starlab_hash_table*) voided_alu_jmp_rs_cycles_table; 
  if(alu_jmp_ptr == NULL){ 
    alu_jmp_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry));
  }

  starlab_hash_table* jmp_jmp_ptr = (starlab_hash_table*) voided_jmp_jmp_rs_cycles_table; 
  if(jmp_jmp_ptr == NULL){ 
    jmp_jmp_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry));
  }

  starlab_hash_table* jmp_mov_ptr = (starlab_hash_table*) voided_jmp_mov_rs_cycles_table; 
  if(jmp_mov_ptr == NULL){
    jmp_mov_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry));
  }

  starlab_hash_table* jmp_alu_ptr = (starlab_hash_table*) voided_jmp_alu_rs_cycles_table; 
  if(jmp_alu_ptr == NULL){
    jmp_alu_ptr = starlab_create_table(INITIAL_TABLE_SIZE, sizeof(rs_cycles_entry));
  }

  // Scan through issued nodes in node table that have not been issued to RS
  // yet.
  for(op = node->next_op_into_rs; op; op = op->next_node) {

    printf("[in node_fill_rs()]: op with addr: %lld\t and op type: %d\t cycle: %lld\n", op->inst_info->addr, op->table_info->op_type, cycle_count);
    // Put your own issue functions here.
    if(FIND_EMPTIEST_RS) {
      rs_id = find_emptiest_rs(op);
    } else {
      // FIND_EMPTIEST_RS is currently the only issuer.
      rs_id = find_emptiest_rs(op);
    }

    if(rs_id == -1)
      break;

    Reservation_Station* rs = &node->rs[rs_id];
    ASSERT(node->proc_id, rs_id < NUM_RS);
    ASSERTM(node->proc_id, !rs->size || rs->rs_op_count < rs->size,
            "There must be at least one free space in selected RS!\n");

    ASSERT(node->proc_id, op->state == OS_ISSUED);
    op->state = OS_IN_RS;
    op->rs_id = (Counter)rs_id;
    rs->rs_op_count++;
    num_fill_rs++;
    DEBUG(node->proc_id, "Filling %s with op_num:%s (%d)\n", rs->name,
          unsstr64(op->op_num), rs->rs_op_count);
    if(op->srcs_not_rdy_vector == 0) {
      /* op is ready to issue right now */
      DEBUG(node->proc_id, "Adding to ready list  op_num:%s op:%s l1:%d\n",
            unsstr64(op->op_num), disasm_op(op, TRUE), op->engine_info.l1_miss);
      op->state = (cycle_count + 1 >= op->rdy_cycle ? OS_READY : OS_WAIT_FWD);
      op->next_rdy    = node->rdy_head;
      node->rdy_head  = op;
      op->in_rdy_list = TRUE;
    }

    char temp_addr[21];
    char curr_addr_as_string[21]; 
    sprintf(curr_addr_as_string, "%lld", op->inst_info->addr);

   if (is_first_op) 
   {

      printf("[in node_fill_rs()] This is the first op\n");
      // Track the first op type and address
      char address[21]; 
      sprintf(address, "%lld", op->inst_info->addr);
      rs_prev_op* prev_op = (rs_prev_op*)malloc(sizeof(rs_prev_op));
      if(prev_op){
        strcpy(prev_op->prev_op_addr, address);
        prev_op->prev_op_type = op->table_info->op_type; 
        prev_op->prev_op_rs_insert_cycle = cycle_count;
        starlab_insert(metadata_ptr, "prev_op", &prev_op);
      }
      is_first_op = false;
  } 

  else 
  {

      // There is a prev op since this is not the first op 
      rs_prev_op* prev_op = (rs_prev_op*)malloc(sizeof(rs_prev_op));
      // whatever fetched (at least for the first time), should match 
      // whatever is inserted in the "if" condition
      prev_op = starlab_search(metadata_ptr, "prev_op");
      printf("[in node_fill_rs()] printing the prev_op fetched values.\n"); 
      printf("Prev op addr: %s\n", prev_op->prev_op_addr); 
      printf("Prev op type: %d\n", prev_op->prev_op_type); 
      printf("Prev op insertion cycle: %d\n", prev_op->prev_op_rs_insert_cycle);

      // if prev_op search was successful
      if (prev_op) 
      {      
           char instr_tuple_as_key[42];  
           sprintf(instr_tuple_as_key, "%s%s", prev_op->prev_op_addr, curr_addr_as_string);
            
          /* In node_sched_ops(), given an op address, we need to fetch the corresponding
              tuple, so that we can update the cycle count when it was scheduled 
              to a given functional unit (FU). It is possible that a given op is the first 
              op in a tuple 1 and the second op in a tuple 2. Therefore, 
              we need to updated the op's issue_to_fu_cycle correctly in both places. 
              To do so, we will need to access the correct entries from the hashtable, 
              using the tuple key, and hence we create two different mapping tables, which 
              store an op and its partner op in the tuple, for both cases when an instruction 
              is first op in a tuple vs second op in a tuple. 

              Note that: A case when a tuple is second op in a tuple, it would not be with 
              the same op where it was the first op. Therefore, insertion into mapping table 
              2 needs to handle this case. 

              For example, 
              <op 1> 
              <op 2> 
              <op 3> 

              Tuples: <op1, op2>, <op2, op3>; in this case, op2 was the second op along with 
              op1 and it was the first op along with op3. Since op2 will be scheduled to a 
              FU only once, we will need to update op2's issue to FU count correctly in both 
              these tuples. 

              Map 1: 
              Key: op1     Value: op1, op2 
              key: op2     Value: op2, op3
              Map 2: 
              Key op2      Value: op1, op2

              If a given op was found in map 2, that means it is the second op, and therefore 
              we update instr2 cycle count. We also look at map 1 and find its entry when op2 
              was the first instr, and update its cycle count.
          */

          // If an op address is already present, then just update its contents 
          rs_mapping_entry* map1_entry = (rs_mapping_entry*) malloc(sizeof(rs_mapping_entry));
          map1_entry = starlab_search(map1_ptr, prev_op->prev_op_addr); 
          rs_mapping_entry* map2_entry = (rs_mapping_entry*) malloc(sizeof(rs_mapping_entry));
          map2_entry = starlab_search(map2_ptr, curr_addr_as_string); 
           if(map1_entry)
           {
             // address already exists, just update it 
             strcpy(map1_entry->instr1, prev_op->prev_op_addr); 
             strcpy(map1_entry->instr2, curr_addr_as_string); 
             map1_entry->instr1_op_type = prev_op->prev_op_type; 
             map1_entry->instr2_op_type = op->table_info->op_type; 

             // say prev_op was op1 and curr op is op2 
             // we will need to create map2 entry here itself? 
             if(map2_entry)
             {
              strcpy(map2_entry->instr1, prev_op->prev_op_addr); 
              strcpy(map2_entry->instr2, curr_addr_as_string); 
              map2_entry->instr1_op_type = prev_op->prev_op_type; 
              map2_entry->instr2_op_type = op->table_info->op_type; 
             }

           }

           if((map1_entry == NULL) && (map2_entry == NULL))
           {
             rs_mapping_entry* new_entry = (rs_mapping_entry*)malloc(sizeof(rs_mapping_entry));
             if(new_entry)
             {
               strcpy(new_entry->instr1, prev_op->prev_op_addr);
               strcpy(new_entry->instr2, curr_addr_as_string);
               new_entry->instr1_op_type = prev_op->prev_op_type; 
               new_entry->instr2_op_type = op->table_info->op_type; 
               starlab_insert(map1_ptr, prev_op->prev_op_addr, &new_entry);
               starlab_insert(map2_ptr, curr_addr_as_string, &new_entry); 
             }     
             
           }

            // Allocate and initialize RS entry
            rs_cycles_entry* rs_entry = (rs_cycles_entry*)malloc(sizeof(rs_cycles_entry));
            if (rs_entry)
            {

                  strcpy(rs_entry->instr1_addr, prev_op->prev_op_addr); 
                  strcpy(rs_entry->instr2_addr, curr_addr_as_string); 
                  rs_entry->instr1_rs_insertion_cycle = prev_op->prev_op_rs_insert_cycle;
                  rs_entry->instr2_rs_insertion_cycle = cycle_count;
                  rs_entry->instr1_rs_issue_to_fu_cycle = 0;
                  rs_entry->instr2_rs_issue_to_fu_cycle = 0; 
                  strcpy(temp_addr, curr_addr_as_string);
                  printf("Created rs_entry:\n");
                  printf("  instr1_addr: %s\n", rs_entry->instr1_addr);
                  printf("  instr2_addr: %s\n", rs_entry->instr2_addr);
                  printf("  instr1_rs_insertion_cycle: %lld\n", rs_entry->instr1_rs_insertion_cycle);
                  printf("  instr2_rs_insertion_cycle: %lld\n", rs_entry->instr2_rs_insertion_cycle);
                  printf("  instr1_rs_issue_to_fu_cycle: %lld\n", rs_entry->instr1_rs_issue_to_fu_cycle);
                  printf("  instr2_rs_issue_to_fu_cycle: %lld\n", rs_entry->instr2_rs_issue_to_fu_cycle);

                  // <MOV, MOV>
                if (prev_op->prev_op_type == 3 && op->table_info->op_type == 3) {
                
                      printf("inserting into mov mov\n");
                      starlab_insert(mov_mov_ptr, instr_tuple_as_key, rs_entry);
                  } 
                  // <MOV, ALU>
                  else if (prev_op->prev_op_type == 3 && is_alu_op(op->table_info->op_type)) 
                  {
                    
                    printf("inserting into mov alu\n");
                    starlab_insert(mov_alu_ptr, instr_tuple_as_key, rs_entry);
                  } 
                  // <MOV, JMP>
                  else if (prev_op->prev_op_type == 3 && op->table_info->op_type == 2) {
                    
                      printf("inserting into mov jmp\n");
                      starlab_insert(mov_jmp_ptr, instr_tuple_as_key, rs_entry);
                  } 
                  
                  // <ALU, ALU>
                  else if (is_alu_op(prev_op->prev_op_type) && is_alu_op(op->table_info->op_type)) {
                    
                      printf("inserting into alu alu\n");
                      starlab_insert(alu_alu_ptr, instr_tuple_as_key, rs_entry);
                  } 
                  
                  // <ALU, MOV>
                  else if (is_alu_op(prev_op->prev_op_type) && op->table_info->op_type == 3) {
                    
                      printf("insreting into alu mov\n");
                      starlab_insert(alu_mov_ptr, instr_tuple_as_key, rs_entry);
                  } 
                              
                // <ALU, JMP>
                else if (is_alu_op(prev_op->prev_op_type) && op->table_info->op_type == 2) {
                  
                    printf("inserting into alu jmp\n");
                    starlab_insert(alu_jmp_ptr, instr_tuple_as_key, rs_entry);

                } 
                
                // <JMP, JMP>
                else if (prev_op->prev_op_type == 2 && op->table_info->op_type == 2) {
                    
                    printf("inserting into jmp jmp\n");
                    starlab_insert(jmp_jmp_ptr, instr_tuple_as_key, rs_entry);
                } 
                
                // <JMP, MOV>
                else if (prev_op->prev_op_type == 2 && op->table_info->op_type == 3) {
                    
                    printf("inserting into jmp mov\n");
                    printf("tuple: %s\n", instr_tuple_as_key);
                    starlab_insert(jmp_mov_ptr, instr_tuple_as_key, rs_entry);
                } 
                              
                // <JMP, ALU>
                else if (prev_op->prev_op_type == 2 && is_alu_op(op->table_info->op_type)) {
                  
                    printf("inserting into jmp alu\n");
                    starlab_insert(jmp_alu_ptr, instr_tuple_as_key, rs_entry);
                } 
              
                else {
                    // do nothing
                }
             
          }
                      
                    
       }

    }

   // Update the current op as the previous op
   rs_prev_op* prev_op = (rs_prev_op*)malloc(sizeof(rs_prev_op));
   prev_op = starlab_search(metadata_ptr, "prev_op");

   // Print details of prev_op before updating
    if (prev_op) {
        printf("[in node_fill_rs()] Before updating prev_op:\n");
        printf("  prev_op_addr: %s\n", prev_op->prev_op_addr);
        printf("  prev_op_type: %d\n", prev_op->prev_op_type);
        printf("  prev_op_rs_insert_cycle: %lld\n", prev_op->prev_op_rs_insert_cycle);
    } else {
        printf("[in node_fill_rs()] prev_op is NULL\n");
    }

    // Update prev_op
    strcpy(temp_addr, curr_addr_as_string);
    strcpy(prev_op->prev_op_addr, temp_addr);
    prev_op->prev_op_type = op->table_info->op_type;
    prev_op->prev_op_rs_insert_cycle = cycle_count;

    // Print details of prev_op after updating
    printf("[in node_fill_rs()] After updating prev_op:\n");
    printf("  prev_op_addr: %s\n", prev_op->prev_op_addr);
    printf("  prev_op_type: %d\n", prev_op->prev_op_type);
    printf("  prev_op_rs_insert_cycle: %lld\n", prev_op->prev_op_rs_insert_cycle);
   

   // This is the max number of ops we can fill into the RS per cycle.
    // 0 means infinite.
    if(RS_FILL_WIDTH && (num_fill_rs == RS_FILL_WIDTH))
      break;

  }

  voided_metadata_rs_cycles_table = (void*) metadata_ptr; 

  voided_mapping_rs_instr1_to_instr2 = (void*) map1_ptr; 
  voided_mapping_rs_instr2_to_instr1 = (void*) map2_ptr; 

  voided_mov_mov_rs_cycles_table = (void*) mov_mov_ptr; 
  voided_mov_alu_rs_cycles_table = (void*) mov_alu_ptr; 
  voided_mov_jmp_rs_cycles_table = (void*) mov_jmp_ptr; 

  voided_alu_alu_rs_cycles_table = (void*) alu_alu_ptr; 
  voided_alu_mov_rs_cycles_table = (void*) alu_mov_ptr; 
  voided_alu_jmp_rs_cycles_table = (void*) alu_jmp_ptr; 

  voided_jmp_jmp_rs_cycles_table = (void*) jmp_jmp_ptr; 
  voided_jmp_mov_rs_cycles_table = (void*) jmp_mov_ptr; 
  voided_jmp_alu_rs_cycles_table = (void*) jmp_alu_ptr; 


  // had to stop issuing, this is the next node that should be issued to the RS
  node->next_op_into_rs = op;

}

/**************************************************************************************/
/* node_handle_scheduled_ops: Once the op is scheduled (i.e., going from RS to
 * FUs), we need to remove scheduled ops from the RS and ready queue */

void node_handle_scheduled_ops() {
  /* this traversal could be made more efficient since we know what
     ops we tried to schedule last cycle, but for now let's look at
     the whole ready list */
  Op** last = &node->rdy_head;
  for(Op* op = node->rdy_head; op; op = op->next_rdy) {
    if(op->state == OS_SCHEDULED || op->state == OS_MISS) {
      DEBUG(node->proc_id,
            "Removing from RS (and ready list)  op_num:%s op:%s l1:%d\n",
            unsstr64(op->op_num), disasm_op(op, TRUE), op->engine_info.l1_miss);
      *last           = op->next_rdy;
      op->in_rdy_list = FALSE;
      ASSERT(node->proc_id, node->rs[op->rs_id].rs_op_count > 0);
      node->rs[op->rs_id].rs_op_count--;
    } else {
      last = &op->next_rdy;
    }
  }
}

/**************************************************************************************/
/* is_node_stage_stalled: returns TRUE if node table is full and there are no
 * ready ops */

Flag is_node_stage_stalled() {
  return (node->node_count == NODE_TABLE_SIZE) && /* node table is full */
         !node->rdy_head &&                       /* no ready ops */
         !node->next_op_into_rs; /* no ops waiting to enter RS */
}

void debug_print_retired_uop(Op* op) {
  PRINT_RETIRED_UOP(node->proc_id, "============================\n");
  PRINT_RETIRED_UOP(node->proc_id, "EIP: 0x%llx\n", op->inst_info->addr);
  PRINT_RETIRED_UOP(node->proc_id, "Op Type: %s\n",
                    Op_Type_str(op->table_info->op_type));
  PRINT_RETIRED_UOP(node->proc_id, "Mem Type: %d\n", op->table_info->mem_type);
  PRINT_RETIRED_UOP(node->proc_id, "CF Type: %d\n", op->table_info->cf_type);
  PRINT_RETIRED_UOP(node->proc_id, "Barrier Type: %d\n",
                    op->table_info->bar_type);
  PRINT_RETIRED_UOP(node->proc_id, "Is SIMD: %d\n", op->table_info->is_simd);
  PRINT_RETIRED_UOP(node->proc_id, "Srcs: ");
  for(uns i = 0; i < op->table_info->num_src_regs; ++i) {
    PRINT_RETIRED_UOP(node->proc_id, "%s ",
                      disasm_reg(op->inst_info->srcs[i].id));
  }
  PRINT_RETIRED_UOP(node->proc_id, "\n");
  PRINT_RETIRED_UOP(node->proc_id, "Dests: ");
  for(uns i = 0; i < op->table_info->num_dest_regs; ++i) {
    PRINT_RETIRED_UOP(node->proc_id, "%s ",
                      disasm_reg(op->inst_info->dests[i].id));
  }
  PRINT_RETIRED_UOP(node->proc_id, "\n");
}

Flag op_not_ready_for_retire(Op* op) {
  return !(op->state == OS_DONE || OP_DONE(op)) || op->off_path ||
         op->recovery_scheduled || op->redirect_scheduled;
}

Flag is_node_table_empty() {
  if(node->node_count == 0) {
    ASSERT(node->proc_id, node->node_head == NULL);
    ASSERT(node->proc_id, node->node_tail == NULL);
    return TRUE;
  }

  ASSERT(node->proc_id, node->node_head != NULL);
  ASSERT(node->proc_id, node->node_tail != NULL);
  return FALSE;
}

void collect_not_ready_to_retire_stats(Op* op) {
  rob_stall_reason = ROB_STALL_OTHER;
  if(op->recovery_scheduled) {
    rob_stall_reason = ROB_STALL_WAIT_FOR_RECOVERY;
  } else if(op->redirect_scheduled) {
    rob_stall_reason = ROB_STALL_WAIT_FOR_REDIRECT;
  }

  if(op->engine_info.l1_miss) {
    rob_stall_reason = ROB_STALL_WAIT_FOR_L1_MISS;
    STAT_EVENT(op->proc_id, RET_BLOCKED_L1_MISS);
    Flag bw_prefetch = !op->engine_info.l1_miss_satisfied &&  // op->req is OK
                                                              // to use
                       op->req->demand_match_prefetch && op->req->bw_prefetch;
    Flag bw_prefetchable = !op->engine_info.l1_miss_satisfied &&  // op->req is
                                                                  // OK to use
                           !op->req->demand_match_prefetch &&
                           op->req->bw_prefetchable;
    if(bw_prefetch || bw_prefetchable)
      STAT_EVENT(op->proc_id, RET_BLOCKED_L1_MISS_BW_PREF);
  }

  if(op->engine_info.l1_miss || op->state == OS_WAIT_MEM) {
    rob_stall_reason = ROB_STALL_WAIT_FOR_MEMORY;
    STAT_EVENT(op->proc_id, RET_BLOCKED_MEM_STALL);
    if(num_offchip_stall_reqs(op->proc_id) > 0) {
      STAT_EVENT(op->proc_id, RET_BLOCKED_OFFCHIP_DEMAND);
    }
  }

  if(op->engine_info.dcmiss) {
    rob_stall_reason = ROB_STALL_WAIT_FOR_DC_MISS;
    STAT_EVENT(op->proc_id, RET_BLOCKED_DC_MISS);
    if(!op->engine_info.l1_miss)
      STAT_EVENT(op->proc_id, RET_BLOCKED_L1_ACCESS);
  }

  node->ret_stall_length++;
}

Flag is_node_table_full() {
  ASSERT(node->proc_id, node->node_count <= NODE_TABLE_SIZE);
  return (node->node_count == NODE_TABLE_SIZE);
}

void collect_node_table_full_stats(Op* op) {
  if(!(op->state == OS_DONE || OP_DONE(op))) {
    if(op->table_info->op_type == OP_ILD || op->table_info->op_type == OP_IST ||
       op->table_info->op_type == OP_FLD || op->table_info->op_type == OP_FST) {
      STAT_EVENT(node->proc_id, FULL_WINDOW_MEM_OP);
    } else if(op->table_info->op_type >= OP_FCVT &&
              op->table_info->op_type <= OP_FCMOV) {
      STAT_EVENT(node->proc_id, FULL_WINDOW_FP_OP);
    } else {
      STAT_EVENT(node->proc_id, FULL_WINDOW_OTHER_OP);
    }
  }

  STAT_EVENT(node->proc_id, FULL_WINDOW_STALL);
}

int is_alu_op(unsigned int op_type) {
    return (op_type == 8 || op_type == 9 || op_type == 10 || op_type == 11 ||
            op_type == 12 || op_type == 13 || op_type == 16 || op_type == 17 ||
            op_type == 18 || op_type == 19 || op_type == 20 || op_type == 21);
}
