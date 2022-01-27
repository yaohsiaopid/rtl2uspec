#ifndef DUMPUSPEC_H 
#define DUMPUSPEC_H
USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

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
#include "inter_hbi.h"

string percore_prefix_;
struct hbi_sets {
	dict<string, pool<struct hbi_res>> mapping;
	hbi_type_ hbi_type;
	void insert(string key, struct hbi_res itm) {
		mapping[key].insert(itm);
	}
	
	pool<struct hbi_res> get(string key) { return mapping[key];}

	void dump() {
		log("size: %d\n", mapping.size());
		for (auto pair: mapping) {
			log("%s %d\n", pair.first.c_str(), pair.second.size());
		}
		log("------------");
	}
	int size() { return mapping.size(); }
	bool compare_spatial(const string &loc1, const string &loc2) {
		auto pool1 = mapping[loc1];
		auto pool2 = mapping[loc2];
		if (pool1.size() != pool2.size()) {
			//log("compare_spatial size diff %d %d\n ", pool1.size(), pool2.size());
			return false;
		}
		bool ret = true;
		for (auto &itm: pool1) {
			bool exist = false;
			for (auto &itm2: pool2) {
				if (itm2.i1_idx == itm.i1_idx && itm2.i2_idx == itm.i2_idx) {
					ret = ret && (itm2.assertion_res == itm.assertion_res); // TODO &&  (itm.hbi_result == ppo ||  itm.hbi_result == ppoopp || itm.hbi_result == noorder);
					exist = true;
					break;
				}
			}
			if (!exist) return false;
		}
		return ret; 
	}

	bool compare_diff(const string &loc1, const string &loc2, const string &dbmsg) {
		auto pool1 = mapping[loc1];
		auto pool2 = mapping[loc2];


        bool local_datadep = false;        
		for (auto &itm: pool1) {
            if (itm.hbi_type == data && itm.samecore) {
                local_datadep = true;
                break;
            }

        }

        //// TODO: REMOVE -> to comply with submission
        if (dbmsg == "compare_datadeep" 
           // && pool1.size() != pool2.size() 
            && local_datadep ) {
           // && loc1.find(percore_prefix_) == std::string::npos) {
            log("2fuzzing %s %s\n", loc1.c_str(), loc2.c_str());
            return true;
        }
        //if (dbmsg == "compare_datadeep" && loc1.find("p0_waddr") != string::npos)  {
        //    log("fuzzing %s %s\n", loc1.c_str(), loc2.c_str());
        //    return true;
        //}
		if (pool1.size() != pool2.size()) {
			log("%s size diff %d %d\n ", dbmsg.c_str(), pool1.size(), pool2.size());	 
			return false;
		}
		// log("diff loc1 %s loc2 %s \n", loc1.c_str(), loc2.c_str());
		bool ret = true;
		for (auto &itm: pool1) {
			bool exist = false;
			if (itm.i2_loc == loc2 || itm.i1_loc == loc2) continue;
			for (auto &itm2: pool2) {
				string mutual_1 = (itm.i1_loc == loc1 ? itm.i2_loc : itm.i1_loc);
				string mutual_2 = (itm2.i1_loc == loc2 ? itm2.i2_loc : itm2.i1_loc);
				if (mutual_1 == loc2 || mutual_2 == loc1) continue;
				if (mutual_1 == mutual_2 && itm2.i1_idx == itm.i1_idx && itm2.i2_idx == itm.i2_idx) {
					exist = true;
					ret = ret && (itm2.assertion_res == itm.assertion_res); // TODO &&  (itm.hbi_result == ppo ||  itm.hbi_result == ppoopp || itm.hbi_result == noorder);
				}
			}
			if (!exist) { 
				log("diff loc1 %s loc2 %s \n", loc1.c_str(), loc2.c_str());
			log("diff loc1 %s \n", itm.tostr().c_str());
			log("not exists?\n"); assert(0); 
			return false; 
			}
			//  else { log("pass,");}
		}

		return ret;
	}

};

bool compare_prop(const string &loc1, const string &loc2, bool ismem1, bool ismem2, 
struct hbi_sets &inter_struc_spatial,
struct hbi_sets &inter_struc_tmeporal,
struct hbi_sets &inter_datadep,
string percore_prefix) {
	// return whether to merge loc1 and loc2
    log("\t\t try merge %s %s\n", loc1.c_str(), loc2.c_str());

	// TODO reduce for
	bool ret = true;

	// resource count (only uniquefy shared array)
	bool loc1_isshared_arr = (loc1.find(percore_prefix) == std::string::npos) && ismem1;
	bool loc2_isshared_arr = (loc2.find(percore_prefix) == std::string::npos) && ismem2;
	if (loc1_isshared_arr != loc2_isshared_arr) return false;

	// --- spatial_match = (inter_hbi.structural.spatial[N1] = inter_hbi.structural.spatial[N2]) ---
	ret = ret && inter_struc_spatial.compare_spatial(loc1, loc2);
	if (!ret) return ret;

	// --- temporal_match = (inter_hbi.structural.temporal[N1] = inter_hbi.structural.spatial[N2])
	ret = ret && inter_struc_tmeporal.compare_diff(loc1, loc2, "compare_temporal");
	
	if (!ret) { 
        //log("%s %sfails at temporal\n", loc1.c_str(), loc2.c_str()); 
        return ret; 
    }

	// --- datadep_match = (inter_hbi.dataflowdep[N1] = inter_hbi.dataflowdep[N2])	
	ret = ret && inter_datadep.compare_diff(loc1, loc2, "compare_datadeep");
	if (!ret) {
        //log("%s %sfails at datadep\n", loc1.c_str(), loc2.c_str()); 
        return ret;
    }
	return ret;
}
// uspec_lang
struct shared_resource_ {
    dict<string, pair<bool, bool>> imap; // name -> (serialization ? , rf  exist ? )
    dict<string, pool<string>> rf_target;
    void addws(string key, bool ws) {
        if (imap.find(key) == imap.end())
            imap[key] = make_pair(true, false); 
        else {
            auto tmp_pair = imap[key];
            tmp_pair.first = true;
            imap[key] = tmp_pair; 
        }
    }
    void addrf(string key, bool rf, string target) {
        if (imap.find(key) == imap.end())
            imap[key] = make_pair(false, true); 
        else {
            auto tmp_pair = imap[key];
            tmp_pair.second = true;
            imap[key] = tmp_pair; 
        }
        rf_target[key].insert(target);
    }
} ;

struct dump_uarch {
	dict<string, string> mega_node_names; 
	int mega_node_seq;
	void dump(std::ofstream &f) {
		for (auto &itm: mega_node_names) 
			f << "%" << itm.first << "," << itm.second << "\n";
	}
	void add_mnode(string nm, string mgname="") {
		mega_node_names[nm] = mgname;
	}
	string get_tname(string nm, bool is_shared) {
		const string nm_tmp = is_shared ? "(0, " + nm + ")" : nm;
		string trans_ = nm_tmp;
		if (!is_shared) {
			if (mega_node_names.find(nm_tmp) == mega_node_names.end()) {
				trans_ = "mgnode_" + std::to_string(mega_node_seq++);
				mega_node_names[nm_tmp] = trans_;
			} else {
				trans_ = mega_node_names[nm_tmp];
			}
		}
		return trans_;
	}

};
// uspec_lang

class USPEC_GEN {
    Inter_HBI hypos_;
    dict<int, assertion_res_> sva_result;
    struct hbi_sets inter_struc_spatial, inter_struc_tmeporal, inter_datadep;
public:

    string uarch;
    shared_resource_ shared_resource;
    string stage_name_s = "";
    dump_uarch pipeline_uarch;
    int hbi_inter_cnt;
    string hbi_inter_s; 
    USPEC_GEN(Inter_HBI &hypos): uarch(""), stage_name_s(""), hypos_(hypos), hbi_inter_cnt(0), hbi_inter_s("") {}
    USPEC_GEN(): uarch(""), stage_name_s(""), hbi_inter_cnt(0), hbi_inter_s("") { hypos_ = Inter_HBI(); };
        void place_bin(struct hbi_res itm, 
    struct hbi_sets &inter_struc_spatial,
    struct hbi_sets &inter_struc_tmeporal,
    struct hbi_sets &inter_datadep) {
        switch (itm.hbi_type)
        {
        case structural_spatial:
            assert(itm.i1_loc == itm.i2_loc);
            inter_struc_spatial.insert(itm.i1_loc, itm);
            // inter_struc_spatial	// same location
            break;
        case structural_temporal:
            if (itm.i1_loc == itm.i2_loc) {
                // is array
                log("structural_temporal cases: %s %s\n", itm.i1_loc.c_str(), itm.i2_loc.c_str());
            }
            inter_struc_tmeporal.insert(itm.i1_loc, itm);
            inter_struc_tmeporal.insert(itm.i2_loc, itm);
            break;
        case data:
            // usually itm.i1_loc != itm.i2_loc(update with value read from i1_loc)
            inter_datadep.insert(itm.i1_loc, itm);
            inter_datadep.insert(itm.i2_loc, itm);
            
            break;
        default:
            assert(0);
            break;
        }
    }

    pair<int, enum assertion_res_> get_res(string line) {
        int idx = 0;
        while(idx < line.size() && line[idx] != ',') idx++;
        int sva_idx = std::stoi(line.substr(0, idx));

        idx = line.size() - 1;
        while(idx >= 0 && line[idx] != ',') idx--;
        assertion_res_ t_;
        if (line.substr(idx+1) == "proven") 		 	
            t_ = proven;
        else if (line.substr(idx+1) == "cex")
            t_ = cex; 
        else if (line.substr(idx+1) == "undetermined")
            t_ = undetermined;
        else if (line.substr(idx+1) == "error")
            t_ = error; 
        else 
            t_ = na;
        return make_pair(sva_idx, t_);
    }

    void parse_result() {
        // hbi_meta.txt.res
		assert(hypos_.hbi_checkings.size() > 0);
		std::ifstream hbi_res_txt; 
		while (true) {
			hbi_res_txt.open(folder_prefix + "hbi_meta.txt.res");
			if (hbi_res_txt.good()) break; 	
			std::this_thread::sleep_for(std::chrono::seconds(2) ); // sleep for 2 sec
            log("wait %s hbi_meta.txt.res", folder_prefix.c_str());
		}

        inter_struc_spatial.hbi_type = structural_spatial;
        inter_struc_tmeporal.hbi_type = structural_temporal;
        inter_datadep.hbi_type = data;

		log("inferred");
		for (auto &itm: hypos_.hbi_inferred) {
			log("%s\n", itm.tostr().c_str());
			place_bin(itm, inter_struc_spatial, inter_struc_tmeporal, inter_datadep);
		}
		log("--------------\n");

		string line;
        // -----------------populate sva_result ----------------------------------
		getline(hbi_res_txt, line); // skip header line 
        while(1) {
            getline(hbi_res_txt, line);
			if(line.size() == 0) 
				break;
            auto p_ = get_res(line);
            log("result sva idx%d %d\n", p_.first, p_.second == proven);
            sva_result[p_.first] = p_.second;
        }
        // -------------------------------------------------------

		enum assertion_res_ stack_res;
		bool track = false;
		for (auto &itm: hypos_.hbi_checkings) {
            assertion_res_ t_ = na;
            if (sva_result.find(itm.file_seqno) != sva_result.end()) 
                t_ = sva_result[itm.file_seqno];
            else 
                log("not found %d\n", itm.file_seqno);
			if (itm.file_seqno_next != -1) {
				if (track) {
                    stack_res = (t_ == stack_res ? stack_res : na);
                } else { 
					stack_res = t_; 
					track = true;
				}
			}
			if (itm.file_seqno_next == -2 || itm.file_seqno_next == -1) {
                if (track) {
                    log("track %s\n", itm.tostr().c_str());
                } 
				if (track) itm.assertion_res = stack_res;
				else itm.assertion_res = t_; //get_res(itm, line); // check if corresponding line and get result 
				track = false;
				place_bin(itm, inter_struc_spatial, inter_struc_tmeporal, inter_datadep);
			}
		}
        log("-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=");
		inter_struc_spatial.dump();
		inter_struc_tmeporal.dump();
		inter_datadep.dump();


    }
    void uspec_dump_structural_spatial(hbi_res &itm, bool ismem_, bool is_shared) {
        if (is_shared && !ismem_) log("wwww spatial %s\n", itm.i1_loc.c_str());
        const string same_core_pred = is_shared ? "~SameMicroop i1 i2 => " : "SameCore i1 i2 => EdgeExists((i1, INSTNREG), (i2, INSTNREG), \"\") =>";
        string i1loc_ = pipeline_uarch.get_tname(itm.i1_loc, is_shared);
        string i2loc_ =  pipeline_uarch.get_tname(itm.i2_loc, is_shared); 

        const string op1 = opcodes_name[itm.i1_idx] == "load" ? "Read" : "Write";
        const string op2 = opcodes_name[itm.i2_idx] == "load" ? "Read" : "Write";
        string axiom = R"(
Axiom "HBI_SEQNO": forall microop "i1",  forall microop "i2", 
SAMECORE IsAnyOP1 i1 => IsAnyOP2 i2 => SAMEADDR)";
        if (itm.file_seqno == -1) {
            if (is_shared) {
                // since already proven in uhb node
                axiom += R"(
AddEdge((i1, LOC1), (i2, LOC2), "infer_ws_shared_SEQNO", "red") \/ AddEdge((i2, LOC2), (i1, LOC1), "infer_ws_shared_SEQNO", "red"))";								
                // *** shared and same address *****
                shared_resource.addws(i1loc_, true);
                
            } else {
                axiom += R"(
AddEdge((i1, LOC1), (i2, LOC2), "infer_ws_shared_SEQNO", "red"))";					
            }
        } else if (itm.assertion_res == proven) {
            log("weird case %s\n", i1loc_.c_str());
            if (is_shared) {	
                log("a shared resource ? \n");
            }
            axiom += R"(AddEdge((i1, LOC1), (i2, LOC2), "ppo_SHARED_SEQNO", "blue"))";
        } else {
            log("[error] %s\n", itm.tostr().c_str());
            return;
        }
        vector<s_map> axiom_replist = {
            s_map("SAMECORE", same_core_pred),
            s_map("SEQNO", hbi_inter_cnt++),
            s_map("INSTNREG", pipeline_uarch.get_tname(instn_reg_name, false)),
            s_map("OP1", op1),
            s_map("OP2", op2),
            s_map("LOC1", i1loc_),
            s_map("LOC2", i2loc_),
            s_map("SAMEADDR", ismem_ ? "SamePhysicalAddress i1 i2 => " : ""),
            s_map("SHARED", is_shared ? "shared" : "percore"),
            s_map(".", "_"),
        };
        axiom = replace_template(axiom, axiom_replist);
        hbi_inter_s += axiom + "." + "% " + std::to_string(itm.file_seqno) + "\n";
    } // end uspec_dump_structural_spatial 

    void uspec_dump_inter(hbi_res &itm, bool ismem_1, bool ismem_2, bool is_shared1, bool is_shared2, bool is_data_dep = false ) {

        if (is_shared1 && !ismem_1) {
            is_shared1 = false;
            log("wwww inter %s\n", itm.i1_loc.c_str());
        }
        if (is_shared2 && !ismem_2) {
            is_shared2 = false;
            log("wwww inter %s\n", itm.i2_loc.c_str());
        }

        const string same_core_pred = !itm.samecore ? "~SameMicroop i1 i2 => " : "SameCore i1 i2 => EdgeExists((i1, INSTNREG), (i2, INSTNREG), \"\") =>";
        
        string i1loc_ = pipeline_uarch.get_tname(itm.i1_loc, is_shared1);
        string i2loc_ =  pipeline_uarch.get_tname(itm.i2_loc, is_shared2); 
        
        const string op1 = opcodes_name[itm.i1_idx] == "load" ? "Read" : "Write";
        const string op2 = opcodes_name[itm.i2_idx] == "load" ? "Read" : "Write";
        string axiom = R"(
Axiom "HBI_SEQNO": forall microop "i1",  forall microop "i2",
SAMECORE IsAnyOP1 i1 => IsAnyOP2 i2 => SAMEADDR NONINTERVEN
AddEdge((i1, LOC1), (i2, LOC2), "TYPESEQNO", "blue"))";
        if (itm.assertion_res == proven) {  // prerserve i1 update i1loc before i2 update i2loc 
            string interven = "";
            if (itm.hbi_type == data) {
                interven = " ~(exists microop \"j\", IsAnyOP1 j /\\ SamePhysicalAddress i1 j /\\ EdgeExists((i1, LOC1), (j, LOC1), \"\") /\\ EdgeExists((j, LOC1), (i2, LOC2), \"\"))  => ";
                interven += "SameData i1 i2 => ";
            }
            vector<s_map> axiom_replist = {
                s_map("SAMECORE", same_core_pred),
                s_map("SEQNO", hbi_inter_cnt++),
                s_map("INSTNREG", pipeline_uarch.get_tname(instn_reg_name, false)),
                s_map("NONINTERVEN", interven),
                s_map("OP1", op1),
                s_map("OP2", op2),
                s_map("LOC1", i1loc_),
                s_map("LOC2", i2loc_),
                s_map("SAMEADDR", itm.hbi_type == data ? "SamePhysicalAddress i1 i2 => " : ""),
                s_map("TYPE", itm.hbi_type == data ? "rf_" : "tprl_"),
                s_map(".", "_") };
            log("add RF\n");
            axiom = replace_template(axiom, axiom_replist);
            if (itm.hbi_type == data) {
               if (is_shared1) {
                    log("shared type! %s", i2loc_.c_str());	
                    shared_resource.addrf(i1loc_, true, i2loc_);
               } else {
                   log("[TODO] RF on local array, and assume no cache so this is not an rf related to mem system\n");
                   string t_ = "%";
                   for (int idx = 0; idx < axiom.size(); idx++) {
                       t_ += axiom[idx];
                       if (axiom[idx] == '\n') t_ += "%";
                   }
                   axiom = t_;

               }
            }

            hbi_inter_s += axiom + "." + "% " + std::to_string(itm.file_seqno); 
            if (is_data_dep)
                hbi_inter_s += "\n % " + std::to_string(hbi_inter_cnt-1) + " IS_DATA_DEP";
        } else {
            log("[error] %s\n", itm.tostr().c_str());
        }
     } // uspec_dump_inter end

    void header_inferred() {
		string po_ = R"(
% ProgramOrder 
Axiom "PO_man": forall microop "i1",  forall microop "i2",
SameCore i1 i2 => ProgramOrder i1 i2 => AddEdge ((i1, INSTNREG), (i2, INSTNREG), "PO", "orange"))";
		substr_replace(po_, "INSTNREG", pipeline_uarch.get_tname(instn_reg_name, false));
		substr_replace(po_, ".", "_");
		uarch += "\n";
		uarch += hbi_inter_s;
		uarch = stage_name_s + "\n" + po_ + ".\n" + uarch;
		int cnt_s_rsc = 0;
		log("shared_resource:\n");
		for (auto &itm: shared_resource.imap) {
			log("%s %d\n", itm.first.c_str(), shared_resource.rf_target[itm.first].size());
			auto pair_ = itm.second;
			// ws + rf
			if (pair_.first && pair_.second) {
				string fr_ = R"(
% -------------------- inferred ----------------------------
Axiom "co_rf_fr_CNT": forall microop "i1",  forall microop "i2", 
~SameMicroop i1 i2 =>  IsAnyWrite i1 => IsAnyRead i2  => SamePhysicalAddress i1 i2 => DataFromInitialStateAtPA i2 =>  AddEdge((i2, RFTARGET), (i1, KEY), "init_fr")+

Axiom "HBI_fr_CNT": forall microop "i1",  forall microop "i2", forall microop "i3", 
~ SameMicroop i1 i2 => ~ SameMicroop i1 i3 => ~ SameMicroop i3 i2 =>  
IsAnyWrite i1 => IsAnyRead i2 => SameData i1 i2 => ~SameData i2 i3 => 
IsAnyWrite i3 => SamePhysicalAddress i1 i2 =>  SamePhysicalAddress i1 i3 => 
EdgeExists((i1, KEY), (i2, RFTARGET), "www") => 
EdgeExists((i1, KEY), (i3, KEY), "rrr") => 
AddEdge((i2, RFTARGET), (i3, KEY), "ws_final", "red")+)";
			for (auto &tar: shared_resource.rf_target[itm.first])
				uarch += (replace_template(fr_, {s_map("CNT", cnt_s_rsc), s_map("KEY", itm.first), s_map("RFTARGET",tar), s_map(".", "_"), s_map("+", ".")})  );
			}
			// ws final
			if (pair_.first) {
				string po_final = R"(
Axiom "HBI_final_CNT": forall microop "i1",  forall microop "i2", 
~SameMicroop i1 i2 =>  IsAnyWrite i1 => IsAnyWrite i2 => SamePhysicalAddress i1 i2 => DataFromFinalStateAtPA i2 =>
AddEdge((i1, KEY), (i2, KEY), "ws_final", "red")+)"; 
				uarch += (replace_template(po_final, {s_map("CNT", cnt_s_rsc), s_map("KEY", itm.first), s_map(".", "_"), s_map("+", ".")}) );
			}
			cnt_s_rsc++;
		}
    } // end header_inferred

		
    void uhb_gen_merge(vector<DFG> &per_inst_DFG, string percore_prefix) { 
        percore_prefix_ = percore_prefix;
		pool<string> all_uhb_parents;

		// HBI checking done , parse results, merge nodes
		for (size_t idx = 0; idx < opcodes_name.size(); idx++) 
		{
            log("--------------------------------------------------\n");
            log("uhb_gen_merge on instruction %zu\n", idx);
			for (size_t dep = 0; dep < per_inst_DFG[idx].memgraph.uhb_sz(); dep++) {
                log("================== dep %zu ================================\n", dep);
                // in same depth within uhb path of an instruction, try to merge nodes 
				auto uhb_nodes = vector<string>(per_inst_DFG[idx].memgraph.uhb_item(dep).begin(), per_inst_DFG[idx].memgraph.uhb_item(dep).end());
				for (size_t i = 0; i < uhb_nodes.size(); i++) {
					for (size_t j = i + 1; j < uhb_nodes.size(); j++) {
						string s1 = uhb_nodes[i];
						string s2 = uhb_nodes[j];
						if (per_inst_DFG[idx].memgraph.ds_find_check(dep, s1, s2)) {
							//log("pass idx: %d\n", idx);
							continue;
						}
						if (compare_prop(s1, s2, per_inst_DFG[idx].memgraph.ismem(s1), per_inst_DFG[idx].memgraph.ismem(s2), 
						inter_struc_spatial, inter_struc_tmeporal, inter_datadep, percore_prefix)) {
							log("\t merge s1 s2: %s %s\n", s1.c_str(), s2.c_str());
							//per_inst_DFG[idx].memgraph.dsj_union(dep, s1, s2);
							for (size_t tmpidx = 0; tmpidx < opcodes_name.size(); tmpidx++) {
								auto pool_ = per_inst_DFG[tmpidx].memgraph.uhb_item(dep);
								if (pool_.find(s1) != pool_.end() && pool_.find(s2) != pool_.end()) {
									per_inst_DFG[tmpidx].memgraph.dsj_union(dep, s1, s2);
								}
							}
						}

					}
				}
				log("-----------  hbi merging %d %d ----------------- \n", idx, dep);
				auto tmppool = per_inst_DFG[idx].memgraph.dsj_dump(dep);
				all_uhb_parents.insert(tmppool.begin(), tmppool.end());
				log("-----------  hbi merging %d %d ----------------- \n", idx, dep);
			}
			
		}
		log("all_uhb_parents %d\n", all_uhb_parents.size());

		// dump 
        uarch = "";
		pool<string> stage_name;
		int cnt = 0;
		log("uarch dump\n");
		pool<hbi_res> visited;

        pipeline_uarch = {
			.mega_node_names=dict<string, string>(), 
			.mega_node_seq=0};
		pipeline_uarch.add_mnode(instn_reg_name, "IF_");

		for (size_t idx = 0; idx < opcodes_name.size(); idx++) {
            //log("dumping inter on %zu\n", idx);
			vector<pool<string>> uhb_intra;
			uhb_intra.push_back(pool<string>({instn_reg_name}));

			// uhb inter
			for (size_t dep = 0; dep < per_inst_DFG[idx].memgraph.uhb_sz(); dep++) {
				pool<string> stage_nodes; 
				auto mega_node_parents = per_inst_DFG[idx].memgraph.dsj_dump(dep);
				for (auto &p: mega_node_parents) {
                    log("node parent %s\n", p.c_str());

					// dump hbi res it is involved 
					// structural spatial 
                    //if (p == "hasti_mem.p0_wsize") {
                    //    log("[yhdb] %d\n", inter_struc_spatial.get(p).size());
                    //}
					for (auto &itm: inter_struc_spatial.get(p)) {
						assert(itm.i1_loc == itm.i2_loc);
						if (visited.find(itm) != visited.end()) { 
                            //log("[yhdb] pass \n");
                            continue;
                        }
						visited.insert(itm);
						bool ismem_ = per_inst_DFG[idx].memgraph.ismem(itm.i1_loc);
						bool is_shared = !itm.samecore && (itm.i1_loc.find(percore_prefix) == std::string::npos);
                        uspec_dump_structural_spatial(itm, ismem_, is_shared);
					
					}

					auto inter_pool_ = inter_struc_tmeporal.get(p);
					auto dp_pool_ = inter_datadep.get(p);
					//inter_pool_.insert(dp_pool_.begin(), dp_pool_.end());
					for (auto &itm: inter_pool_) {
						//log("itm%d\n", itm.file_seqno);
						if (all_uhb_parents.find(itm.i2_loc) == all_uhb_parents.end() || 
						all_uhb_parents.find(itm.i1_loc) == all_uhb_parents.end()) {
                            //log("not found in uhb_parents %s %s\n", itm.i1_loc.c_str(), itm.i2_loc.c_str());
                             continue;	
                        }
                        if (itm.file_seqno == -1) {
                            log("seqno = -1 %s \n", itm.tostr().c_str());
                        }
						if (itm.file_seqno != -1 && visited.find(itm) != visited.end())  {
                            //log("[yhdb] pass2;\n");
                            continue;
                        }
						visited.insert(itm);
						bool ismem_1 = per_inst_DFG[idx].memgraph.ismem(itm.i1_loc);
						bool ismem_2 = per_inst_DFG[idx].memgraph.ismem(itm.i2_loc);
						bool is_shared1 = (itm.i1_loc.find(percore_prefix) == std::string::npos);
						bool is_shared2 = (itm.i2_loc.find(percore_prefix) == std::string::npos);
                        uspec_dump_inter(itm, ismem_1, ismem_2, is_shared1, is_shared2);
                    
					}
                    // data dependency
					for (auto &itm: dp_pool_) {
						//log("itm%d\n", itm.file_seqno);
						if (all_uhb_parents.find(itm.i2_loc) == all_uhb_parents.end() || 
						all_uhb_parents.find(itm.i1_loc) == all_uhb_parents.end()) {
                            //log("not found in uhb_parents %s %s\n", itm.i1_loc.c_str(), itm.i2_loc.c_str());
                             continue;	
                        }
                        if (itm.file_seqno == -1) {
                            log("seqno = -1 %s \n", itm.tostr().c_str());
                        }
						if (itm.file_seqno != -1 && visited.find(itm) != visited.end())  {
                            //log("[yhdb] pass2;\n");
                            continue;
                        }
						visited.insert(itm);
						bool ismem_1 = per_inst_DFG[idx].memgraph.ismem(itm.i1_loc);
						bool ismem_2 = per_inst_DFG[idx].memgraph.ismem(itm.i2_loc);
						bool is_shared1 = (itm.i1_loc.find(percore_prefix) == std::string::npos);
						bool is_shared2 = (itm.i2_loc.find(percore_prefix) == std::string::npos);
                        uspec_dump_inter(itm, ismem_1, ismem_2, is_shared1, is_shared2, true);
                    
					}
					stage_nodes.insert(p);
                    
				}
				uhb_intra.push_back(stage_nodes);
			}
            // uhb intra
			string edges = "";
			const string edge_template = "((i, ND1), (i, ND2), \"PATHNAME\")";
			
			bool prev = false;
			for (size_t i = 0; i < uhb_intra.size() - 1; i++) {
				int j = i + 1;
				for (auto &node_i: uhb_intra[i]) {
					for (auto &node_j: uhb_intra[j]) {

						bool is_shared_i1 = node_i.find(percore_prefix) == std::string::npos  && per_inst_DFG[idx].memgraph.ismem(node_i) ;
						string i1loc_ =  pipeline_uarch.get_tname(node_i, is_shared_i1);
						
						bool is_shared_i2 = node_j.find(percore_prefix) == std::string::npos && per_inst_DFG[idx].memgraph.ismem(node_j);
						string i2loc_ = pipeline_uarch.get_tname(node_j, is_shared_i2);

						string e = "((i, LOC1), (i, LOC2), \"path_OPTAG\")";
						vector<s_map> e_replist = {s_map("LOC1", i1loc_), s_map("LOC2", i2loc_) };
						e = replace_template(e, e_replist);
						if (prev) 
							e = ";\n" + e;

						edges += e;
						prev = true;
						if (stage_name.find(i1loc_) == stage_name.end()) {
							string s_ = is_shared_i1 ? node_i : i1loc_;
							substr_replace(s_, ".", "_");
							stage_name_s += "StageName " + std::to_string(cnt++) + " \"" + s_ + "\".\n";
							stage_name.insert(i1loc_);
						} 
						if (stage_name.find(i2loc_) == stage_name.end()) {
							string s_ = is_shared_i2 ? node_j : i2loc_;
							substr_replace(s_, ".", "_");
							stage_name_s += "StageName " + std::to_string(cnt++) + " \"" + s_ + "\".\n";
							stage_name.insert(i2loc_);
						} 
					}
				}
			
			}
			substr_replace(edges, ".", "_");
			string uhb_intra_axiom = "Axiom \"intra_OPTAG\":\nforall microop \"i\", IsAnyOPTAG i => AddEdges [" + edges + "].";
			substr_replace(uhb_intra_axiom, "OPTAG", opcodes_name[idx] == "load" ? "Read" : "Write");
			uarch += uhb_intra_axiom;
			uarch += "\n";
        }

        header_inferred();

		std::ofstream outuarch("vscale.uarch", std::ios::out);
		outuarch << uarch << "\n";
		pipeline_uarch.dump(outuarch);

		outuarch.close();	
        
    }
};
PRIVATE_NAMESPACE_END
#endif
