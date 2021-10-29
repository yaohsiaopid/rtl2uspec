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
#include <cstdlib>
#include <chrono>
#include <thread>
#include <utility>
#include <chrono> 
#include <cstdio>
#include <cstdlib>

#include "design.h"
#include "util.h"
#include "cdfg.h"
#include "dataflow.h"
#include "dfg.h"
#include "inter_hbi.h"
#include "dumpuspec.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

string percore_prefix;			// instantiated core name 0, cell type same as core_module_name
string percore_prefix_prime;   // instantiated core name 1, cell type same as core_module_name
SigMap sigmap_; 
RTLIL::Module *module_;

Full_design_cdfg design_cdfg_info;

RTLDataFlow RTLDataFlow_default;
RTLDataFlow RTLDataFlow_nos;
RTLDataFlow RTLDataFlow_full_s;

vector<DFG> per_inst_DFG;

Inter_HBI inter_hbi_;

USPEC_GEN uspec_gen_;

void selective_flatten_preproc(RTLIL::Design *design, RTLIL::Module *module_, string &percore_prefix, string &percore_prefix_prime, pool<RTLIL::SigSpec> &output_sigs_outcore);

struct RTL2USPEC_CDFG : public Pass {
	RTL2USPEC_CDFG() : Pass("rtl2uspec_cdfg") { }
	void execute(vector<string> args, RTLIL::Design *design) override
	{
		log(" ============= [PASS] rtl2uspec_cdfg ============= \n");
        
        module_ = design->top_module();
		sigmap_ = SigMap(module_);

		log("module_ name: %s\n", module_->name.str().c_str());
		log("module cells size %d\n", module_->cells_.size());
        assert(module_ != nullptr && module_->cells_.size() > 0);
        
        pool<RTLIL::SigSpec> output_sigs_outcore; 
        selective_flatten_preproc(design, module_, percore_prefix, percore_prefix_prime, output_sigs_outcore);

		auto start = std::chrono::steady_clock::now();

        design_cdfg_info.set(&RTLDataFlow_default, &RTLDataFlow_nos, &RTLDataFlow_full_s);

		//design_cdfg_info.collect_sig_cell(module_, 1, half_s);
        //design_cdfg_info.collect_sig_cell(module_, 1, nos);
        design_cdfg_info.collect_sig_cell(module_, 1, full_s);

        //design_cdfg_info.find_downstream_reg(half_s);
        //design_cdfg_info.find_downstream_reg(nos);
        design_cdfg_info.find_downstream_reg(full_s, output_sigs_outcore);

        //design_cdfg_info.find_downstream_handle_mem(half_s);
        //design_cdfg_info.find_downstream_handle_mem(nos);
        design_cdfg_info.find_downstream_handle_mem(full_s);


		log(" ============= [PASS] reachable set  ============= \n");

        //RTLDataFlow_default;
        
        auto s_ = get_sig_by_str(pc_sig_begin, module_, sigmap_);
        auto cells_ = design_cdfg_info.sigToNextCell();
        RTLDataFlow_full_s.find_reachable_set(pc_sig_begin, cells_[s_]);

        auto sig_cyc = RTLDataFlow_full_s.sig_cyc();
        assert(sig_cyc.find(instn_reg_name) != sig_cyc.end());
        int th = sig_cyc[instn_reg_name].diff;
        if (sig_cyc.find(PCR[0]) == sig_cyc.end()) {
            log("fail\n");
            assert(0);
        } else {
            log("[debug] %d\n", sig_cyc[PCR[0]].diff);
            log("%d\n", th);
        }
        //return;
        assert(sig_cyc[PCR[0]].diff == th);
        dict<pair<RTLIL::SigSpec, RTLIL::Cell*>, int> ignore_frontend;
        dict<string, u_sig_cell> sig_cyc_offset;
        for (auto &p: sig_cyc) {
            sig_cyc_offset[p.first] = {.s = p.second.s, .c = p.second.c, .diff = (p.second.diff - th)};
            
            if (p.second.diff <= th) {
                // TODO: same as th should be consider 
                // same or before inst_reg_name
                pair<RTLIL::SigSpec, RTLIL::Cell*> t_ = make_pair(p.second.s, p.second.c);
                ignore_frontend[t_] = p.second.diff - th;
                if (t_.second == nullptr)
                    log("ignore %s\n", log_signal(t_.first));
                else
                    log("ignore %s\n", t_.second->name.str().c_str());
            }
        }

		dump_sigpool(output_sigs_outcore, "output_sigs_outcore:");
        auto sig2cells_ = design_cdfg_info.sigToNextCell();
        auto invsig2cells_ = design_cdfg_info.invCellToNextSig();
        auto connections_ = design_cdfg_info.connections();
        ////auto output_driver_cyc_diff = 
        //    RTLDataFlow_full_s.output_port_cycdiff(sig2cells_, invsig2cells_, output_sigs_outcore, 
        //             //design_cdfg_info.invSigToNextCell(),
        //             connections_);
        
        //RTLDataFlow_full_s.find_reachable_set(PCR[0]);
        //RTLDataFlow_full_s.find_reachable_set(inst_reg_name);
        //
        DFG::cyc_diff = sig_cyc_offset;
        DFG::ever_update_s = vector<string>();
        DFG::ever_update_meta_list = vector<string>();
        DFG::cnt_intra_hbi = 0;
        DFG::max_cyc_diff = 0;
        DFG::intra_hbi_per_s(ignore_frontend, design_cdfg_info, sig_cyc_offset, percore_prefix);

        per_inst_DFG.resize(num_instn);
        for (int idx = 0; idx < num_instn; idx++) {
            // 1. generate properties with assumption being instruction of type idx
            per_inst_DFG[idx].intra_hbi_instn(valid_exe_condition[idx], idx);
            // 2. evetual progress for instruction of type idx

            USPEC_SVA uspec_sva_;
            const int wsz = 32;
            auto pc_val1 = uspec_sva_.addConst(wsz, "pc");
            auto pcr0 = EXPR_(PCR[0]);
            auto nonz_e = EXPR_("!= 0", &pc_val1, nullptr);
            uspec_sva_.addProperty(assume, nonz_e);

            auto pc1_at_pcr0 = EXPR_("==", &pcr0, &pc_val1);
            auto neg_pc1_at_pcr0 = EXPR_("!", &pc1_at_pcr0);
            auto i1 = uspec_sva_.addConst(wsz, "i");
            for (auto &v_: valid_exe_condition[idx]) {
                uspec_sva_.addProperty(assume, EXPR_(v_, &i1, nullptr)); // TODO, currrently assume IFR v_ (eg. v_ = "[13:10] == 4'b0000)
            }
            auto ifr = EXPR_(instn_reg_name);
            auto i1_at_ifr0 = EXPR_("==", &ifr, &i1);
            uspec_sva_.addImmImpli(pc1_at_pcr0, i1_at_ifr0);

            auto nop = EXPR_("== `RV_NOP", &ifr, nullptr);
            auto pre = EXPR_("&&", &nop, &neg_pc1_at_pcr0);
            uspec_sva_.addInOrderPartial(assume, pre, pc1_at_pcr0, true); 

            auto arbiter = EXPR_(core_select);
            for (int i = 0; i < (1 << core_select_val); i++) {
                int cur = i;
                int next = (i == ((1 << core_select_val) - 1)) ? 0 : i + 1; 
                auto p = EXPR_("==" + std::to_string(core_select_val) + "'d" + std::to_string(cur), &arbiter, nullptr);
                auto q = EXPR_("==" + std::to_string(core_select_val) + "'d" + std::to_string(next), &arbiter, nullptr);
                uspec_sva_.addImpli(p, q);
            }
            auto last_ = EXPR_(PCR[PCR.size()-1]);
            auto eq = EXPR_("==", &last_, &pc_val1);
            auto eq_neg = EXPR_("!", &eq);
            uspec_sva_.addImmImpli("first", "s_eventually ( (" + eq.str() + ") ##1 (" + eq_neg.str() + " ))", false, assert);

            uspec_sva_.finish(intra_hbi_folder + "eventual_" + std::to_string(idx) + ".sv", clk_s, reset_s);
        }


        //return;
        // wait for evaluatiton

        bool intraEval = false, interEval = false;
        for (size_t argidx = 1; argidx < args.size(); argidx++) {
            std::string arg = args[argidx];
            if (arg == "-intradone") {
                intraEval = true;
            }
            if (arg == "-interdone") {
                intraEval = true;
                interEval = true;
            }
        }
        if (intraEval) {
            for (int idx = 0; idx < num_instn; idx++) {
                per_inst_DFG[idx].parse_intra_hbi_res();
                log("%d %d\n", RTLDataFlow_full_s.design_reg_reg.size(), RTLDataFlow_full_s.design_reg_mem.size());
                per_inst_DFG[idx].build_dfg(RTLDataFlow_full_s.design_reg_reg, 
                        RTLDataFlow_full_s.design_reg_mem, module_, sigmap_, 
                        RTLDataFlow_full_s.rd_data_to_wports, 
                        RTLDataFlow_full_s.reg_to_upstream_rdport);
            }

            log("------------------ dfg done -------------------------\n");
            inter_hbi_.module_ = module_;
            inter_hbi_.sigmap_ = sigmap_;
            inter_hbi_.set_dfgs(per_inst_DFG, sigmap_, percore_prefix, percore_prefix_prime);

            inter_hbi_.generate(design_cdfg_info);
            log("------------------ generate inter hbi done -------------------------\n");
        }

        if (interEval) {
            uspec_gen_ = USPEC_GEN(inter_hbi_);
            uspec_gen_.parse_result();
            uspec_gen_.uhb_gen_merge(per_inst_DFG, percore_prefix);

            auto end = std::chrono::steady_clock::now();
            std::chrono::duration<double> total = end-start;
            log("total time: %f", total.count());
        }


    }
} rtluspec_cdfg_pass;

void selective_flatten_preproc(RTLIL::Design *design, RTLIL::Module *module_, string &percore_prefix, string &percore_prefix_prime, pool<RTLIL::SigSpec> &output_sigs_outcore) {
    // Only flatten distinct modules. ie. which instantiated module not to flatten
    bool core0 = true, core1 = true;
    for (auto &item: module_->cells_) {
        auto cnm = item.first.str();
        auto c = item.second;
        // isIncore
        if (c->type.str().find(core_module_name) != string::npos) {
            if (core0) {
                percore_prefix = cnm.substr(1);
            } else {
                c->set_bool_attribute(ID::keep_hierarchy, true);
                if (core1) {
                    percore_prefix_prime = cnm.substr(1);
                    core1 = false; 
                }
            }
            log("isCore cell name %s\n", cnm.c_str());
            core0 = false;
        }
    }
    if (!core0)
        log("percore_prefix: %s\n", percore_prefix.c_str());
    if (!core1)
        log("percore_prefix_prime: %s\n", percore_prefix_prime.c_str());

    auto acore = module_->cells_["\\" + percore_prefix];
    assert(acore != nullptr && percore_prefix.size());
    
    for (auto &w_: acore->connections()) {
        auto port = w_.first;
        if (acore->output(port)) {
            auto sig = w_.second;
            auto s = sigmap_(sig);
            if (!s.is_fully_const()) {
                for (auto &chunk: s.chunks()) { 
                    if (chunk.wire != NULL)
                        output_sigs_outcore.insert(sigmap_(chunk.wire));
                }
            }
        }
    }
    
    Pass::call(design, "memory -nomap -nordff");
    Pass::call(design, "flatten");
    Pass::call(design, "stat");
    Pass::call(design, "ff_stat");
    Pass::call(design, "hierarchy -check -top " + module_->name.str().substr(1)); //TBC
    Pass::call(design, "dump -o ./build/dump/rtlil.txt"); //TBC
    
    
}

PRIVATE_NAMESPACE_END
