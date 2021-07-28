#ifndef DFG_H
#define DFG_H
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
#include "uspec.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN


enum sva_type {ff, arr};
struct sva_result {
	sva_type type;
	// string res;  
	bool res;
	string ctrl_val;		  // if 
	string str() { return "{" + std::to_string(type) + "," + (res ? "true" : "false") + "(" + ctrl_val + ")}"; }
};

class DFG {
    int idx_;
    string meta_file;
    // intra_hbi result
    vector <pool<u_sig_cell>> stage;
    dict<RTLIL::SigSpec, sva_result> sva_results;
    dict<RTLIL::SigSpec, int> sva_results_stage;
    dict<RTLIL::Cell*, sva_result> sva_results_m;
    dict<RTLIL::Cell*, int > sva_results_m_stage;

    MyGraph uhb_graph;

public:
    MyGraph memgraph;
    static int max_cyc_diff;
    static vector<string> ever_update_s;
    static vector<string> ever_update_meta_list;
    static vector<pair<string,string>> eventual_progress_remote; // (i_val_name, sva_)
    static vector<string> eventual_progress_remote_target;
    static int cnt_intra_hbi;
    static dict<string, u_sig_cell> cyc_diff;
    static void intra_hbi_per_s(dict<pair<RTLIL::SigSpec, RTLIL::Cell*>, int> ignore_frontend, Full_design_cdfg design_cdfg_info, dict<string, u_sig_cell> cyc_diff_, string percore_prefix="") {
        cyc_diff = cyc_diff_;
        cnt_intra_hbi = 0;
        max_cyc_diff = 0;
        ever_update_s.clear();    
        ever_update_meta_list.clear();
        for (auto &p: cyc_diff) {
            int diff = p.second.diff;
            if (diff > max_cyc_diff)
                max_cyc_diff = diff;
            if (diff < 0 || 
              ignore_frontend.find(make_pair(p.second.s, p.second.c)) != ignore_frontend.end()) {

                log("skip intra %s\n", p.first.c_str());
                continue;
            }
            bool ismem = p.second.c != nullptr;
            if (!ismem) {
                // handle register
                vector<s_map> replist = {
                   s_map("PRED", (diff < PCR.size() ? 
                   SVA_TEMPLATES::pc_stage_0 : SVA_TEMPLATES::pc_stage_1 )),
                   s_map("PCREG", 
                   diff < PCR.size() ? PCR[diff] : PCR[PCR.size()-1]),
                   s_map("PCVAL", "PC_act_sim1"),
                   s_map("REG", p.first), // as chunk
                   s_map("RESET", reset_s),
                   s_map("CLK", clk_s),
                   s_map("CNT", cnt_intra_hbi),
                };
                string ever_update_out = replace_template(SVA_TEMPLATES::ever_update, replist);
                ever_update_s.push_back(ever_update_out);
                ever_update_meta_list.push_back("r;" + std::to_string(cnt_intra_hbi) + ";" + p.first + "\n");
                cnt_intra_hbi++; 
            } else {
                // check previous cycle WEN, assume sync write
                diff--;
				log("check WR_EN\n----------\n");

				auto sig_wen = p.second.c->getPort(ID::WR_EN).extract(0,1);
				int sig_wen_bitw = sig_wen.size();
                string s_ss = log_signal(sig_wen);
                string verilog_ = "";
                if (s_ss[0] == '\\') {
                    s_ss = s_ss.substr(1);
                } else {
                    s_ss = "\\" + s_ss;
                    verilog_ = design_cdfg_info.get_sig_verilog(sig_wen);
                    log("jjj %s\n", s_ss.c_str());
                }
                auto w_addr = p.second.c->getPort(ID::WR_ADDR);
                auto w_addr_trans = design_cdfg_info.get_verilog(w_addr);
                string addr_ = w_addr_trans.first;
                if (w_addr.is_chunk()) {
                    auto chk = w_addr.as_chunk();
                    if (chk.wire != nullptr) w_addr = chk.wire;
                }
                addr_ += " <= " + std::to_string(w_addr.size()) + "'d" + std::to_string(w_addr.size() > 5 ? 127 : 31);
                if (!w_addr_trans.second.empty()) {
                    verilog_ += "\n" + w_addr_trans.second;
                }

                if (p.first.find(percore_prefix) == std::string::npos) {
                    log("eventual progress to remote %s\n", p.first.c_str());
                    
                    USPEC_SVA uspec_sva_;
                    const int wsz = 32;
                    auto pc_val1 = uspec_sva_.addConst(wsz, "pc");
                    auto pcr0 = EXPR_(PCR[0]);
                    auto nonz_e = EXPR_("!= 0", &pc_val1, nullptr);
                    uspec_sva_.addProperty(assume, nonz_e);

                    auto pc1_at_pcr0 = EXPR_("==", &pcr0, &pc_val1);
                    auto neg_pc1_at_pcr0 = EXPR_("!", &pc1_at_pcr0);
                    auto i1 = uspec_sva_.addConst(wsz, "i");
                    //for (auto &v_: valid_exe_condition[idx]) {
                    //    uspec_sva_.addProperty(assume, EXPR_(v_, &i1, nullptr)); // TODO, currrently assume IFR v_ (eg. v_ = "[13:10] == 4'b0000)
                    //}
                    auto ifr = EXPR_(instn_reg_name);
                    auto i1_at_ifr0 = EXPR_("==", &ifr, &i1);
                    uspec_sva_.addImmImpli(pc1_at_pcr0, i1_at_ifr0);

                    auto nop = EXPR_("== `RV_NOP", &ifr, nullptr);
                    auto pre = EXPR_("&&", &nop, &neg_pc1_at_pcr0);
                    uspec_sva_.addInOrderPartial(assume, pre, pc1_at_pcr0, true); 

                    auto arbiter = EXPR_(core_select);
                    uspec_sva_.addProperty(assume, EXPR_("== 0", &arbiter, nullptr));

                    auto rst_ = EXPR_(reset_s);
                    uspec_sva_.addProperty(assume, EXPR_("== 0", &rst_, nullptr));

                    uspec_sva_.addDeclaration(verilog_);
                    uspec_sva_.addImmImpli("first", "s_eventually ("+ s_ss + " == 1)", false, assert);
                    uspec_sva_.addProperty(assume, addr_);

                    eventual_progress_remote.push_back(make_pair(i1.str(), uspec_sva_.genstr()));
                    eventual_progress_remote_target.push_back(p.first);
                   
                }


                //string addr_ = "";
                //if (w_addr.is_chunk()) {
                //    auto chk = w_addr.as_chunk();
                //    if (chk.wire != nullptr)
                //        w_addr = design_cdfg_info.sigmap(chk.wire);
                //    log("jjjj %s %d\n", log_signal(w_addr), w_addr.size());
                //    addr_ = log_signal(w_addr) + 1;
                //    addr_ += " <= " + std::to_string(w_addr.size()) + "'d" + std::to_string(w_addr.size() > 5 ? 127 : 31);
                //    // TODO
                //    log("%s\n", addr_.c_str());
                //} 
                //// TODO: w_addr is hdlname and is_chunk.

                vector<s_map> replist = {
                   s_map("EXE", addr_),
                   s_map("PRED", (diff < PCR.size() ? 
                   SVA_TEMPLATES::pc_stage_0 : SVA_TEMPLATES::pc_stage_1 )),
                   s_map("PCREG", 
                   diff < PCR.size() ? PCR[diff] : PCR[PCR.size()-1]),
                   s_map("PCVAL", "PC_act_sim1"),
                   s_map("EXPR", s_ss), // as chunk
                   s_map("RESET", reset_s),
                   s_map("CLK", clk_s),
                   s_map("CNT", cnt_intra_hbi),
                };
                string ever_update_out = replace_template(SVA_TEMPLATES::ever_update_m, replist);
                ever_update_s.push_back(verilog_ + "\n\n" + ever_update_out);
                ever_update_meta_list.push_back("m;" + std::to_string(cnt_intra_hbi) + ";" + p.first + "\n");
                cnt_intra_hbi++; 
                // check WR_EN otherwise the update (v == $past(v)) may be because previous instruction ..
            }
        }  
    }
    void intra_hbi_instn(const pool<string> &exe_condition, int &idx) {
        idx_ = idx;
		string instn_semantics = "(1 ";
		for (auto &itm: exe_condition) {
			instn_semantics += " && sim_top_1_instn_val" + itm;
		}
		instn_semantics += " )";
	    string headers_ = replace_template(SVA_TEMPLATES::input_seq_intrahbi, {
            s_map("CLK", clk_s), s_map("PC1", PCR[0]), s_map("INST", instn_reg_name), s_map("END", PCR.back()), s_map("RESET", reset_s),
        });
		string header_t = replace_template(headers_, {
			s_map("VALID_EXE_ASMP", instn_semantics),
			s_map("ANY", "")
		});
		std::ofstream everupdate_file(intra_hbi_folder + "ever_update_" + std::to_string(idx) + ".sv", std::ios::out);
		everupdate_file << header_t << "\n";

        for (auto &assertion: ever_update_s) 
    		everupdate_file << assertion << "\n";
		everupdate_file.close();

		std::ofstream test_map(intra_hbi_folder + "ever_update_" + std::to_string(idx) + ".txt" , std::ios::out);
		for (auto &hbi_itm: ever_update_meta_list) 
			test_map << hbi_itm;
        test_map.close();
        meta_file = intra_hbi_folder + "ever_update_" + std::to_string(idx) + ".txt";

        for (int r = 0; r < eventual_progress_remote.size(); r++) {
            auto itm = eventual_progress_remote[r];
            string i_semantic = "(1 ";
            for (auto &c_: exe_condition) {
                i_semantic  += " && " + itm.first + c_;
            }
            i_semantic += " )";
            string sva_ = replace_template(itm.second, {s_map("CLK", clk_s), s_map("RESET", reset_s)});
            sva_ += ("\nassume property (@(posedge " + clk_s + ") " + i_semantic + ");");

            std::ofstream test_(intra_hbi_folder + "eventual_remote_" + std::to_string(r) + "_" + std::to_string(idx) + ".sv" , std::ios::out);
            test_ << sva_;
            test_.close();

        }
        
    }

    void parse_intra_hbi_res() {
        std::ifstream result_f; 
        while (true) {
            result_f.open(meta_file + ".res", std::ios::in);
            if (result_f.good()) break; 
            std::this_thread::sleep_for(std::chrono::seconds(2) ); // sleep for 2 sec
            log("wait...\n");
        }
        log("--------------- parse ever_update result ---------------\n");

        stage.clear();
		stage.resize(max_cyc_diff+1);
        sva_results.clear();
        sva_results_m.clear();
        log("%d\n", max_cyc_diff);

        string line;
        while (getline(result_f, line)) {
            if (line.size() > 0 && !(line[0] == 'r' || line[0] == 'm'))
                continue;
            bool is_mem = (line[0] == 'm');
            size_t pos = 0, prev = 0;
            vector<string> tokens; 
            while ((pos = line.find(';', prev)) != std::string::npos) {
                std::string token = line.substr(prev, pos - prev);
                prev = pos + 1;
                tokens.push_back(token);
            }
            
            tokens.push_back(line.substr(prev));
            assert(tokens.size() >= 3);
            int seq = std::stoi(tokens[1]);
            string idstring = tokens[2];

            if (cyc_diff.find(idstring) == cyc_diff.end()) {
                log("can't find idstring %s in cyc_diff\n", idstring.c_str());
                assert(0);
            }
            auto tar_sig = cyc_diff[idstring];
            int cyc_diff = tar_sig.diff;
            // TODO
            // if is_mem and tokens[3] == "updated" -> check eventual_remote as well
            sva_result n = { .type = is_mem ? arr : ff, 
                             .res = (tokens[3] == "updated") };
            assert(
                    (is_mem && tar_sig.c != nullptr) || 
                    (!is_mem && !tar_sig.s.empty())
                  );
            if (n.res) {
                stage[cyc_diff].insert(tar_sig);
            }

            if (is_mem) {
                RTLIL::Cell* mcell = tar_sig.c;
                sva_results_m[mcell] = n;
                sva_results_m_stage[mcell] = cyc_diff;
            } else {
                RTLIL::SigSpec s_ = tar_sig.s;
                sva_results[s_] = n; 
                log("add %s %d\n", log_signal(s_), n.res);
                sva_results_stage[s_] = cyc_diff; 
            }
        } // over lines in results	
        result_f.close();
        log("intra hbi result: \n");
        for (int i = 0; i < stage.size(); i++) {
            log("stage i=%d\n", i);
            for (auto &p: stage[i])
                log("%s,", p.str().c_str());
            log("\n");
        }
        log("-----------\n");
    } 
    void build_dfg(dict <RTLIL::SigSpec, pool<RTLIL::SigSpec>> &design_reg_reg,
                   dict <RTLIL::SigSpec, pool<MemPort_>> &design_reg_mem,
                    RTLIL::Module *module_, SigMap sigmap_,
                    dict<RTLIL::Cell*, pair<RTLIL::Cell*, RTLIL::SigSpec>> rd_data_to_wports, 
                    dict<RTLIL::SigSpec, pool<pair<RTLIL::Cell*, RTLIL::SigSpec>>> reg_to_upstream_rdport
                   ) {
        log("--------------- build dataflow grpah ---------------\n");
        memgraph = MyGraph();
        uhb_graph = MyGraph();

        auto instn_sig = get_sig_by_str("\\" + instn_reg_name, module_, sigmap_);
        auto pcr0_sig = get_sig_by_str("\\" + PCR[0] , module_, sigmap_);
        vector<pair<RTLIL::SigSpec, RTLIL::Cell*>> tmp_stage_sig;	// for anchor same stage
        tmp_stage_sig.resize(stage.size());
        tmp_stage_sig[0].first = instn_sig;
        string u_node, v_node;

        memgraph.samerank(instn_reg_name, instn_reg_name);
        uhb_graph.samerank(instn_reg_name, instn_reg_name);
        uhb_graph.samerank(PCR[0], instn_reg_name);
        memgraph.samerank(PCR[0], instn_reg_name);

        // uhb nodes regs
        dict <RTLIL::SigSpec, pool<RTLIL::SigSpec>> design_reg_reg_include;
        dict <RTLIL::SigSpec, pool<MemPort_>> design_reg_mem_include;

        for (auto &pair_: design_reg_reg) {
            auto sig_u = pair_.first;
            if (!(sva_results[sig_u].res) && !(sig_u == instn_sig || sig_u == PCR[0])) continue;
            for (auto &sig_v: pair_.second) {
                if (!(sva_results[sig_v].res)) continue;
                if (!(sva_results_stage[sig_u] == sva_results_stage[sig_v] - 1)) continue;
                design_reg_reg_include[sig_u].insert(sig_v);

                int j = sva_results_stage[sig_v];
                uhb_graph.addEdge(sig_u, sig_v, "", "", "");
                memgraph.addEdge(sig_u, sig_v, "", "", "");
                if (!tmp_stage_sig[j-1].first.empty()) {
                    uhb_graph.samerank(tmp_stage_sig[j-1], sig_u);
                    memgraph.samerank(tmp_stage_sig[j-1], sig_u);
                } else {
                    tmp_stage_sig[j-1].first = sig_u;
                }
                if (!tmp_stage_sig[j].first.empty()) {
                    uhb_graph.samerank(tmp_stage_sig[j], sig_v);
                    memgraph.samerank(tmp_stage_sig[j], sig_v);
                } else {
                    tmp_stage_sig[j].first = sig_v;
                }
            }
        }
        for (auto &pair_: design_reg_mem) {
            auto sig_u = pair_.first;
            if (!(sva_results[sig_u].res)) continue;
            for (auto &mptr: pair_.second) {
                auto m = mptr.cell_;
                if (!(sva_results_m[m].res)) continue;
                if (!(sva_results_stage[sig_u] == sva_results_m_stage[m] - 1)) continue;
                design_reg_mem_include[sig_u].insert(mptr);

                int j = sva_results_m_stage[m];

                uhb_graph.addEdge(sig_u, m, "", "", "");	
                memgraph.addEdge(sig_u, m, "", "", "");	
                if (!tmp_stage_sig[j-1].first.empty()) {
                    uhb_graph.samerank(tmp_stage_sig[j-1], sig_u);
                    memgraph.samerank(tmp_stage_sig[j-1], sig_u);
                } else {
                    tmp_stage_sig[j-1].first = sig_u;
                }
                if (!tmp_stage_sig[j].first.empty()) {
                    uhb_graph.samerank(tmp_stage_sig[j], m);
                    memgraph.samerank(tmp_stage_sig[j], m);
                } else {
                    tmp_stage_sig[j].second = m;
                    uhb_graph.samerank(tmp_stage_sig[j], m);
                    memgraph.samerank(tmp_stage_sig[j], m);
                }

            }
        }

        std::ofstream tmpfile_uhb("./build/img/uhb_" + std::to_string(idx_) + ".dot", std::ios::out);
        uhb_graph.finish(tmpfile_uhb);
        tmpfile_uhb.close();
        log("-------------uhb grpah done %d --------\n", idx_);
        memgraph.uhb_anchor(instn_reg_name);

        for (auto &pair_: design_reg_reg_include) {
            auto sig_u = pair_.first;
            for (auto &sig_v: pair_.second) {
                int j = sva_results_stage[sig_v];
                if (reg_to_upstream_rdport.find(sig_v) != reg_to_upstream_rdport.end()) {
                    for (auto &pair_: reg_to_upstream_rdport[sig_v]) {
                        // pair_.first : cell 
                        // pair_.second : rd_addr
                        auto rd_addr = pair_.second;
                        // remove bit select just keep wire
                        if (rd_addr.is_chunk()) {
                            auto chk = rd_addr.as_chunk();
                            if (chk.wire != nullptr)
                                rd_addr = sigmap_(chk.wire);
                        }
                        if (!tmp_stage_sig[j-1].first.empty()) {
                            memgraph.samerank(tmp_stage_sig[j-1], pair_.first);
                            memgraph.samerank(tmp_stage_sig[j-1], rd_addr);
                        } else {
                            tmp_stage_sig[j-1].first = rd_addr;
                            memgraph.samerank(rd_addr, pair_.first);
                        }
                        if (!tmp_stage_sig[j].first.empty()) {
                            memgraph.samerank(tmp_stage_sig[j], sig_v);
                        } else {
                            tmp_stage_sig[j].first = sig_v;
                        }
                        memgraph.addEdge(pair_.first, sig_v, "", "", "color=red, label=\"rdata\"", 0);
                        memgraph.addEdge(rd_addr, sig_v, "", "", "color=red, label=\"rdata\"", 0);
                    }
                    
                }

            }
        }
        for (auto &pair_: design_reg_mem_include) {
            auto sig_u = pair_.first;
            for (auto &mptr: pair_.second) {
                auto m = mptr.cell_;
                int j = sva_results_m_stage[m];
                
                if (rd_data_to_wports.find(m) != rd_data_to_wports.end()) {
                    memgraph.samerank(tmp_stage_sig[j-1], rd_data_to_wports[m].second);
                    memgraph.samerank(tmp_stage_sig[j-1], rd_data_to_wports[m].first);
                    memgraph.addEdge(rd_data_to_wports[m].second, m, "", "", "color=red, label=\"wrrdata\"");
                    memgraph.addEdge(rd_data_to_wports[m].first, m, "", "", "color=red, label=\"wrrdata\"");
                }

                uhb_graph.addEdge(sig_u, m, "", "", "");	
                memgraph.addEdge(sig_u, m, "", "", "");	
                if (!tmp_stage_sig[j-1].first.empty()) {
                    uhb_graph.samerank(tmp_stage_sig[j-1], sig_u);
                    memgraph.samerank(tmp_stage_sig[j-1], sig_u);
                } else {
                    tmp_stage_sig[j-1].first = sig_u;
                }
                if (!tmp_stage_sig[j].first.empty()) {
                    uhb_graph.samerank(tmp_stage_sig[j], m);
                    memgraph.samerank(tmp_stage_sig[j], m);
                } else {
                    tmp_stage_sig[j].second = m;
                    uhb_graph.samerank(tmp_stage_sig[j], m);
                    memgraph.samerank(tmp_stage_sig[j], m);
                }

            }
        }

        std::ofstream outfile("./build/img/mem_dataflow_" + std::to_string(idx_) + ".dot", std::ios::out);
        memgraph.finish(outfile);
        outfile.close();
        log("deps_anchor\n");
        memgraph.deps_anchor(instn_reg_name);
    }
    
    

};

int DFG::cnt_intra_hbi = 0;
int DFG::max_cyc_diff = 0;
dict<string, u_sig_cell> DFG::cyc_diff = dict<string, u_sig_cell>();
vector<string> DFG::ever_update_s = vector<string>();
vector<string> DFG::ever_update_meta_list = vector<string>();
vector<pair<string,string>> DFG::eventual_progress_remote = vector<pair<string, string>>();
vector<string> DFG::eventual_progress_remote_target = vector<string>();


PRIVATE_NAMESPACE_END

#endif 
