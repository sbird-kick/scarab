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
* File         : stage_data.h
* Author       : HPS Research Group
* Date         : 4/5/2022
* Description  :
***************************************************************************************/

#ifndef __PIPELINE_STAGE_H__
#define __PIPELINE_STAGE_H__

#include <string>
#include <vector>

extern "C" {
    #include "op.h"
}

struct StageData {
    static bool flush_op(Op *op, Counter recovery_op_num);

    StageData() = delete;
    StageData(uns proc_id_, std::string name_, int32_t stage_width_);

    void insert(Op *op);
    void reset();
    void recover(Counter recovery_op_num);
    void debug() const;

    uns proc_id;                /// The process ID
    std::string name;           /// Name of the pipeline stage. E.g, "Decode 0"
    uint32_t num_ops;           /// Actual number of ops currently in the stage
    std::vector<Op *> ops;      /// Pipeline Stage slots. Always allocated for maximum stage width.
};

#endif