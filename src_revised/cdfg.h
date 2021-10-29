// From input design to full-design DFG
// * collect_sig_cell: map from sig/cell to cell/sig
#ifndef CDFG_H
#define CDFG_H
// * find_downstream_reg: maps between state elements 
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
#include "dataflow.h"
#include "util_verilogbackend.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN


// finer granularity of wire passing along with bitfield 
// #define VPR_04_TEST	
enum map_type { half_s, nos, full_s }; 
class Full_design_cdfg {

    // <>_half_s: A/B is constant, map(S) to Y
    // <>_nos: drop case "A/B is constant, map(S) to Y" ie for PCR[0]
    // <>_full_s: include all A/B/S->Y
    
    dict<RTLIL::SigSpec, pool<RTLIL::Cell*>> sigToNextCell_, sigToNextCell_nos, sigToNextCell_full_s; // sig to (comb ckt or mem or reg)
    dict<RTLIL::Cell*, pool<RTLIL::SigSpec>> cellToNextSig_, cellToNextSig_nos, cellToNextSig_full_s; // (comb ckt or mem or reg) to sig 
    dict<RTLIL::Cell*, pool<RTLIL::SigSpec>> invSigToNextCell_, invSigToNextCell_nos, invSigToNextCell_full_s; 
    dict<RTLIL::SigSpec, pool<RTLIL::Cell*>> invCellToNextSig_, invCellToNextSig_nos, invCellToNextSig_full_s;

    dict<RTLIL::SigSpec, pool<RTLIL::SigSpec>> connections_;    // wire key drives wire val

    pool<RTLIL::Cell*> mem_pool_;
    dict<RTLIL::SigSpec, RTLIL::SigSpec> ff_dq_, ff_qd_;
    dict<RTLIL::SigSpec, RTLIL::Cell*> ff_q_cell_;

    RTLIL::Module *module;

    // DFS to collect R - dataflow relation between state elements 
    //class RTLDataFlow RTLDataFlow_;
    RTLDataFlow *RTLDataFlow_default;
    RTLDataFlow *RTLDataFlow_nos;
    RTLDataFlow *RTLDataFlow_full_s;
    pool<SigSpec> visited;
    pool<SigSpec> visiting;
    std::vector<RTLIL::Cell*> paths_c;
    bool debug;

    
public: 
    SigMap sigmap;

    void set(RTLDataFlow *RTLDataFlow_default_, RTLDataFlow *RTLDataFlow_nos_, RTLDataFlow *RTLDataFlow_full_s_) {
        RTLDataFlow_default = RTLDataFlow_default_;
        RTLDataFlow_nos =  RTLDataFlow_nos_;           
        RTLDataFlow_full_s =  RTLDataFlow_full_s_;
    }

    string get_sig_verilog(RTLIL::SigSpec sig_wen) {
        std::ostringstream oss_wen;
        std::ostream &oss_wen_ = oss_wen;
        
        auto cell_pool_addr = invCellToNextSig_full_s[sig_wen];
        verilog_dump_setmap(invCellToNextSig_full_s);
        log("%d\n", cell_pool_addr.size());
        for (auto &c_ : cell_pool_addr) {
        	verilog_dump_cell(oss_wen_, "", c_);
        	log("c_: %s\n", RTLIL::id2cstr(c_->name));
        }

        string addr_auto_gen_rev = oss_wen.str();
        std::istringstream iss(addr_auto_gen_rev);
        string temp; 
        vector<string> lines;
        while(getline(iss, temp)) {
        	lines.push_back(temp);
        }
        reverse(lines.begin(), lines.end());
        log("dump verilog %s\n", log_signal(sig_wen));
        string ret = "";
        for (auto &s_: lines) {
        	log("%s\n", s_.c_str());
            ret += (s_ + "\n");
        }
        log("------------\n");	
        return ret;
    }

    const dict<RTLIL::Cell*, pool<RTLIL::SigSpec>> invSigToNextCell() const {
        return invSigToNextCell_full_s;
    }
    const dict<RTLIL::SigSpec, pool<RTLIL::Cell*>> &invCellToNextSig() const {
        return invCellToNextSig_full_s;
    }
    const dict<RTLIL::SigSpec, pool<RTLIL::Cell*>> &sigToNextCell() const {
        return sigToNextCell_full_s;
    }
    const dict<RTLIL::SigSpec, pool<RTLIL::SigSpec>> &connections() const {
        return connections_;   
    }
    const pool<RTLIL::Cell*> &mem_pool() const {
        return mem_pool_;
    }
    const dict<RTLIL::SigSpec, RTLIL::SigSpec> &ff_dq() const {
        return ff_dq_; 
    };
    const dict<RTLIL::SigSpec, RTLIL::Cell*> &ff_q_cell() const {
        return ff_q_cell_;
    }
    
    void add_ff(RTLIL::Cell* c) {
        assert(c->hasPort(ID::D) && c->hasPort(ID::Q));
        ff_dq_[sigmap(c->getPort(ID::D))] = sigmap(c->getPort(ID::Q));
        ff_qd_[sigmap(c->getPort(ID::Q))] = sigmap(c->getPort(ID::D));
        ff_q_cell_[sigmap(c->getPort(ID::Q))] = c;
    }


    void dfs_visit(RTLIL::SigSpec sig_in_, RTLIL::Cell* cell,  map_type select_ = half_s, RTLIL::SigSpec memportsig = SigSpec()) {
        // collect all sig_in -> cells that is reg/mem
        dict<RTLIL::SigSpec, pool<RTLIL::Cell*>> &sigToNextCell = select_ == half_s ? sigToNextCell_ : (select_ == full_s ? sigToNextCell_full_s : sigToNextCell_nos );
        dict<RTLIL::Cell*, pool<RTLIL::SigSpec>> &cellToNextSig = select_ == half_s ? cellToNextSig_ : (select_ == full_s ? cellToNextSig_full_s : cellToNextSig_nos );
        dict<RTLIL::Cell*, pool<RTLIL::SigSpec>> &invSigToNextCell = select_ == half_s ? invSigToNextCell_ : (select_ == full_s ? invSigToNextCell_full_s : invSigToNextCell_nos );
        dict<RTLIL::SigSpec, pool<RTLIL::Cell*>> &invCellToNextSig = select_ == half_s ? invCellToNextSig_ : (select_ == full_s ? invCellToNextSig_full_s : invCellToNextSig_nos );
        
        class RTLDataFlow &RTLDataFlow_ = select_ == half_s ? *RTLDataFlow_default : (select_ == full_s ? *RTLDataFlow_full_s : *RTLDataFlow_nos);
        assert(!(RTLDataFlow_full_s == nullptr) && select_ == full_s);
        
        SigMap sigmap_ = sigmap;
        auto sig_in = sigmap_(sig_in_);
        if (sig_in.is_fully_const()) return; 
        
        // cross edge  
        if (visited.find(sig_in) != visited.end()) {
            log("[warn] DFS wire cross edge: %s %s \n", log_signal(sig_in), cell->name.str().c_str());
            return;
        }

        // cycle 
        if (visiting.find(sig_in) != visiting.end()) {
            log("[warn] DFS wire cycle: %s %s \n", log_signal(sig_in), cell->name.str().c_str());
            return; 
        }

        visiting.insert(sig_in);

        
        pool<RTLIL::Cell*> cells_down = sigToNextCell[sigmap_(sig_in)];
        for (auto &driven: connections_[sig_in]) {
            auto pool_ = sigToNextCell[(sigmap_(driven))];
            cells_down.insert(pool_.begin(), pool_.end());
        } 
        
        if (cells_down.size() == 0) {
            log("[warn] DFS sig no children cell??: %s %s \n", log_signal(sig_in), cell->name.str().c_str());
            return; 
        }

        for (auto &c : cells_down) {
            if (string(log_signal(sig_in)).find("imem_reg") != std::string::npos) {
                log("%s\n", c->name.str().c_str());
            }
            // visit cell further
            if (find(paths_c.begin(), paths_c.end(), c) != paths_c.end()) {
                log("[warn] DFS cell cycle: %s %s \n", log_signal(sig_in), c->name.str().c_str());
                continue;
            }
            
            if (isreg(c)) {
                auto sig_q = sigmap_(c->getPort(ID::Q));
                if (ismem(cell)) {
                    assert(!memportsig.empty());
                    RTLDataFlow_.add_edge(c, cell->name, memportsig, memtocell, ID::RD_DATA, general, c); // assume outport of memory is usually RD_DATA 
                } else { 
                    RTLDataFlow_.add_edge(cell, sig_q, celltoreg, general);  // cell -> sig_q
                }
            } else if (ismem(c)) {
                // cell to mems
                bool port=false;
                for (auto &prop : c->connections_) {	
                    //if (string(log_signal(sig_in)).find("imem_reg") != std::string::npos) {
                    //    log("port %s\n", prop.first.str().c_str());
                    //}
                    
                    SigSpec sig_wr_addr = sigmap_(prop.second);
                    auto chks = sig_wr_addr.chunks();
                    for (auto &chunk: chks) {
                        if (chunk.wire != NULL && sigmap_(chunk.wire) == sig_in) {
                            // mem/cell/reg to mem 
                            if (prop.first.str().find("WR") != std::string::npos) {
                                log("[YHDB]\n");
                                RTLDataFlow_.add_edge(cell, c->name, sigmap_(sig_in), celltomem, prop.first, general, c); 
                            } else {
                                    
                                log("RD %s\n", c->name.str().c_str());
                                int nread_ports = c->parameters[ID::RD_PORTS].as_int();
                                int abits = c->parameters[ID::ABITS].as_int();
                                bool use_rd_clk, rd_clk_posedge;
                                int width = c->parameters[ID::WIDTH].as_int();
                                pool<RTLIL::Cell*> cpool;
                                for (int i = 0; i < nread_ports; i++)
                                {
                                    auto sig_rd_addr = c->getPort(ID::RD_ADDR).extract(i*abits, abits);
                                    if (sig_rd_addr.is_chunk()) {
                                        auto chk = sig_rd_addr.as_chunk();
                                        if (chk.wire != NULL)
                                            sig_rd_addr = sigmap_(chk.wire);
                                    } else {
                                        assert(0);
                                    }
                                    if (sigmap_(sig_rd_addr) != sigmap_(sig_in)) continue;
                                    log("memc name :%s\n", c->name.str().c_str());
                                    auto sig_rd_data = c->getPort(ID::RD_DATA).extract(i*width, width);
                                    cpool.insert(sigToNextCell[sig_rd_data].begin(), sigToNextCell[sig_rd_data].end());
                                }

                                // -------------
                                log("TESTING %s\n", c->name.str().c_str());
                                if (RTLDataFlow_.check_exist(c, cell)) {
                                    continue; 
                                }
                                paths_c.push_back(c);
                                for (auto &tmpc: cpool) {
                                    for (auto &sig_ : cellToNextSig[tmpc]) {
                                        dfs_visit(sigmap_(sig_), cell, select_, memportsig);	
                                    }
                                }
                                paths_c.pop_back();
                                            
                            }
                            if (string(log_signal(sig_in)).find("imem_reg") != std::string::npos) {
                                log("add%s\n", prop.first.str().c_str());
                            }
                            port = true;
                        }
                    }
                }
                if (!port)
                    log("[warn] DFS celltomem no port sig_in %s c %s", log_signal(sig_in), c->name.str().c_str());
            } else {
                if (RTLDataFlow_.check_exist(c, cell)) {
                    if (debug) {
                        log("\nddebug path\n");
                        for (auto &tt: paths_c) { log("%s,", tt->name.str().c_str());}
                        log("\n");
                    }
                    continue; 
                }

                auto sigs_set = cellToNextSig.find(c);
                if (sigs_set == cellToNextSig.end()){
                    log("[warn] DFS no cell %s has no next sig \n", c->name.str().c_str());
                    continue;
                } 
                paths_c.push_back(c);
                for (auto &sig_ : sigs_set->second) {
                    dfs_visit(sigmap_(sig_), cell, select_, memportsig);	
                }
                paths_c.pop_back();
            }
            
        }
        visiting.erase(sig_in);
        visited.insert(sig_in);
    }
    // TODO 0: cache visited
    // TODO 1: visited as vector push and pop
    // TODO 2: modularized(?), called for each module in the design, currently limited to flattened single top module
    void find_downstream_reg(map_type select_, pool<RTLIL::SigSpec> output_sigs_outcore) {
        log("[Full_design_cdfgs] find_downstream_reg select %d\n", select_);
        
        dict<RTLIL::Cell*, pool<RTLIL::SigSpec>> &cellToNextSig = select_ == half_s ? cellToNextSig_ : (select_ == full_s ? cellToNextSig_full_s : cellToNextSig_nos );
        dict<RTLIL::Cell*, pool<RTLIL::SigSpec>> &invSigToNextCell = select_ == half_s ? invSigToNextCell_ : (select_ == full_s ? invSigToNextCell_full_s : invSigToNextCell_nos );
        class RTLDataFlow &RTLDataFlow_ = select_ == half_s ? *RTLDataFlow_default : (select_ == full_s ? *RTLDataFlow_full_s : *RTLDataFlow_nos);

        RTLDataFlow_.set(module);


        for (auto &cell_iter : module->cells_) {
            auto cell = cell_iter.second;

            visited.clear();
            visiting.clear();
            paths_c.clear();

            bool effect_reg = false;
            RTLIL::SigSpec memportsig;
            bool debug = false;
            
            if (cellToNextSig.find(cell) != cellToNextSig.end()) {
                auto upward_sigs_set = invSigToNextCell[cell];
                bool dump = false;
                for (auto &Y : cellToNextSig[cell]){
                    bool found = false;
                    for (auto &driven: connections_[Y]) {
                        if(output_sigs_outcore.find(driven) != output_sigs_outcore.end())
                            found = true;
                    }
                    if (output_sigs_outcore.find(sigmap(Y)) != output_sigs_outcore.end() || found) {
                        log("outputsig %s\n", log_signal(Y));
                    }
                        
                    if (upward_sigs_set.find(Y) != upward_sigs_set.end()) {
                        log("skip downvisit: %s\n", log_signal(Y));
                        continue;
                    }
                    log("** downvisit cell at %s \n", log_signal(Y));
                    memportsig = RTLIL::SigSpec();
                    if (cell->type.in(ID($mem), ID($mem_v2))) {
                        log("vstmem %s\n", cell->parameters[ID::MEMID].decode_string().c_str());
                        memportsig = Y;
                    } else if (ismem(cell)) {
                        assert(0); // yet support memrd, meminit, memwr
                    }	
                    dfs_visit(Y, cell, select_, memportsig);
                }
                RTLDataFlow_.addVisited(cell);
#ifdef TEST 
                if (dump || cell->name.str().find("dmem_haddr") != std::string::npos){
                    dump_sigpool(cellToNextSig[cell], "core_hwrite celltonext");
                    for (auto &s: cellToNextSig[cell]) {
                        dump_cellpool(sigToNextCell[s], "sigtonextcell");
                    }
                    dump_sigpool(RTLDataFlow_.cellDownRegs[cell], "core_hwrite debug");
                }
#endif 
            }
            
        }   // DFS on every cells
        
#ifdef TEST
        auto s = get_sig_by_str("core_gen_block[0].vscale.pipeline.alu_out", module_, sigmap_);
        if (!s.empty()) {
            auto cc = sigToNextCell[s];
            pool<RTLIL::SigSpec> pl;
            for (auto &cell: cc)
                pl.insert(RTLDataFlow_.cellDownRegs[cell].begin(), RTLDataFlow_.cellDownRegs[cell].end());
            dump_sigpool(pl, "alu out downstrea");
        }
#endif 
        // design_reg_reg
        log("------reg_reg------\n");
        for (auto &pair: RTLDataFlow_.cellDownRegs) {
            auto c_ = pair.first;
            assert(!ismem(c_));
            if (isreg(c_)) {
                auto qport = sigmap(c_->getPort(ID::Q));
                for (auto &r_: pair.second) {
                    RTLDataFlow_.design_reg_reg[qport].insert(sigmap(r_));
                    log("%s,%s\n",log_signal(qport), log_signal(sigmap(r_)));
                }
            }
        }
        log("------reg_reg end------\n");

    }

    void find_downstream_handle_mem(map_type select_ = half_s) {
        class RTLDataFlow &RTLDataFlow_ = select_ == half_s ? *RTLDataFlow_default : (select_ == full_s ? *RTLDataFlow_full_s : *RTLDataFlow_nos);

        dict<RTLIL::SigSpec, pool<RTLIL::Cell*>> &sigToNextCell = select_ == half_s ? sigToNextCell_ : (select_ == full_s ? sigToNextCell_full_s : sigToNextCell_nos );

        SigMap sigmap_ = sigmap;

	    // reg_to_upstream_rdport, rd_data_to_wports

        for (auto &cell: mem_pool_) {
            int nread_ports = cell->parameters[ID::RD_PORTS].as_int();
            int abits = cell->parameters[ID::ABITS].as_int();
            bool use_rd_clk, rd_clk_posedge;
            int width = cell->parameters[ID::WIDTH].as_int();
            for (int i = 0; i < nread_ports; i++)
            {
                auto sig_rd_addr = cell->getPort(ID::RD_ADDR).extract(i*abits, abits);
                if (sig_rd_addr.is_chunk()) {
                    auto chk = sig_rd_addr.as_chunk();
                    if (chk.wire != NULL)
                        sig_rd_addr = sigmap_(chk.wire);
                } else {
                    assert(0);
                }
                use_rd_clk = cell->parameters[ID::RD_CLK_ENABLE].extract(i).as_bool();
                if (use_rd_clk) {
                    log("yet support rd clk");
                    assert(0); 
                    // back-and-store.txt
                } else {
                    log("memcell name :%s\n", cell->name.str().c_str());
                    auto sig_rd_data = cell->getPort(ID::RD_DATA).extract(i*width, width);
                    auto cpool = sigToNextCell[sig_rd_data];
                    pool<MemPort_> down_mems; // WR_x, RD_ADDR
                    pool<SigSpec> down_regs;
                    // down_mems = RTLDataFlow_.cellDownMems_[cell];
                    for (auto &tcell: cpool) {
                        if (ismem(tcell)) {
                            assert(0);
                            // continue;
                            // cell to mem  case should be inncluded in cellDownMems_
                        } else if (isreg(tcell)) {
                            down_regs.insert(tcell->getPort(ID::Q)); 
                            continue; 
                        } else {
                            auto regs = RTLDataFlow_.cellDownRegs[tcell];
                            down_regs.insert(regs.begin(), regs.end());
                        }
                        
                        auto mports = RTLDataFlow_.cellDownMems_[tcell];
                        down_mems.insert(mports.begin(), mports.end());
                    }
                    for (auto &r_: down_regs) {
                        RTLDataFlow_.reg_to_upstream_rdport[r_].insert(make_pair(cell, sig_rd_addr));
                        // 1026 test
                    }

                    for (auto &m_: down_mems) {
                        // TBD
                        log("down_mems %d %s %s\n", m_.mem_cell.str() == cell->name.str(), m_.str().c_str(), log_signal(sig_rd_addr));
                        if (m_.mem_cell.str() == cell->name.str()) 
                            continue;
                        if (m_.port_name == ID::WR_DATA || m_.port_name == ID::WR_EN || m_.port_name == ID::WR_ADDR)
                            // rd_data_to_wports[m_.sig_name] = make_pair(cell, sig_rd_addr);
                            RTLDataFlow_.rd_data_to_wports[m_.cell_] = make_pair(cell, sig_rd_addr);		// since only single W port
                        else 
                            log("[reg_to_upstream_rdport] not handling other mem cases\n");
                    }
                }

                
            }
        }
        log("reg_to_upstream_rdport %d\n", RTLDataFlow_.reg_to_upstream_rdport.size());
        for (auto &itm: RTLDataFlow_.reg_to_upstream_rdport) {
            //	r, cell, sig_rd_addr
            for (auto &rdport: itm.second) 
                log("reg_to_upstream_rdport: %s ,%s %s\n", log_signal(itm.first), rdport.first->name.str().c_str(), log_signal(rdport.second));
        }

        log("rd_data_to_wports: %d\n", RTLDataFlow_.rd_data_to_wports.size());
        for (auto &itm: RTLDataFlow_.rd_data_to_wports) {
            log("%s: (cell=%s, %s)\n", (itm.first->name.str().c_str()), itm.second.first->name.c_str(), log_signal(itm.second.second));
        }		

        log("celldownmem\n");
        for (auto &pair: RTLDataFlow_.cellDownMems_) {
            auto c_ = pair.first;
            if (isreg(c_) || ismem(c_)) {
                if (isreg(c_)) {
                    auto qport = sigmap_(c_->getPort(ID::Q));
                    
                    for (auto &rpt: pair.second) {
                        if (rpt.port_name == ID::WR_EN || rpt.port_name == ID::WR_DATA || rpt.port_name == ID::WR_ADDR) {
                            RTLDataFlow_.design_reg_mem[qport].insert(rpt);
                            log("reg cname %s -> mem: \n", log_signal(qport));
                        }
                    }

                } else { 
                    // rd data to write port ! 
                    //log("mem %s -> mem: \n", id(c_->parameters[ID::MEMID].decode_string()).c_str());
                }
                for (auto &rpt: pair.second) 
                    log("%s\n", rpt.str().c_str());
                
            }
        }
        
        log("design_reg_reg %d\n", RTLDataFlow_.design_reg_reg.size());
        log("design_reg_mem %d\n", RTLDataFlow_.design_reg_mem.size());
        for (auto &pr: RTLDataFlow_.design_reg_mem) {
            log("signal %s\n", log_signal(pr.first));
            for (auto &pt: pr.second) {
                log("%s\n", pt.str().c_str());
            }
        }


        log(" [find_downstream_handle_mem] %d \n", select_);
        RTLDataFlow_.stat();
    }



    void collect_sig_cell(RTLIL::Module *module_, int print, map_type select_ = half_s) {
        dict<RTLIL::SigSpec, pool<RTLIL::Cell*>> &sigToNextCell = select_ == half_s ? sigToNextCell_ : (select_ == full_s ? sigToNextCell_full_s : sigToNextCell_nos );
        dict<RTLIL::Cell*, pool<RTLIL::SigSpec>> &cellToNextSig = select_ == half_s ? cellToNextSig_ : (select_ == full_s ? cellToNextSig_full_s : cellToNextSig_nos );
        dict<RTLIL::Cell*, pool<RTLIL::SigSpec>> &invSigToNextCell = select_ == half_s ? invSigToNextCell_ : (select_ == full_s ? invSigToNextCell_full_s : invSigToNextCell_nos );
        dict<RTLIL::SigSpec, pool<RTLIL::Cell*>> &invCellToNextSig = select_ == half_s ? invCellToNextSig_ : (select_ == full_s ? invCellToNextSig_full_s : invCellToNextSig_nos );
        
        sigToNextCell.clear();
        cellToNextSig.clear();
        invSigToNextCell.clear();
        invCellToNextSig.clear();
        connections_.clear();

        module = module_;
        sigmap.set(module);

        // sig: wire, reg name
        // cell: comb ckt, mem, ff
        log("[Full_design_cdfgs] collect_sig_cell select %d \n", select_);
        
        pool<RTLIL::SigSpec> ignore_sigs;   // ignore user-defined cell (ie other non-flattened cores) input/output 
        for (auto &cellit : module->cells_) {
            auto cell = cellit.second;
            if (cell->type.str().substr(0,1) != "$") {
                // already flatten should be no user-defined module as cell
                for (auto &conn: cell->connections()) {
                    auto port_name = conn.first;
                    auto sig_ = conn.second;
                    if (cell->input(port_name) || cell->output(port_name))
                        ignore_sigs.insert(sigmap(sig_));
                }
            }
        }
        
        for (auto &conn: module->connections()) {
            // conn.first driven by conn.second 
            auto driven = conn.first;
            auto driver = conn.second;
            vector<RTLIL::Wire*> driver_w, driven_w;
            getWires(driven, driven_w);
            getWires(driver, driver_w);
            for (auto &a: driver_w) {
                for (auto &b: driven_w) {
                    // if (string(log_signal(sigmap(a))).find("dmem_hwrite") != std::string::npos) {
                    // 	log("%zu a: %s -> b: %s\n", sigmap(a).get_hash() , log_signal(sigmap(a)), log_signal((b)));
                    // }
                    connections_[sigmap(a)].insert((b));
                }	
            }
        }

        for (auto &cellit : module->cells_) {
            auto cell = cellit.second;
            cellToNextSig[cell] = pool<RTLIL::SigSpec>();
            invSigToNextCell[cell] = pool<RTLIL::SigSpec>();

            bool ismem_ = ismem(cell);
            bool isreg_ = isreg(cell);

            if (cell->type.str().substr(0,1) != "$") {
                log("custom cell %s\n", cell->name.str().c_str());	// other cores that is not flatten
                continue;
            } 
            if (ismem_) {
                mem_pool_.insert(cell);
            } else if (isreg_) {
                add_ff(cell); 
            }
            for (auto &conn : cell->connections()) {
                auto port_name = conn.first;
                auto sig_ = conn.second;
                
                bool no_chunk = true;
#ifndef VPR_04_TEST
                for (auto &chunk: sig_.chunks()) {
                    if (chunk.wire == NULL) {
                        // handl all A, B are in same stage but default fix value, then use S
                        bool ctrl = cell->type.in(ID($mux), ID($pmux), ID($_MUX_)); 
                        if (ctrl && cell->input(port_name) && select_ == half_s) {
                            auto tmps = cell->getPort(ID::S);
                            for (auto &ck: tmps.chunks()) {
                                if (ck.wire == NULL) continue;
                                auto wire_ = sigmap(ck.wire);
                                sigToNextCell[wire_].insert(cell);
                                invSigToNextCell[cell].insert(wire_);
                            }
                        }
                        continue;
                    }
                    no_chunk = false;
                    auto wire_ = sigmap(chunk.wire);
#else 
                auto wire_ = sigmap(sig_); //sigmap(chunk); 
                // finer granularity with bitfield not just the wire
#endif

                    if (ignore_sigs.find(wire_) != ignore_sigs.end()) continue;
                    if (cell->input(port_name)) {
                        bool ctrl = cell->type.in(ID($mux), ID($pmux), ID($_MUX_)); 
                        bool act = false;
                        if (select_ == nos || select_ == half_s) 
                            act = ((ctrl && port_name != ID::S) || !ctrl);
                        else
                            act = (select_ == full_s);
                        if (act)  {
                            sigToNextCell[wire_].insert(cell);
                            invSigToNextCell[cell].insert(wire_);
                        }
                    } else if (cell->output(port_name)) {
                        cellToNextSig[cell].insert(wire_);
                        invCellToNextSig[wire_].insert(cell);
                    }
#ifndef VPR_04_TEST                
                }

                if (no_chunk && !sig_.is_fully_const()) {
                    log("[warn] no wire : %s %s \n", log_signal(sig_), cell->name.str().c_str());
                }
#endif             
            }
            
        }
        if (print) {
            log("------ full DFG map-----\n");
            FILE *fptr = fopen("./build/dump/fulldfg.csv", "w");
            fprintf(fptr, "dir,u,v\n");
            FILE *fptr_cell = fopen("./build/dump/cellmap.csv", "w");
            fprintf(fptr_cell, "mem,reg,hash\n");
    #ifdef VPR_04_TEST		
            FILE *fptr_s = fopen("./build/dump/ctrl.csv", "w");
    #endif
            char buff[200];
            for (auto &cellit : module->cells_) {
                auto cell = cellit.second;
                bool ismem_ = ismem(cell);
                bool isreg_ = isreg(cell);
                fprintf(fptr_cell, "%d,%d,%s,%u\n", ismem_, isreg_, cell->name.str().c_str(),cell->hash());
    #ifndef VPR_04_TEST
                for (auto &s: invSigToNextCell[cell]) {
                    fprintf(fptr, "u,%s,%u\n",log_signal(s), cell->hash());
                }
                
                for (auto &s: cellToNextSig[cell]) {
                    fprintf(fptr, "d,%u,%s\n", cell->hash(), log_signal(s));
                }
    #else
                for (auto &conn : cell->connections()) {
                    auto port_name = conn.first;
                    auto sig_ = conn.second;
                    
                    if (sig_.is_fully_const()) continue;
                    bool no_chunk = true;
                    for (auto &chunk: sig_.chunks()) {
                        if (chunk.wire == NULL) continue;
                        no_chunk = false;
                        auto wire_ = sigmap(chunk.wire);
                        if (ignore_sigs.find(wire_) != ignore_sigs.end()) continue;
                        if (cell->input(port_name)) {
                            bool ctrl = cell->type.in(ID($mux), ID($pmux), ID($_MUX_)); 
                            if ( (
                                ctrl && port_name != ID::S
                            ) || !ctrl )  {
                                fprintf(fptr, "u,%s,%u\n",dump_sig_chunk(chunk, sigmap).c_str(), cell->hash());
                            } else if (ctrl && port_name == ID::S) {
                                fprintf(fptr_s, "u,%s,%u\n",dump_sig_chunk(chunk, sigmap).c_str(), cell->hash());
                            }
                        } else if (cell->output(port_name)) {
                            fprintf(fptr, "d,%u,%s\n", cell->hash(), dump_sig_chunk(chunk, sigmap).c_str());
                        }
                    }
                }
    #endif 
            }
            fclose(fptr);
            fclose(fptr_cell);
        }


        log("sigToNextCell_: %d\n", sigToNextCell_.size());
        log("cellToNextSig_: %d\n", cellToNextSig_.size());
        log("invSigToNextCell_: %d\n", invSigToNextCell_.size());
        log("invCellToNextSig_: %d\n", invCellToNextSig_.size());

        log("sigToNextCell_nos: %d\n", sigToNextCell_nos.size());
        log("cellToNextSig_nos: %d\n", cellToNextSig_nos.size());
        log("invSigToNextCell_nos: %d\n", invSigToNextCell_nos.size());
        log("invCellToNextSig_nos: %d\n", invCellToNextSig_nos.size());
        
        log("sigToNextCell_full_s: %d\n", sigToNextCell_full_s.size());
        log("cellToNextSig_full_s: %d\n", cellToNextSig_full_s.size());
        log("invSigToNextCell_full_s: %d\n", invSigToNextCell_full_s.size());
        log("invCellToNextSig_full_s: %d\n", invCellToNextSig_full_s.size());

        log("ff_dq %d\n", ff_dq_.size());

        log(" ----- collect_sig_cell DONE ---\n");
        log(" ---- mem_pool_: check memory cells in the design [per core only repeat once]: ------- \n");
        for (auto &c: mem_pool_) {
            log("%s\n", c->name.str().c_str());
        }
        log("\n");
    }


    pair<string, string> get_verilog(RTLIL::SigSpec s) {
        // TODO: REMOVE
        if (s.is_chunk()) {
            auto chk = s.as_chunk();
            if (chk.wire != nullptr)
                s = sigmap(chk.wire);
        }
        string ss_ = log_signal(s);
        string verilog_ = "";
        if (ss_[0] == '\\')
            ss_ = ss_.substr(1);
        else {
            verilog_ = get_sig_verilog(s);
            ss_ = log_signal(s);
            ss_ = "\\" + ss_; 
        }
        return make_pair(ss_, verilog_);
    };

};



PRIVATE_NAMESPACE_END
#endif 
