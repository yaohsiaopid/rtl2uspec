#ifndef SVA_H
#define SVA_H

#include "kernel/register.h"
#include "kernel/celltypes.h"
#include "kernel/log.h"
#include "kernel/sigtools.h"
#include "kernel/ff.h"
#include <string>
#include <sstream>
#include <set>
#include <map>

struct SVA_TEMPLATES {
	static const string pc_stage_0;
	static const string pc_stage_1;
	static const string pc_update_0;
	static const string pc_update_1;
	static const string pc_s_coupdate;
	static const string pc_s_coupdate_m;
	static const string ever_update;
	static const string ever_update_m;
	static const string always_update_prop;

	static const string input_seq_intrahbi;

	static const string tmp_two_trace_1;
	static const string tmp_two_trace_2;
	
} SVA_TEMPLATES_;

const string SVA_TEMPLATES::pc_stage_0 = R"((PCREG == PCVAL))";
const string SVA_TEMPLATES::pc_stage_1 = R"((RESET == 0 && $past(PCREG) == PCVAL && PCREG != PCVAL))";

const string SVA_TEMPLATES::pc_update_0 = R"((PCREG == PCVAL && $past(PCREG) != PCVAL))";
const string SVA_TEMPLATES::pc_update_1 = R"((RESET == 0 && $past(PCREG) == PCVAL && PCREG != PCVAL))";

const string SVA_TEMPLATES::pc_s_coupdate = R"(
PC_CNT: assert property (@(posedge CLK) ((REG != $past(REG)) |-> (PCUPDATE)));
)";
const string SVA_TEMPLATES::ever_update = R"(
AFIX_CNT: assert property (@(posedge CLK) ((PRED) |-> (REG == $past(REG))));
)";
const string SVA_TEMPLATES::always_update_prop = R"(
wire [BIT-1:0] tar_val ;
wire \uhb_LOC = (LOC == tar_val );
assume property (@(posedge CLK) CONST(tar_val ));
assume property (@(posedge CLK) (PRED) |-> (\uhb_LOC ));
assume property (@(posedge CLK) !(PRED) |-> !(\uhb_LOC ));
assume property (@(posedge CLK) first |-> !\uhb_LOC );
A_UPDATE_CNT: assert property (@(posedge CLK) first |-> (s_eventually(\uhb_LOC )));
)";
const string SVA_TEMPLATES::input_seq_intrahbi = R"(
wire [32-1:0] sim_top_1_instn_val;
wire [32-1:0] PC_act_sim1;
assume property(@(posedge CLK) CONST(sim_top_1_instn_val));
assume property(@(posedge CLK) CONST(PC_act_sim1));
assume property(@(posedge CLK) (PC_act_sim1 != 0));
assume property(@(posedge CLK) (RESET == 0)); // in op mode

ANYassume property(@(posedge CLK) VALID_EXE_ASMP  );
assume property(@(posedge CLK) PC1 == PC_act_sim1 |-> INST == sim_top_1_instn_val);
SEQ1: assume property (@(posedge CLK) first |-> strong (
    !(PC1 == PC_act_sim1) [*0:$] ##1
    (PC1 == PC_act_sim1)  [*1:$] ##1 // at least 2nd cycle to actually happen 
    !(PC1 == PC_act_sim1)
));
PROGRESS: assume property (@(posedge CLK) first |-> s_eventually((END == PC_act_sim1)));
)";

const string SVA_TEMPLATES::ever_update_m = R"(
MEM_A_CNT: assert property (@(posedge CLK) (((EXE) && (PRED)) |-> (EXPR == 1)));
)";
//const string SVA_TEMPLATES::ever_update_m = R"(
//wire [ADDR_WIDTH-1:0] tmp_addrREG_SV = ADDR_SIG_STR;
//reg [REG_WIDTH-1:0] tmp_regREG_SV;
//always @(posedge CLK) begin
//tmp_regREG_SV <= MEMID[tmp_addrREG_SV];
//end
//assume property (@(posedge CLK)
//	(tmp_addrREG_SV <= 31)
//);
//MEM_A_CNT: assert property(@(posedge CLK)
//((PRED) |-> MEMID[$past(tmp_addrREG_SV)] == tmp_regREG_SV));
//)";

#endif 
