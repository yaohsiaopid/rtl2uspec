#ifndef INTER_HBI_H
#define INTER_HBI_H
#include "kernel/yosys.h"
#include "kernel/sigtools.h"
#include "kernel/celltypes.h"
#include "backends/ilang/ilang_backend.h"
#include "kernel/hashlib.h"

#include <string>
#include <map>
#include <set>
#include <functional>
#include <queue>
#include <cassert>
#include <algorithm>
#include <regex>
#include <sstream>
#include <fstream>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <utility>
#include <chrono> 
#include <cstdio>
#include <cstdlib>

#include "design.h"
#include "util.h"
#include "sva.h"
#include "dataflow.h"
#include "cdfg.h"
#include "container.h"
#include "dfg.h"
#include "uspec.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN
string folder_prefix = "build/sva/inter_hbi/";

class Inter_HBI {
    static string all_op_execonditions;
	SigMap sigmap;
    vector<DFG> per_inst_DFG;
    string percore_prefix, percore_prefix_prime;

    int hbi_cntr = 0;
    dict<int, hbi_res> hbi_meta; // file# -> one of hbi_checking that maps to it 

    int addr_reg_cntr;
    int data_reg_cntr;

    dict<pair<pair<int, int>,string>, int> pcr_inorder_hbi;

public:
    vector<hbi_res> hbi_checkings, hbi_inferred;
    vector<hbi_res> hbi_inferred_invalid; // by the fact we associate each local resource with PC and intra-core PCR are updated in sequence, and thus its not possible for the Wx -> Ry on local array to happen 
    RTLIL::Module* module_;
    SigMap sigmap_;
    bool isLocal(string &in) {
        return in.find(percore_prefix) != std::string::npos;
    }
    void set_dfgs(vector<DFG> &per_inst_DFG_, SigMap sigmap_,
            string &percore_prefix_, string &percore_prefix_prime_) {
        per_inst_DFG = per_inst_DFG_;
        sigmap = sigmap_;
        percore_prefix = percore_prefix_;
        percore_prefix_prime = percore_prefix_prime_;

        addr_reg_cntr = 0;
        data_reg_cntr = 0;
    }

    void generate(Full_design_cdfg &design_cdfg_info) {
		string conditions = "(0 ";
		for (auto &vex: valid_exe_condition) {
			string itm = "(1 ";
			for (auto conjunc: vex) {
				itm += " && INSTNVAL " + conjunc; 
			}
			itm += ")";
			conditions += ("|| " + itm); 
		}
		conditions += ")";
        conditions = "//assume property (@(posedge CLK) ( " + conditions + " )); //all_instructions";
		all_op_execonditions = conditions;
    

		hbi_cntr = 0;
		hbi_checkings.clear();
        hbi_meta.clear();

        int dfg_sz_ = per_inst_DFG.size();
		for (int idx1 = 0; idx1 < dfg_sz_; idx1++) 
		{
			auto dataflow_g1 = per_inst_DFG[idx1].memgraph;
			string instn1 = "instn_0";
			for (int idx2 = 0; idx2 < dfg_sz_; idx2++) 
			{
				auto dataflow_g2 = per_inst_DFG[idx2].memgraph;
				string instn2 = "instn_1";
                // for all pair of instructions
                
				size_t sz =  dataflow_g1.uhb_sz() < dataflow_g2.uhb_sz() ? dataflow_g1.uhb_sz() : dataflow_g2.uhb_sz();


                // structural spatial
				for (size_t t_ = 0; t_ < sz; t_++) {
                    // same pipeline stage
					auto p1 = dataflow_g1.uhb_item(t_);
					auto p2 = dataflow_g2.uhb_item(t_);

                    // structural spatial
					for (auto &target_: p1) {
						if (p2.find(target_) == p2.end()) continue;

						if (dataflow_g1.ismem(target_) == 1) {
							log("[interhbi] inferr ws on %s (since already prove eventually_update) %zu\n", target_.c_str(), t_);
							hbi_inferred.push_back(hbi_res(-1, structural_spatial, idx1, idx2, target_, target_, isLocal(target_)));
                            continue;
                        }

                        // non-array structural spatial

                        auto hbi_res_ = struct_spatial(target_, idx1, idx2, t_+1); // only consider all state element after IFR, t_=0 correspond to t_+1 stage after IFR
                        hbi_checkings.push_back(hbi_res_);

                    }

                } // structural spatial

                // structural temporal_interface (on array)
                for (auto &itm: dataflow_g1.node_map_mem()) {
                    for (auto &itm2: dataflow_g2.node_map_mem()) {
                        string target_1 = itm.first;
                        string target_2 = itm2.first;
                        if (target_1 != target_2) continue; 

				        auto loc_per_reg1 = dataflow_g1.get_loc_per_reg();
				        auto loc_per_reg2 = dataflow_g2.get_loc_per_reg();
                        auto loc_pool1 = loc_per_reg1[target_1];
                        auto loc_pool2 = loc_per_reg2[target_1];
                        for (auto &info1: loc_pool1) {
                            for (auto &info2: loc_pool2) {
                                // 1. local
                                //   a. read -> if address (local signal -> PCR in order) 

                                //   b. write -> if wen (local signal -> PCR in order)
                                // 2. global: protocol
                                log("[interhbi] struct_temporal_interface %d %d %s (%d,%d), (%d,%d)\n", idx1, idx2, itm.first.c_str(), info1.dep, info1.write, info2.dep, info2.write);
                                bool shared_1 = !isLocal(target_1);
                                if (shared_1) {
                                    // assume trx protocol provided
                                    auto hbi_indices = struct_temporal_interface(target_1, idx1, idx2,  info1.write, info2.write); // 0 -> read, 1 -> write

                                    pool<string> down_pool1 = {target_1};
                                    pool<string> down_pool2 = {target_1};

                                    if (!info1.write)
                                        down_pool1 = dataflow_g1.downstream(target_1);
                                    if (!info2.write)
                                        down_pool2 = dataflow_g2.downstream(target_1);
                                    int k_ = -1;
                                    for (int i = 0; i < hbi_indices.size(); i++) {
                                        for (auto &t1: down_pool1) 
                                            for (auto &t2: down_pool2) 
                                                hbi_checkings.push_back(hbi_res(hbi_indices[i], structural_temporal, idx1, idx2, t1, t2, i < hbi_indices.size() - 1 ? hbi_indices[i+1] : -2));
                                        k_ = hbi_indices[i];
                                        hbi_meta[k_] = hbi_checkings[hbi_checkings.size()-1];
                                    }
                                } else {
                                    // already associate with PCR
                                    log("temporal on local arr | pc in order | unless interface exists\n");
                                    // arr is already associated with some PCR and its downstream ff should also associate with some PCR
                                    // then with fact that its upstream rdaddr should be local then the downstream ff is ordered w.r.t the upstream raddr
                                    // r stage < w stage in uhb path
                                    // r->r since upstream should be local -> depnds on upstream ordering
                                    // r->w since upstream should be local -> depnds on upstream ordering
                                    // **** w->r needs checking 
                                    // w->w same stage already -> PCR order 
                                    // -----
                                    // r stage > w stage in uhb path
                                    // *** r->w  needs checking 
                                    // ------
                                    if(mem_core_req.find(target_1) != mem_core_req.end()) {
                                        log("TODO!!\n");
                                    } else {
                                        // since local resource are all associated with PCR -> check hbi((PCR[info1.dep] == i1), (PCR[info2.dep] == i2))
                                        // 
                                        if (info1.write && !info2.write && info1.dep > info2.dep) {
                                            pool<string> down_pool1 = {target_1};
                                            pool<string> down_pool2 = {target_1};
                                            if (!info1.write)
                                                down_pool1 = dataflow_g1.downstream(target_1);
                                            if (!info2.write)
                                                down_pool2 = dataflow_g2.downstream(target_1);
                                            int tmpcnt=0;
                                            for (auto &t1: down_pool1) 
                                                for (auto &t2: down_pool2) 
                                                    //tmpcnt++;
                                                    hbi_inferred_invalid.push_back(hbi_res(-1, structural_temporal, idx1, idx2, t1, t2, -1));
                                            //log("cnt: %d\n", tmpcnt);
                                            log("TODO\n");
                                            // TODO: genearl case check: check hbi((PCR[info1.dep] == i1), (PCR[info2.dep] == i2)) 
                                            // TODO: hbi_checking (hbi_res( not (PCR[info1.dep])))

                                        } else if (!info1.write && info2.write && info1.dep > info2.dep) {
                                            assert(0);
                                            log("TODO\n");
                                            pool<string> down_pool1 = {target_1};
                                            pool<string> down_pool2 = {target_1};
                                            if (!info1.write)
                                                down_pool1 = dataflow_g1.downstream(target_1);
                                            if (!info2.write)
                                                down_pool2 = dataflow_g2.downstream(target_1);
                                            int tmpcnt=0;
                                            for (auto &t1: down_pool1) 
                                                for (auto &t2: down_pool2) 
                                                    hbi_inferred_invalid.push_back(hbi_res(-1, structural_temporal, idx1, idx2, t1, t2, -1));
                                                    //tmpcnt++;
                                            //log("cnt: %d\n", tmpcnt);
                                        }
                                        
                                    }


                                }

                                // DATA DEP
                                if (info1.write && !(info2.write)) {
                                    log("dataflow relation! %d %d %s\n", idx1, idx2, target_1.c_str());
                                    if (shared_1) {
                                        int hbi_ = dataflow_relation(target_1, idx1, idx2,  info1.write, info2.write); // 0 -> read, 1 -> write
                                        log("dataflow  %d\n", hbi_);
                                        auto down_pool2 = dataflow_g2.downstream(target_1);
                                        for (auto &t2: down_pool2) 
                                            hbi_checkings.push_back(hbi_res(hbi_, data, idx1, idx2, target_1, t2, false));
                                        hbi_meta[hbi_] = hbi_checkings[hbi_checkings.size()-1];
                                    } else {
                                        log("dataflow on local arr | single exe path -> no bypass .. ->  pc in order | unless interface exists\n");
                                        log("%d\n", hbi_checkings.size());
                                        int hbi_idx = dataflow_relation_localarr(target_1, idx1, idx2, info1.dep, info2.dep, design_cdfg_info );

                                        auto down_pool2 = dataflow_g2.downstream(target_1);
                                        for (auto &t2: down_pool2) 
                                            hbi_checkings.push_back(hbi_res(hbi_idx, data, idx1, idx2, target_1, t2, true));
                                        hbi_meta[hbi_idx] = hbi_checkings[hbi_checkings.size()-1];
                                        log("%d->%d\n", hbi_idx, hbi_checkings.size());
                                    }
                                }
                            }
                        } 

                    }
                }


                // structural temporal_pipeline 
                // depends on ***association of pipeline stage to a state element*** -> reduce to PCR in order
				for (size_t t_ = 0; t_ < sz; t_++) {
                    // same pipeline stage
					auto p1 = dataflow_g1.uhb_item(t_);
					auto p2 = dataflow_g2.uhb_item(t_);
                    log("structural temporal idx1 %d idx2 %d t_ = %d (%d, %d)", idx1, idx2,  t_, p1.size(), p2.size());
                    log("%d\n", hbi_checkings.size());

                    for (auto &target_1: p1) {
                        for (auto &target_2: p2) {
                            // both targets are updated
                           
                            if (target_2 == target_1) 
                                continue;  
                            // skip non-array structural spatial

                            bool shared_1 = !isLocal(target_1);
                            bool shared_2 = !isLocal(target_2);
                            bool ismem_1 = (dataflow_g1.ismem(target_1) == 1);
                            bool ismem_2 = (dataflow_g2.ismem(target_2) == 1);
                            //if (shared_1 && shared_2 && ismem_1 && ismem_2) 
                            if ((ismem_1 && shared_1) || (ismem_2 && shared_2)) {
                                // if no association of pipeline stage, then should not reason about this ? 
                                log("driew\n");
                                continue;
                            }
                            // !shared_1 && !shared_2 -> local -> PCR
                            // !shared_1 && shared_2 && !ismem_2 -> local -> PCR
                            // shared_1 && !shared_2 && !ismem_2 -> local -> PCR
                            auto hbi_res_ = struct_temporal_pipeline(target_1, target_2, idx1, idx2, t_+1); 
                            // only consider all state element after IFR, t_=0 correspond to t_+1 stage after IFR
                            hbi_checkings.push_back(hbi_res_);

                        }
                    } // structural temporal
                } // structural temporal_pipeline 

                log(": -> %d\n", hbi_checkings.size());
            } // for all pair of instructions
        }
        log("total %d\n", hbi_cntr);

		std::ofstream hbi_meta_detail(folder_prefix + "hbi_meta.txt.detail", std::ios::out);
	    hbi_meta_detail	<< hbi_res::header() << "\n";
		for (auto &itm: hbi_checkings) 
			hbi_meta_detail << itm.tostr() << "\n";
        hbi_meta_detail.close();
		std::ofstream hbi_meta_f(folder_prefix + "hbi_meta.txt", std::ios::out);
	    hbi_meta_f << hbi_res::header() << "\n";
        for (int i = 0; i < hbi_cntr; i++)  {
            if (hbi_meta.find(i) != hbi_meta.end())
			hbi_meta_f <<// i << "::" << 
                 hbi_meta[i].tostr() << "\n";
        }
        hbi_meta_f.close();
        log("inferred %d\n", hbi_inferred.size());
        log("inferred_invalid %d\n", hbi_inferred_invalid.size());
    }

    int dataflow_relation_localarr(string &target_1, int idx1, int idx2, int info1_dep, int info2_dep, Full_design_cdfg &design_cdfg_info) {
        // Write at info1_dep -> read at info2_dep
        // though can possibly reduce to PCR[info1.dep] -> PCR[info2.dep] in order, but we need constraint on same address , currently can only use the association to do so 

        USPEC_SVA uspec_sva_;
        const int wsz = 32;

        // addProgramOrder
        auto pc_val1 = uspec_sva_.addConst(wsz, "pc");
        auto pc_val2 = uspec_sva_.addConst(wsz, "pc");
        auto pcr0 = EXPR_(PCR[0]);
        auto pc1_at_pcr0 = EXPR_("==", &pcr0, &pc_val1);
        auto pc2_at_pcr0 = EXPR_("==", &pcr0, &pc_val2);
        uspec_sva_.addInOrder(assume, pc1_at_pcr0, pc2_at_pcr0, true); 

        auto i1 = uspec_sva_.addConst(wsz, "i");
        auto i2 = uspec_sva_.addConst(wsz, "i");

        // addPairInstn
        //for (auto &v_: valid_exe_condition[idx1]) {
        //    uspec_sva_.addProperty(assume, EXPR_(v_, &i1, nullptr)); // TODO, currrently assume IFR v_ (eg. v_ = "[13:10] == 4'b0000)
        //}
        //for (auto &v_: valid_exe_condition[idx2]) {
        //    uspec_sva_.addProperty(assume, EXPR_(v_, &i2, nullptr)); // TODO, currrently assume IFR v_ 
        //}
        string exe_condition1 = "(1 ";
        for (auto &v_: valid_exe_condition[idx1]) {
            exe_condition1 += " && " + EXPR_(v_, &i1, nullptr).str() + " ";
            //uspec_sva_.addProperty(assume, EXPR_(v_, &i1, nullptr)); // TODO, currrently assume IFR v_ (eg. v_ = "[13:10] == 4'b0000)
        }
        exe_condition1 += " )";
        string t1 = replace_template(all_op_execonditions,{s_map( "INSTNVAL", i1.str())});
        uspec_sva_.addProperty(assume, exe_condition1, "//input_instructions\n" + t1);
        string exe_condition2 = "(1 ";
        for (auto &v_: valid_exe_condition[idx2]) {
            exe_condition2 += " && " + EXPR_(v_, &i2, nullptr).str() + " ";
            //uspec_sva_.addProperty(assume, EXPR_(v_, &i2, nullptr)); // TODO, currrently assume IFR v_ 
        }
        exe_condition2 += " )";
        string t2 = replace_template(all_op_execonditions,{s_map( "INSTNVAL", i2.str())});
        uspec_sva_.addProperty(assume, exe_condition2, "//input_instructions\n" + t2);

        // associate PC with instn
        auto ifr = EXPR_(instn_reg_name);
        auto i1_at_ifr0 = EXPR_("==", &ifr, &i1);
        auto i2_at_ifr0 = EXPR_("==", &ifr, &i2);

        uspec_sva_.addImmImpli(pc1_at_pcr0, i1_at_ifr0);
        uspec_sva_.addImmImpli(pc2_at_pcr0, i2_at_ifr0);


        RTLIL::Cell* mcell = module_->cells_["\\" + target_1];
        assert(mcell != nullptr);


        int abits = mcell->parameters[ID::ABITS].as_int();
        int cellsize = mcell->parameters[ID::SIZE].as_int();
        auto same_addr = uspec_sva_.addConst(abits, "addr");
        auto same_data = uspec_sva_.addConst(cellsize, "data");	// only to check processed on array, ie save into array[same_addr] == same_data


        auto get_verilog = [&design_cdfg_info](RTLIL::SigSpec s) -> pair<string, string> {
            string ss_ = log_signal(s);
            string verilog_ = "";
            if (ss_[0] == '\\')
                ss_ = ss_.substr(1);
            else {
                verilog_ = design_cdfg_info.get_sig_verilog(s);
                ss_ = log_signal(s);
                ss_ = "\\" + ss_; 
            }
            return make_pair(ss_, verilog_);
        };

        auto sig_addr = mcell->getPort(ID::WR_ADDR);
        auto trans_addr = get_verilog(sig_addr);
        auto addr_e = EXPR_(trans_addr.first);

        auto sig_wdata = mcell->getPort(ID::WR_DATA);
        auto trans_wdata = get_verilog(sig_wdata);
        auto wdata_e = EXPR_(trans_wdata.first);

        auto sig_wen = mcell->getPort(ID::WR_EN).extract(0, 1);
        auto trans_wen = get_verilog(sig_wen);
        auto wen_e = EXPR_(trans_wen.first);

        log("check gen: %s\n----%s\n", trans_addr.first.c_str(), trans_addr.second.c_str());
        if (!trans_addr.second.empty()) 
            uspec_sva_.addDeclaration(trans_addr.second);
        log("check gen: %s\n----%s\n", trans_wdata.first.c_str(), trans_wdata.second.c_str());
        if (!trans_wdata.second.empty()) 
            uspec_sva_.addDeclaration(trans_wdata.second);
        log("check gen: %s\n----%s\n", trans_wen.first.c_str(), trans_wen.second.c_str());
        if (!trans_wen.second.empty()) 
            uspec_sva_.addDeclaration(trans_wen.second);
        
        auto waddr_condition = EXPR_("==", &addr_e, &same_addr);
        auto wen_condition = EXPR_("== 1", &wen_e, nullptr);
        auto wdata_condition = EXPR_("== ", &wdata_e, &same_data);
        auto txn_1 = EXPR_("&&", &waddr_condition, &wen_condition);
        auto txn = EXPR_("&&", &txn_1, &wdata_condition);


        assert(info1_dep - 1 >= 0 && info1_dep - 1 < PCR.size());
        auto pcr_1 = EXPR_(PCR[info1_dep-1]); // write transaction happens at info1_dep -1 and finishes / instantiate noate at info1_dep
        auto pc1_at_pc_dep1 = EXPR_("==", &pcr_1 , &pc_val1);
        uspec_sva_.addImmImpli(pc1_at_pc_dep1, txn);


        // read txn
        
        auto pcr_2 = EXPR_(PCR[info2_dep]); // write transaction happens at info1_dep -1 and finishes / instantiate noate at info1_dep
        auto pc2_at_pc_dep2 = EXPR_("==", &pcr_2 , &pc_val2);

        int nread_ports = mcell->parameters[ID::RD_PORTS].as_int();
        auto sig_rd_addr = mcell->getPort(ID::RD_ADDR).extract(0, abits);
        auto trans_rd_addr = get_verilog(sig_rd_addr);
        auto rd_addr_e = EXPR_(trans_rd_addr.first);
        auto txn_r = EXPR_("==", &rd_addr_e, &same_addr);
        uspec_sva_.addImmImpli(pc2_at_pc_dep2, txn_r);

        auto memid_ = EXPR_(target_1 + "[ " + same_addr.str() + " ]"); //id(mcell->parameters[ID::MEMID].decode_string());
        auto tgt_e1 = EXPR_("==", &memid_, &same_data ); // write target_1[same_addr] == same_data

        auto prev_pc = uspec_sva_.addPrev(pcr_2, 32);
        auto prev_readrd = uspec_sva_.addPrev(rd_addr_e, 32);

        auto tgt_e2_t0 = EXPR_("==", &prev_pc, &pc_val2);
        auto tgt_e2_t1 = EXPR_("!=", &pcr_2, &pc_val2);
        auto tgt_e2_t2 = EXPR_("==", &prev_readrd, &same_addr); 
        auto tgt_e2_t3 = EXPR_("&&", &tgt_e2_t0, &tgt_e2_t1);
        auto tgt_e2 = EXPR_("&&", &tgt_e2_t3, &tgt_e2_t2);
        auto e1 = uspec_sva_.addEvent(wire, tgt_e1.str());
        auto e2 = uspec_sva_.addEvent(wire, tgt_e2.str());


        uspec_sva_.addInOrder(assert, e1, e2); 
        uspec_sva_.finish(folder_prefix + std::to_string(hbi_cntr) + ".sv", clk_s, reset_s);
        int hbi_idx = hbi_cntr;
        hbi_cntr++;
        
        return hbi_idx;
    }

//    hbi_res 
    vector<int> struct_temporal_interface(string &target_1, size_t idx1, size_t idx2, bool a1, bool a2) {
        assert(mem_core_req.find(target_1) != mem_core_req.end());
        // both write to target_1 & target_2 respectively
        log("share 1 share 2 target %s\n", target_1.c_str());

        //==================================================================================================== 
        // send in order
        USPEC_SVA uspec_sva_;
        const int wsz = 32;

        // addProgramOrder
        auto pc_val1 = uspec_sva_.addConst(wsz, "pc");
        auto pc_val2 = uspec_sva_.addConst(wsz, "pc");
        auto pcr0 = EXPR_(PCR[0]);
        auto pc1_at_pcr0 = EXPR_("==", &pcr0, &pc_val1);
        auto pc2_at_pcr0 = EXPR_("==", &pcr0, &pc_val2);
        uspec_sva_.addInOrder(assume, pc1_at_pcr0, pc2_at_pcr0, true); 

        auto i1 = uspec_sva_.addConst(wsz, "i");
        auto i2 = uspec_sva_.addConst(wsz, "i");

        // addPairInstn
        for (auto &v_: valid_exe_condition[idx1]) {
            uspec_sva_.addProperty(assume, EXPR_(v_, &i1, nullptr)); // TODO, currrently assume IFR v_ (eg. v_ = "[13:10] == 4'b0000)
        }
        for (auto &v_: valid_exe_condition[idx2]) {
            uspec_sva_.addProperty(assume, EXPR_(v_, &i2, nullptr)); // TODO, currrently assume IFR v_ 
        }

        // associate PC with instn
        auto ifr = EXPR_(instn_reg_name);
        auto i1_at_ifr0 = EXPR_("==", &ifr, &i1);
        auto i2_at_ifr0 = EXPR_("==", &ifr, &i2);

        uspec_sva_.addImmImpli(pc1_at_pcr0, i1_at_ifr0);
        uspec_sva_.addImmImpli(pc2_at_pcr0, i2_at_ifr0);


        // get send_req format
        log("%s\n", target_1.c_str());
        auto mem_core_req__ = mem_core_req[target_1];

        // TODO: output_driver_cyc_diff[sig_] 
        //auto sig_ = get_sig_by_str("\\" + percore_prefix + "." + mem_core_req__.trans_valid.first, module_, sigmap_);
        //assert(sig_.empty());
        //auto cyc = output_driver_cyc_diff[sig_];
        //cyc -> get PCR
        auto pcr_send_e = EXPR_(mem_core_req__.pc_);
        auto i1addr = uspec_sva_.addConst(wsz, "addr");
        auto i2addr = uspec_sva_.addConst(wsz, "addr");
        auto e1_condition = mem_core_req__.tostr(a1, i1addr.str(), percore_prefix);
        auto e2_condition = mem_core_req__.tostr(a2, i2addr.str(), percore_prefix);
        auto send_e1 = uspec_sva_.addEvent(wire, e1_condition);
        auto send_e2 = uspec_sva_.addEvent(wire, e2_condition);
        auto prev_send_e1 = uspec_sva_.addPrev(send_e1); 
        auto prev_send_e2 = uspec_sva_.addPrev(send_e2); 

        // associate send_e1 with i1
        auto pc1_at_pcr_send = EXPR_("==", &pcr_send_e, &pc_val1);
        auto pc2_at_pcr_send = EXPR_("==", &pcr_send_e, &pc_val2);
        auto trans_pc1_at_pcr_send = uspec_sva_.trans(&pc1_at_pcr_send); 
        auto trans_pc2_at_pcr_send = uspec_sva_.trans(&pc2_at_pcr_send); 

        uspec_sva_.addImmImpli(trans_pc1_at_pcr_send, prev_send_e1.str(), true);
        uspec_sva_.addImmImpli(trans_pc2_at_pcr_send, prev_send_e2.str(), true);

        uspec_sva_.addInOrder(assert,prev_send_e1.str(),prev_send_e2.str()); 
                                     
        uspec_sva_.finish(folder_prefix + std::to_string(hbi_cntr) + ".sv", clk_s, reset_s);
        hbi_cntr++;

        // end send in order

        //==================================================================================================== 
        // rec in order
        USPEC_SVA uspec_sva_rec;
        i1addr = uspec_sva_rec.addConst(wsz, "addr");
        i2addr = uspec_sva_rec.addConst(wsz, "addr");
        e1_condition = mem_core_req__.tostr(a1, i1addr.str(), percore_prefix);
        e2_condition = mem_core_req__.tostr(a2, i2addr.str(), percore_prefix);
        send_e1 = uspec_sva_rec.addEvent(wire, e1_condition);
        send_e2 = uspec_sva_rec.addEvent(wire, e2_condition);
        uspec_sva_rec.addInOrder(assume, send_e1.str(), send_e2.str(), true); 

	    auto mem_rec_req__ = mem_rec_req[target_1];
        auto tgt_e1_condition = mem_rec_req__.tostr(a1, i1addr.str(), mem_prefix);
        auto tgt_e2_condition = mem_rec_req__.tostr(a2, i2addr.str(), mem_prefix);

        auto tgt_e1 = uspec_sva_rec.addEvent(wire, tgt_e1_condition);
        auto tgt_e2 = uspec_sva_rec.addEvent(wire, tgt_e2_condition);
        uspec_sva_rec.addInOrder(assert, tgt_e1.str(), tgt_e2.str()); 

        uspec_sva_rec.finish(folder_prefix + std::to_string(hbi_cntr) + ".sv", clk_s, reset_s);
        hbi_cntr++;

        // end rec in order

        //==================================================================================================== 
        // proc in order
        USPEC_SVA uspec_sva_proc;
        i1addr = uspec_sva_proc.addConst(wsz, "addr");
        i2addr = uspec_sva_proc.addConst(wsz, "addr");
        uspec_sva_proc.addProperty(assume, EXPR_("[1:0] == 2'b00", &i1addr, nullptr));
        uspec_sva_proc.addProperty(assume, EXPR_("[1:0] == 2'b00", &i2addr, nullptr));
        uspec_sva_proc.addProperty(assume, EXPR_("< 32'd128", &i1addr, nullptr)); 
        uspec_sva_proc.addProperty(assume, EXPR_("< 32'd128", &i2addr, nullptr)); 
        // 128
        e1_condition = mem_rec_req__.tostr(a1, i1addr.str(), mem_prefix);
        e2_condition = mem_rec_req__.tostr(a2, i2addr.str(), mem_prefix);

        auto rec_e1 = uspec_sva_proc.addEvent(wire, e1_condition);
        auto rec_e2 = uspec_sva_proc.addEvent(wire, e2_condition);
        uspec_sva_proc.addInOrder(assume, rec_e1.str(), rec_e2.str(), true); 

        auto mem_proc_req__ = mem_proc_reqs[target_1];
        if (a1) {
            tgt_e1_condition = mem_proc_req__.tostr(a1, i1addr.str(), mem_prefix);
        } else {
            auto prev_send_e1 = uspec_sva_proc.addPrev(rec_e1); 
            tgt_e1_condition = mem_proc_req__.tostr(a1, i1addr.str(), mem_prefix, prev_send_e1.str());
            log("!a1 read");
        }
        if (a2) {
            tgt_e2_condition = mem_proc_req__.tostr(a2, i2addr.str(), mem_prefix);
        } else {
            auto prev_send_e2 = uspec_sva_proc.addPrev(rec_e2); 
            tgt_e2_condition = mem_proc_req__.tostr(a2, i2addr.str(), mem_prefix, prev_send_e2.str());
            log("!a2 read");
        }

        tgt_e1 = uspec_sva_proc.addEvent(wire, tgt_e1_condition);
        tgt_e2 = uspec_sva_proc.addEvent(wire, tgt_e2_condition);

        uspec_sva_proc.addInOrder(assert, tgt_e1.str(), tgt_e2.str()); 

        uspec_sva_proc.finish(folder_prefix + std::to_string(hbi_cntr) + ".sv", clk_s, reset_s);
        hbi_cntr++;
        // end proc in order

        vector<int> arr{hbi_cntr-3, hbi_cntr-2, hbi_cntr-1};
        return arr;
    } // struct_temporal_interface

    hbi_res struct_temporal_pipeline(string &target_1, string &target_2, int idx1, int idx2, int stage_) {
        //log("[inter_hbi] add struct_temporal_pipeline (reduce_pcr_order)  %d %d %d %s %s\n", idx1, idx2, stage_, target_1.c_str(), target_2.c_str());
        string stage_pcr;
        // reduce to PRC[stage_] in order 
        // TODO
        bool cyc_after_final = false;
        if (stage_ >= PCR.size()) {
            cyc_after_final = true;
            stage_pcr = PCR[PCR.size()-1]; 
        } else 
            stage_pcr = PCR[stage_];
        
        int hbi_idx = -1;
        auto pr = make_pair(make_pair(idx1, idx2), stage_pcr);
        if (pcr_inorder_hbi.find(pr) != pcr_inorder_hbi.end()) {
            hbi_idx = pcr_inorder_hbi[pr]; //file_seqno;
            return hbi_res(hbi_idx, structural_temporal, idx1, idx2, target_1, target_2, true);
        } 

        hbi_idx = pcr_in_order(stage_pcr, idx1, idx2);

        pcr_inorder_hbi[pr] = hbi_idx; //hbi_res(hbi_idx, structural_temporal, idx1, idx2, target_1, target_2, true); 
        hbi_meta[hbi_idx] = hbi_res(hbi_idx, structural_temporal, idx1, idx2, target_1, target_2, true);
        return hbi_res(hbi_idx, structural_temporal, idx1, idx2, target_1, target_2, true);
    }

    int pcr_in_order(string stage_pcr, int idx1, int idx2) {
        USPEC_SVA uspec_sva_;
        const int wsz = 32;

        // addProgramOrder
        auto pc_val1 = uspec_sva_.addConst(wsz, "pc");
        auto pc_val2 = uspec_sva_.addConst(wsz, "pc");
        auto pcr0 = EXPR_(PCR[0]);
        auto pc1_at_pcr0 = EXPR_("==", &pcr0, &pc_val1);
        auto pc2_at_pcr0 = EXPR_("==", &pcr0, &pc_val2);
        uspec_sva_.addInOrder(assume, pc1_at_pcr0, pc2_at_pcr0, true); 

        auto i1 = uspec_sva_.addConst(wsz, "i");
        auto i2 = uspec_sva_.addConst(wsz, "i");

        // addPairInstn
        string exe_condition1 = "(1 ";
        for (auto &v_: valid_exe_condition[idx1]) {
            exe_condition1 += " && " + EXPR_(v_, &i1, nullptr).str() + " ";
            //uspec_sva_.addProperty(assume, EXPR_(v_, &i1, nullptr)); // TODO, currrently assume IFR v_ (eg. v_ = "[13:10] == 4'b0000)
        }
        exe_condition1 += " )";
        string t1 = replace_template(all_op_execonditions,{s_map( "INSTNVAL", i1.str())});
        uspec_sva_.addProperty(assume, exe_condition1, "//input_instructions\n" + t1);
        string exe_condition2 = "(1 ";
        for (auto &v_: valid_exe_condition[idx2]) {
            exe_condition2 += " && " + EXPR_(v_, &i2, nullptr).str() + " ";
            //uspec_sva_.addProperty(assume, EXPR_(v_, &i2, nullptr)); // TODO, currrently assume IFR v_ 
        }
        exe_condition2 += " )";
        string t2 = replace_template(all_op_execonditions,{s_map( "INSTNVAL", i2.str())});
        uspec_sva_.addProperty(assume, exe_condition2, "//input_instructions\n" + t2);

        // associate PC with instn
        auto ifr = EXPR_(instn_reg_name);
        auto i1_at_ifr0 = EXPR_("==", &ifr, &i1);
        auto i2_at_ifr0 = EXPR_("==", &ifr, &i2);

        uspec_sva_.addImmImpli(pc1_at_pcr0, i1_at_ifr0);
        uspec_sva_.addImmImpli(pc2_at_pcr0, i2_at_ifr0);

        // check if the stage_pcr are in order

        auto tgtpcr = EXPR_(stage_pcr);
        auto i1_at_targetpcr = EXPR_("==", &tgtpcr, &pc_val1);
        auto i2_at_targetpcr = EXPR_("==", &tgtpcr, &pc_val2);
        uspec_sva_.addInOrder(assert , i1_at_targetpcr, i2_at_targetpcr);
        uspec_sva_.finish(folder_prefix + std::to_string(hbi_cntr) + ".sv", clk_s, reset_s);

        int hbi_idx = hbi_cntr;
        hbi_cntr++;
        return hbi_idx;
    } // end pcr_in_order
    hbi_res struct_spatial(string &target_, int idx1, int idx2, int stage_) {
        log("[inter_hbi] add struct_spatial %d %d %d %s\n", idx1, idx2, stage_, target_.c_str());
        // reduce to PRC[stage_] in order 
        string stage_pcr;
        bool cyc_after_final = false;
        if (stage_ >= PCR.size()) {
            cyc_after_final = true;
            stage_pcr = PCR[PCR.size()-1]; 
        } else 
            stage_pcr = PCR[stage_];
        
        int hbi_idx = -1;
        auto pr = make_pair(make_pair(idx1, idx2), stage_pcr);
        if (pcr_inorder_hbi.find(pr) != pcr_inorder_hbi.end()) {
            hbi_idx = pcr_inorder_hbi[pr];//file_seqno;
            return hbi_res(hbi_idx, structural_spatial, idx1, idx2, target_, target_, true);
        } 

        hbi_idx = pcr_in_order(stage_pcr, idx1, idx2);

        pcr_inorder_hbi[pr] = hbi_idx;//hbi_res(hbi_idx, structural_spatial, idx1, idx2, target_, target_, true);
        hbi_meta[hbi_idx] = hbi_res(hbi_idx, structural_spatial, idx1, idx2, target_, target_, true);
        return hbi_res(hbi_idx, structural_spatial, idx1, idx2, target_, target_, true);
    } // end struct_spatial

    int dataflow_relation(string &target_1, size_t idx1, size_t idx2, bool a1, bool a2) {
        assert(mem_core_req.find(target_1) != mem_core_req.end());
        assert(a1 && !a2);
        // same addr but different core
        // proc in order
        const int wsz = 32;
        USPEC_SVA uspec_sva_proc;
        auto i1addr = uspec_sva_proc.addConst(wsz, "addr");
        uspec_sva_proc.addProperty(assume, EXPR_("[1:0] == 2'b00", &i1addr, nullptr));
        uspec_sva_proc.addProperty(assume, EXPR_("< 32'd128", &i1addr, nullptr)); 

	    auto mem_rec_req__ = mem_rec_req[target_1];
        auto e1_condition = mem_rec_req__.tostr(a1, i1addr.str(), mem_prefix);
        auto e2_condition = mem_rec_req__.tostr(a2, i1addr.str(), mem_prefix, false); // from differnt core

        auto rec_e1 = uspec_sva_proc.addEvent(wire, e1_condition);
        auto rec_e2 = uspec_sva_proc.addEvent(wire, e2_condition);
        uspec_sva_proc.addInOrder(assume, rec_e1.str(), rec_e2.str(), true); 

        auto mem_proc_req__ = mem_proc_reqs[target_1];
        auto tgt_e1_condition = mem_proc_req__.tostr(a1, i1addr.str(), mem_prefix);
        
        auto prev_send_e2 = uspec_sva_proc.addPrev(rec_e2); 
        auto tgt_e2_condition = mem_proc_req__.tostr(a2, i1addr.str(), mem_prefix, prev_send_e2.str(), true); // diff core

        auto tgt_e1 = uspec_sva_proc.addEvent(wire, tgt_e1_condition);
        auto tgt_e2 = uspec_sva_proc.addEvent(wire, tgt_e2_condition);

        uspec_sva_proc.addInOrder(assert, tgt_e1.str(), tgt_e2.str()); 

        uspec_sva_proc.finish(folder_prefix + std::to_string(hbi_cntr) + ".sv", clk_s, reset_s);
        int hbi_idx = hbi_cntr;
        hbi_cntr++;
        // end proc in order c0: wx -> c1: rx
        return hbi_idx; 
    } // end dataflow_relation

};
string Inter_HBI::all_op_execonditions = "";

PRIVATE_NAMESPACE_END
#endif
