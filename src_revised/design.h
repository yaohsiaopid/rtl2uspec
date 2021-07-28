#ifndef DESIGN_H
#define DESIGN_H

#include "kernel/register.h"
#include "kernel/celltypes.h"
#include "kernel/log.h"
#include "kernel/sigtools.h"
#include "kernel/ff.h"
#include "uspec.h"
#include <string>
#include <sstream>
#include <set>
#include <map>

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

const string intra_hbi_folder = "./build/sva/intra_hbi/";

#define VSCALE
#ifdef VSCALE
// ----------------- pre processing
const int num_instn = 2;
const string clk_s = "clk";						
const string core_module_name = "vscale_core";
const string core_instance_name = "";
// ------------------ find reachable set / generate intra_hbi 
const string instn_reg_name = "core_gen_block[0].vscale.pipeline.inst_DX"; 
// IFR in per core named called from top design
const string pc_sig_begin = "core_gen_block[0].vscale.pipeline.PCmux.PC_PIF";
// the PC signal that is used to index imem and clocked into PC_IF

// PCR[1] -> instn_reg_name/IFR
//"core_gen_block[0].vscale.pipeline.PC_IF", 
const vector<string> PCR = {
    "core_gen_block[0].vscale.pipeline.PC_DX", "core_gen_block[0].vscale.pipeline.PC_WB"}; // "32'hffffffff", "32'hffffffff"}; 
const vector<string> opcodes_name  {"store", "load"};
const vector<pool<string>> valid_exe_condition = {{"[6:0] == 7\'b0100011", "[14:12] == 3'b010"}, {"[6:0] == 7\'b0000011", "[11:7] != 5\'d0", "[14:12] == 3'b010"}};
const string reset_s =  "reset";

const string core_select = "arbiter_next_core";
const int core_select_val = 2;  // bit width
// --------- memory txn interface ---------
std::string mem_prefix = "hasti_mem"; //memory module instantiated name
dict<std::string, struct core_mem_req_> mem_core_req = {
	{
		"hasti_mem.mem", {
			.trans_rw=make_pair("dmem_hwrite", "1"), 
			.trans_valid=make_pair("dmem_htrans", "`HASTI_TRANS_NONSEQ"),
			.trans_siz=make_pair("dmem_hsize", "`HASTI_SIZE_WORD"),
			.waddr = "dmem_haddr",
			.wdata = "dmem_hwdata",
			.raddr = "dmem_haddr",
			.rdata = "dmem_hrdata",
            .pc_ = "core_gen_block[0].vscale.pipeline.PC_DX",
		}
	}
}; // key is mem module 

dict<std::string, struct mem_rec_req_> mem_rec_req = {
	{
		"hasti_mem.mem", {
			.trans_rw=make_pair("p0_hwrite", "1"), 
			.trans_valid=make_pair("p0_htrans", "`HASTI_TRANS_NONSEQ"),
			.trans_siz=make_pair("p0_hsize", "`HASTI_SIZE_WORD"),
			.trans_samecore=make_pair("p0_haddr[2+`HASTI_ADDR_WIDTH-1:1+`HASTI_ADDR_WIDTH-1]", "2'd0"), // same core being c0
			.trans_diffcore=make_pair("p0_haddr[2+`HASTI_ADDR_WIDTH-1:1+`HASTI_ADDR_WIDTH-1]", "2'd1"), // diff core being c1
			.waddr = "p0_haddr[`HASTI_ADDR_WIDTH-1:0]",
			.raddr = "p0_haddr[`HASTI_ADDR_WIDTH-1:0]",
		}
	}
}; // key is mem module 

dict<std::string, struct mem_proc_req_> mem_proc_reqs = {
	{
		"hasti_mem.mem", {
			.array_name="mem",
			.trans_siz=make_pair("p0_wmask", "32'hffffffff"),
			.trans_w={"p0_state == 1 ", "p0_wvalid == 1"},
			.waddr=make_pair("p0_word_waddr[2+`HASTI_ADDR_WIDTH-1:1+`HASTI_ADDR_WIDTH-1]", "p0_word_waddr[`HASTI_ADDR_WIDTH-1:0]"),
			.raddr=make_pair("p0_reg_raddr[2+`HASTI_ADDR_WIDTH-1:1+`HASTI_ADDR_WIDTH-1]", "p0_reg_raddr[`HASTI_ADDR_WIDTH-1:0]"),
			.waddr_offset=2,
			.raddr_offset=2,//0,
			.trans_r_transparent=make_pair(true, 1), // previous cycle should get the read request as it is rdtransparent 	
			.rdata = "p0_hrdata",
		// 	.waddr = "dmem_haddr",
		// 	.wdata = "dmem_hwdata",
		// 	.raddr = "dmem_haddr",
			
		}
	}
}; // key is mem module 

#endif

#ifdef RIDECORE
// ----------------- pre processing
string clk_s = "clk";						
string core_module_name = "pipe";
string core_instance_name = "";
// ------------------ find reachable set / generate intra_hbi 
string instn_reg_name = ""; 
vector<string> PCR = {"32'hffffffff","32'hffffffff","32'hffffffff", "32'hffffffff"}; 
vector<string> opcodes_name  {"store", "load"};
vector<pool<string>> valid_exe_condition = {{"[6:0] == 7\'b0100011", "[14:12] == 3'b010"}, {"[6:0] == 7\'b0000011", "[11:7] != 5\'d0", "[14:12] == 3'b010"}};
string reset_s =  "reset_x";
// ------------------ RIDECORE ---------------------
#endif 

PRIVATE_NAMESPACE_END

#endif
