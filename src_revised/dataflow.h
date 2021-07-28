#ifndef DATAFLOW_H
#define DATAFLOW_H
// dataflow relation (u, v), where at least one of u, v is state element

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
#include "sva.h"
#include "cdfg.h"


USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN



enum direction { celltoreg=0, regtocell=1, celltomem=2, memtocell=3};
enum edge_target { general=0, instn_dep=1 };

struct MemPort_ {
public: 
	RTLIL::Cell* cell_; 
    RTLIL::IdString mem_cell;
    RTLIL::SigSpec sig_name;
    RTLIL::IdString port_name;

    MemPort_(RTLIL::IdString mc, RTLIL::SigSpec sig, RTLIL::IdString port, RTLIL::Cell* mcell_): mem_cell(mc), sig_name(sig), port_name(port), cell_(mcell_) {}    
	
    inline std::string str() const {
        string ret = "mem (" + mem_cell.str() + "," + string(log_signal(sig_name)) + "," + port_name.str() + ")";
	    return ret; 
    }

	inline bool operator==(const MemPort_& other) const {
        // bool comparison = result of comparing 'this' to 'other'
        return (mem_cell == other.mem_cell && 
				sig_name == other.sig_name && 
				port_name == other.port_name && 
				cell_ == other.cell_);
    }
	inline unsigned int hash() const {
		return mkhash(mkhash(hash_ops<RTLIL::IdString>::hash(mem_cell),hash_ops<RTLIL::Cell*>::hash(cell_) ),
				mkhash(hash_ops<RTLIL::SigSpec>::hash(sig_name), hash_ops<RTLIL::IdString>::hash(port_name))
				);
	}
};

struct u_sig_cell {
	RTLIL::SigSpec s;
	RTLIL::Cell* c;	
	int diff; 
    inline bool operator==(const u_sig_cell& other) const {
        return (s == other.s && 
                c == other.c && 
                diff == other.diff);
    }
    inline unsigned int hash() const {
        return mkhash(
            mkhash(hash_ops<int>::hash(diff), hash_ops<RTLIL::Cell*>::hash(c)),
            hash_ops<RTLIL::SigSpec>::hash(s));
    }
    inline string str() {
        return (c != nullptr ? c->name.str(): string(log_signal(s))) + " " + std::to_string(diff);
    }
};

struct sort_u_sig_cell {
    bool operator()(const pair<string, u_sig_cell>& a,
                    const pair<string, u_sig_cell>& b) const {
            return a.second.diff < b.second.diff;
    }
};



class RTLDataFlow {
    SigMap sigmap_;
    Module* module_;
    pool<RTLIL::Cell*> visited, visited_up; // full cellDownRegs, DownMems and invserses

public: 
    
    dict <RTLIL::Cell*, pool<RTLIL::SigSpec>> cellDownRegs; // (comb ckt cell or mem or reg) to regs 
    dict <RTLIL::SigSpec, pool<RTLIL::Cell*>> invCellDownRegs;

    dict <RTLIL::SigSpec, pool<RTLIL::Cell*>> regsDownCells; // reg to (comb ckt or mem or reg)
    dict <RTLIL::Cell*, pool<RTLIL::SigSpec>> invRegsDownCells;

    dict <MemPort_, pool<RTLIL::Cell*>> memsDownRegs_; // mems outport (RD_DATA) to reg, cell
    dict <RTLIL::Cell*, pool<MemPort_>> invMemsDownRegs_;
    
    dict <RTLIL::Cell*, pool<MemPort_> > cellDownMems_; // (reg) to mems 
    dict <MemPort_, pool<RTLIL::Cell*>> invCellDownMems_;


    // dataflow relation
    dict <RTLIL::SigSpec, pool<RTLIL::SigSpec>> design_reg_reg_inv;
    dict <RTLIL::SigSpec, pool<RTLIL::SigSpec>> design_reg_reg;
    dict <RTLIL::SigSpec, pool<MemPort_>> design_reg_mem;
    dict<RTLIL::Cell*, pair<RTLIL::Cell*, RTLIL::SigSpec>> rd_data_to_wports; 
    // mem (RD_DATA) to mem (write ) 
    dict<RTLIL::SigSpec, pool<pair<RTLIL::Cell*, RTLIL::SigSpec>>> reg_to_upstream_rdport; 
    // register <key> depends on mem cell <cell, rd_addr> reg_to_upstream_rdport[r_].insert(make_pair(cell, sig_rd_addr));

	// generate two trace 
	//dict <RTLIL::SigSpec, int> cyc_diff; // inst |-> ## cyc_diff 
	//dict <RTLIL::Cell*, int> cyc_diff_m; // inst |-> ## cyc_diff  for memory id update 
    dict<string, u_sig_cell> two_trace_tar_sigs; // map from string to (SigSpec/Cell, cyc_diff)

    void stat() {
		log("cellDownRegs: %zu\n",  cellDownRegs.size());
		log("regsDownCells: %zu\n", regsDownCells.size());
		log("memsDownRegs_: %zu\n", memsDownRegs_.size());
		log("cellDownMems_: %zu\n", cellDownMems_.size());
    }

    void set(RTLIL::Module* module) {
        module_ = module;
        sigmap_.set(module_);
        visited.clear();
        visited_up.clear();
        design_reg_reg.clear();
        design_reg_mem.clear();
        rd_data_to_wports.clear();
        reg_to_upstream_rdport.clear();
    }

    //dict<RTLIL::SigSpec, int> 
    void output_port_cycdiff(dict<RTLIL::SigSpec, pool<RTLIL::Cell*>> sigToNextCell, dict<RTLIL::SigSpec, pool<RTLIL::Cell*>> invCellToNextSig, pool<RTLIL::SigSpec> output_sigs_outcore, 
    dict<RTLIL::SigSpec, pool<RTLIL::SigSpec>> connections_
            ) {
        // TODO

        for (auto &s: output_sigs_outcore) {
            auto j = sigmap_(s);
            assert(!j.empty());

            int min = 1000;
            pool<RTLIL::SigSpec> ups;
            ups.insert(j);
            pool<RTLIL::Cell*> cpool;
            cpool.insert(invCellToNextSig[j].begin(), invCellToNextSig[j].end());
            while(true) {
                bool new_ = false;
            for (auto &cc: connections_)  {
                auto driver = cc.first;
                for (auto &driven: cc.second) {
                    if (ups.find(sigmap_(driven)) != ups.end()) {
                        // && ups.find(sigmap_(driver)) == ups.end()) {
                        log("driver %s\n", log_signal(driver));
                        ups.insert(sigmap_(driver));
                        int tsz = cpool.size();
                        cpool.insert(invCellToNextSig[driver].begin(), invCellToNextSig[driver].end());
                        if (tsz != cpool.size())
                        new_ = true;
                    }
                    //if (sigmap_(driven) != j) continue;
                    //cpool.insert(invCellToNextSig[driver].begin(), invCellToNextSig[driver].end());
                }
            }
                if (!new_) break;
            }
            log("## %s next cells %d %d %d\n", log_signal(s), cpool.size(), invCellToNextSig[j].size(), sigToNextCell[j].size());

		    pool<RTLIL::SigSpec> output_driver;
            for (auto &c_: cpool) { //invCellToNextSig[j]) {
                if (isreg(c_)) {
                    auto q_ = c_->getPort(ID::Q);

                    if(two_trace_tar_sigs.find(log_signal(q_)+1) != two_trace_tar_sigs.end()) 
                    log("ff: %s\n", two_trace_tar_sigs[log_signal(q_)+1].str().c_str());
                    else 
                    log("can't find %s\n", log_signal(q_));

                } else if (ismem(c_)) {
                    log("---todom---\n");
                } else {
                    auto regs = invRegsDownCells[c_];
                    output_driver.insert(regs.begin(), regs.end());
                    log("---todo %s %d ---\n", c_->name.str().c_str(), output_driver.size());
                }
            }
            for (auto &q_: output_driver) {
                assert(two_trace_tar_sigs.find(log_signal(q_)+1) != two_trace_tar_sigs.end());
                log("dff: %s\n", two_trace_tar_sigs[log_signal(q_)+1].str().c_str());
            }
            log("--------\n");

        }
    }

    void add_edge(RTLIL::Cell* c, RTLIL::IdString mem, RTLIL::SigSpec sig, direction i, RTLIL::IdString port = ID::RD_DATA, edge_target tgt = general, RTLIL::Cell* cell_=nullptr) {
		MemPort_ p(mem, sig, port, cell_);
        if (i == celltomem) { // cell including reg/mem/cell to memory 
			// log("addedge cellDownMems %s %s\n", c->name.str().c_str(), p.str().c_str());
            cellDownMems_[c].insert(p);
            invCellDownMems_[p].insert(c);
        } else if (i == memtocell) {
            memsDownRegs_[p].insert(c);
            invMemsDownRegs_[c].insert(p);
        }
    }

    void add_edge(RTLIL::Cell* c, RTLIL::SigSpec sig, direction i, edge_target tgt = general) {
        
		if (i == celltoreg) {
            assert(!ismem(c));
            cellDownRegs[c].insert(sig);
            invCellDownRegs[sig].insert(c);
        } else if (i == regtocell) {
            regsDownCells[sig].insert(c);
            invRegsDownCells[c].insert(sig);
        }
    }

    void addVisited(RTLIL::Cell* c) {
        visited.insert(c);
    }

	bool check_exist(RTLIL::Cell* c, RTLIL::Cell* c_under_exam) {
		if (visited.find(c) == visited.end()) 
            return false;

        if (ismem(c_under_exam)) {
            // no port info 
            return false;
        }

		// copy c's info to c_under_exam
		if (cellDownRegs.find(c) != cellDownRegs.end()) {
			cellDownRegs[c_under_exam].insert(cellDownRegs[c].begin(), cellDownRegs[c].end());
			for (auto &s: cellDownRegs[c_under_exam]) {
				invCellDownRegs[s].insert(c_under_exam);
			}
		}
		if (cellDownMems_.find(c) != cellDownMems_.end()) {
			// if (ismem(c_under_exam)) log("addddddeddddge cellDownMems\n");
			cellDownMems_[c_under_exam].insert(cellDownMems_[c].begin(), cellDownMems_[c].end());
			for (auto &p: cellDownMems_[c_under_exam]) {
				// log("addeddddge cellDownMems %s %s %s\n", c->name.str().c_str(), c_under_exam->name.str().c_str(), p.str().c_str());
				invCellDownMems_[p].insert(c_under_exam);
			}
		}
		return true;
	}

    void in_edges() {
       design_reg_reg_inv.clear();
       for (auto &pr: design_reg_reg) {
           auto s = pr.first;
           for (auto &v: pr.second) {
               if (s != v) 
                   design_reg_reg_inv[sigmap_(v)].insert(sigmap_(s));
           }
       } 
       log("in_edges");
       for (auto &pr: design_reg_reg_inv) {
           log("%s %d\n", log_signal(pr.first), pr.second.size());
       } 
       log("---------");
    }  
    void get_next_pools(pool<RTLIL::SigSpec> &nextpool_c, 
         pool<MemPort_> &nextpool_m,
         const pool<RTLIL::SigSpec> &curpool, 
         const pool<RTLIL::SigSpec> &visited, 
         const pool<RTLIL::Cell* > visited_m) {
        nextpool_c.clear();
        nextpool_m.clear();
        for (auto &sig_: curpool) {
            auto regpool = design_reg_reg[sig_];
            for (auto &r_: regpool) {
                if (visited.find(r_) == visited.end())
                    nextpool_c.insert(r_);
            }
            auto mempool = design_reg_mem[sig_];
            for (auto &mptr_: mempool) {
                if (visited_m.find(mptr_.cell_) == visited_m.end())
                    nextpool_m.insert(mempool.begin(), mempool.end());
            }
        }
    };
    
    const dict<string, u_sig_cell> &sig_cyc() const {
        return two_trace_tar_sigs;
    }

    void find_reachable_set(const string &start, pool<RTLIL::Cell*> cells_ = pool<RTLIL::Cell*>()) {
        log("[dataflow] find reachble set %s \n", start.c_str());
        // assume start is register
        // TODO: to i$
        
        two_trace_tar_sigs.clear();

        auto start_sig = get_sig_by_str(start, module_, sigmap_);
        assert(!(start_sig.empty()));

       
        // BFS
        int cyc_diff = 1;
        pool<RTLIL::SigSpec> visited; 
        pool<RTLIL::Cell*> visited_m; 

        pool<RTLIL::SigSpec> curpool, nextpool_c; 
	    pool<MemPort_> curpool_m, nextpool_m;

        if (cells_.size() > 0) {
            for (auto &c_: cells_) {
                if (isreg(c_)) {
                    curpool.insert(c_->getPort(ID::Q));
                    visited.insert(c_->getPort(ID::Q));
                } else {
                    curpool.insert(cellDownRegs[c_].begin(),cellDownRegs[c_].end());
                    visited.insert(cellDownRegs[c_].begin(),cellDownRegs[c_].end());
                }
            }
            log("cur pool\n");
            for (auto &s_: curpool) {
                log("%s\n", log_signal(s_));
                two_trace_tar_sigs[string(log_signal(s_)).substr(1)] = {.s=s_, .c=nullptr, .diff=0};
            }
            log("------\n");
        } else {
            curpool.insert(start_sig);
            visited.insert(start_sig);
            two_trace_tar_sigs[start] = {.s=start_sig, .c=nullptr, .diff=0};
        }


        // target_group = start_sig.S = nextpool_c + nextpool_m
        get_next_pools(nextpool_c, nextpool_m, curpool, visited, visited_m);

        while(!(nextpool_c.empty() && nextpool_m.empty())) {
            curpool = nextpool_c;
            curpool_m = nextpool_m;

            // ----- handle register ------ 
            for (auto &s_: curpool) {
                if (visited.find(s_) != visited.end()) {
                    log("[error] skippp %s\n", log_signal(s_));
                    continue;
                }

                string s_str = log_signal(s_);
                assert(s_.is_chunk() && s_str[0] == '\\');		// hdl name

                two_trace_tar_sigs[s_str.substr(1)] = {.s=s_, .c=nullptr, .diff=cyc_diff};
            }
            visited.insert(curpool.begin(), curpool.end());
            

            // ----- handle memory ------ 
            pool<RTLIL::IdString> memcell_pool;
            for (auto &mptr_: curpool_m) {
                //log("diff %d pipe_reg_memcell %s\n", cyc_diff, mptr_.str().c_str());
                if (visited_m.find(mptr_.cell_) != visited_m.end()) {
                    //log("skippp mptr cell %s %d\n", mptr_.str().c_str(), cyc_diff);
                    continue;
                }
                assert(mptr_.port_name.in(ID::WR_EN, ID::WR_ADDR, ID::WR_DATA));
                visited_m.insert(mptr_.cell_);
                auto mcell = mptr_.cell_;
			    string memid_ = mcell->parameters[ID::MEMID].decode_string();
                two_trace_tar_sigs[memid_.substr(1)] = {.s=SigSpec(), .c=mcell, .diff=cyc_diff};
            }
            // new_target_group = { reg’ / mem’ | (reg, reg’) / (reg, mem’) forall reg in 	
            //   target_group.S - visited } // downstream of register of target_group’s register
            get_next_pools(nextpool_c, nextpool_m, curpool, visited, visited_m);

            log("nextpool_m %d\n", nextpool_m.size());
            log("nextpool_c %d\n", nextpool_c.size());
            
            cyc_diff++;	
        }  // loop over all reachable S from IFR

        vector<pair<string, u_sig_cell>> tmp_vec;
        for (auto &p: two_trace_tar_sigs)
            tmp_vec.push_back(p);
        sort(tmp_vec.begin(), tmp_vec.end(), sort_u_sig_cell());
        //tmp_vec.sort(sort_u_sig_cell());
        for (auto& it: tmp_vec) {
            log("%-50s %d sig %d\n", it.first.c_str(), it.second.diff, it.second.c == nullptr );
        }
        log("-------\n");



    }
};


PRIVATE_NAMESPACE_END
#endif
