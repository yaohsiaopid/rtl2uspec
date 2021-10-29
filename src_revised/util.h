#ifndef UTIL_H
#define UTIL_H
#include "kernel/yosys.h"
#include "kernel/sigtools.h"
#include "kernel/celltypes.h"
#include "backends/ilang/ilang_backend.h"
#include <string>
#include <map>
#include <set>
#include <functional>
#include <queue>
#include <cassert>
#include <algorithm>
#include <sstream>

#include "design.h"
#include "container.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

bool isreg(const RTLIL::Cell* c) {
	// warn about latches, will treat as std cell
	if (c->type.in(
		ID($adff), ID($adffe), ID($dffsr), ID($dffsre), ID($dlatch), ID($adlatch), ID($dlatchsr), ID($sr)))
		log("[warn] latches.....\n\n");
	return c->type.in(
		ID($dff), ID($dffe), ID($sdff), ID($sdffe), ID($sdffce));
} 
bool ismem(const RTLIL::Cell* c) {
	return c->type.in(ID($mem), ID($memrd), ID($memwr), ID($meminit), ID($mem_v2));
}

void cat(const RTLIL::SigSpec* sig, RTLIL::Cell* cell, const string &s, const string &prefix="") {
	string s2, s3;
	if (cell != NULL)
		s2 = RTLIL::id2cstr(cell->name);
	if (sig != NULL)
		s3 = log_signal(*sig);
	log("%s[warn] %s; cell: %s; wire: %s\n",prefix.c_str(), s.c_str(), s2.c_str(), s3.c_str());
}




void getWires(const RTLIL::SigSpec s, vector<RTLIL::Wire*> &ret) {
	ret.clear();
	for (auto &chunk: s.chunks()) {
		if (chunk.wire != NULL) ret.push_back(chunk.wire);
	}
} 



struct RTL2USPEC_STAT: public Pass {
	RTL2USPEC_STAT(): Pass("ff_stat") {}
	void execute(vector<string> args, RTLIL::Design *design) override
	{
		log("=============[PASS] ff_stat =============\n");
		pool<RTLIL::Cell*> dffs;
		auto top_mod = design->top_module();
        for (auto &item: top_mod->cells_) {
			if (isreg(item.second)) {
				dffs.insert(item.second);
                log("%s\n", log_signal(item.second->getPort(ID::Q)));
			}
		}	
		int bitcnt = 0;
		for (auto &cc: dffs) {
			int width = cc->getParam(ID::WIDTH).as_int();
			bitcnt += width;
		}
		log("total # of flipflop: %d\n", bitcnt);
		log("total # of register: %d\n", dffs.size());
		log("============================================\n");
	}
	
} rtl2uspec_stat;

void dump_sigpool(pool<RTLIL::SigSpec> p, const string &ss) {
	log("dump sigpool for %s {\n", ss.c_str());
	for (auto &s : p) {
		log("-		%s,\n", log_signal(s));
	}
	log(" cnt: %zu }\n", p.size());
};

void dump_cellpool(pool<RTLIL::Cell*> p, const string &ss) {
	log("\n\tdump cellpool for %s {\n", ss.c_str());
	for (auto &s : p) {
		log("		%s,\n", s->name.c_str());
	}
	log(" cnt: %zu }\n", p.size());
};

RTLIL::SigSpec get_sig_by_str(const string &s, RTLIL::Module* module_, SigMap sigmap_) {
	if (s.size() < 2) return RTLIL::SigSpec();
	RTLIL::Wire* wire_;
	wire_ = module_->wires_[
		RTLIL::IdString(s[0] == '\\' ? s : "\\" + s)];
	if (wire_ != nullptr) {
		RTLIL::SigSpec s_ =  sigmap_(wire_);
		return s_;
	}
	return RTLIL::SigSpec();
};


struct s_map {
// replace map
	int c;
	string s;
	string match;
	s_map(const string match, int c):
		match(match), s(""), c(c) {}
	s_map(const string match, string s):
		match(match), s(s), c(-1000) {}
};

void substr_replace(string& str, const string& from, const string &to) {
	if(from.empty())
        return;
    size_t start_pos = 0;
    while((start_pos = str.find(from, start_pos)) != string::npos) {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length(); 
    }
}

void substr_replace(string& s, const string& from, int num) {
	substr_replace(s, from, std::to_string(num));
}
string replace_template(const string prop, vector<s_map> rep_list) {
	string ret = prop;
	for (auto &it: rep_list) {
		// if (it.c < 0 && it.s.size() == 0) assert(0);
		if (it.c < 0)
			substr_replace(ret, it.match, it.s);
		else
			substr_replace(ret, it.match, it.c);
	}
	return ret; 
}

#define X_SCALE 15
#define Y_SCALE 3.0
class MyGraph {
private:
	int depth = 0; 
	int sgraph = 0;
	dict<string, string> node_sty;
	dict<string, int> column_node;
	dict<pair<string, string>, string> edge_sty;
	dict<string, pool<string>> edges; // edges.u = {v | (u, v) in E} down edge 
	dict<string, pool<string>> parent_edges; // edges.v = {u | (u, v) in E} up edge 
	dict<string, RTLIL::Cell*> node_map_mem_;
	dict<string, RTLIL::SigSpec> node_map;
	std::vector<pool < pair <string,string>>> subgraphs;
	vector<string> subgraph_label;
	vector<pool<string>> seq_uhb; // updated node at idx = depth 
	vector<My_Disjoint_Set<string>> seq_uhb_sets;

	void addEdge_(const string &u, const string &v, const string &u_sty, const string &v_sty, const string &e_sty, int overwrite=1) {
		auto u_ = trans(u);
		auto v_ = trans(v);
		if (v_ == u_) return; 
		auto e_ = make_pair(u_, v_);
		if (!overwrite && edge_sty.find(e_) != edge_sty.end()) return;
		
		
		node_sty[u_] = u_sty; // node_sty[u_] + "," + u_sty;
		node_sty[v_] = v_sty; // node_sty[v_] + "," + v_sty;
		edge_sty[e_] = e_sty;
		
		edges[u_].insert(v_);
		parent_edges[v_].insert(u_);

		if (sgraph) {
			subgraphs[sgraph-1].insert(e_);
		}
		// outfile << "\"" <<  u_ << "\" [" << u_sty << "]; \n"; 
		// outfile << "\"" <<  v_ << "\" [" << v_sty << "]; \n"; 
		// outfile << "\"" << u_ << "\"->\"" << v_ << "\" [" << e_sty <<  "];\n";
	}
	struct node_info {
		int dep;
		bool write;
		inline bool operator==(const node_info& other) const {
			return (dep == other.dep && write == other.write);
		}
		inline unsigned int hash() const {
			return mkhash(hash_ops<int>::hash(dep), hash_ops<int>::hash(write ? 1 : 0));
		}
	};
	dict<string, pool<node_info>> loc_per_reg; // loc in dataflow depths write(T) / read(F)
	
public:
	const dict<string, RTLIL::Cell*> node_map_mem() const {
        return node_map_mem_; 
    }
	pool<string> upstream(const string &node_, const string &percore="") {
		pool<string> tmp;
		for (auto &e: edges) {
			if (e.second.find(node_) != e.second.end() && 
			(percore.size() == 0 || (percore.size() != 0 && e.first.find(percore) == std::string::npos)))
				tmp.insert(e.first);
		}
		return tmp;
	}
	inline pool<string> downstream(const string &node_) {
		return edges[node_];
	}
	inline int uhb_sz() { return seq_uhb.size(); }
	inline pool<string> uhb_item(int i) {
		if (i < seq_uhb.size()) 
			return seq_uhb[i]; 
		else 
			return pool<string>(); 
	}
	void dsj_union(int idx, string loc_a, string loc_b) {
		// operates on seq_uhb_sets[idx]
		seq_uhb_sets[idx].ds_union(loc_b, loc_a);
	}
	bool ds_find_check(int idx, string loc_a, string loc_b) {
		return (seq_uhb_sets[idx].ds_find(loc_a) == seq_uhb_sets[idx].ds_find(loc_b));
	}
	pool<string> dsj_dump(int idx) {
		// log("// dsj_dump %d\n", idx);
		return seq_uhb_sets[idx].dump_stat();
		// log("// dsj_dump %d END\n", idx);
	}
	MyGraph() {
	}
	
	void subgraph(string s) {
		sgraph++;
		subgraph_label.push_back(s);
		pool<pair<string,string>> npool;
		subgraphs.push_back(npool);
		// string name = "cluster"+std::to_string(j) + "_" + std::to_string(i);
		// outfile << "subgraph " << name << "{ \n";
	}
	void samerank(pair<RTLIL::SigSpec, RTLIL::Cell*> &pr, RTLIL::SigSpec &r2) {
		if (pr.second == nullptr) {
			samerank(pr.first, r2);
		} else {
			string s1_ = pr.second->name.str().substr(1);
			string s2_ = string(log_signal(r2)).substr(1);
			samerank(s1_, s2_);
		}
	}
	void samerank(pair<RTLIL::SigSpec, RTLIL::Cell*> &pr, RTLIL::Cell* &r2) {
		if (pr.second == nullptr) {
			samerank(pr.first, r2);
		} else {
			string s1_ = pr.second->name.str().substr(1);
			string s2_ = r2->name.str().substr(1);
			samerank(s1_, s2_);
		}
	}
	void samerank(RTLIL::SigSpec &r1, RTLIL::Cell* &r2) {
		string s1_ = string(log_signal(r1)).substr(1);
		string s2_ = r2->name.str().substr(1);
		samerank(s1_, s2_);
		//log("s1_: %s s2 %s", s1_.c_str(), s2_.c_str());
	}
	void samerank(RTLIL::SigSpec &r1, RTLIL::SigSpec &r2) {
		string s1_ = string(log_signal(r1)).substr(1);
		string s2_ = string(log_signal(r2)).substr(1);
		samerank(s1_, s2_);
	}
	void samerank(const string &s1, const string &s2) {
		bool s1_ = !(column_node.find(s1) == column_node.end());
		bool s2_ = !(column_node.find(s2) == column_node.end());
		if (s1_ && s2_) return;
		if (s1_) {
			column_node[s2] = column_node[s1];
		} else if (s2_) {
			column_node[s1] = column_node[s2];
		} else {
			//log("samr rank %s %s\n", s1.c_str(), s2.c_str());
			column_node[s1] = depth;
			column_node[s2] = depth;
			depth++;
		}
	}
	string trans(const string &s) {
		return s;
	}

	void addEdge(RTLIL::SigSpec &r1, RTLIL::SigSpec &r2, const string &u_sty, const string &v_sty, const string &e_sty, int overwrite=1) {
		string s1 = string(log_signal(r1)).substr(1);
		string s2 = string(log_signal(r2)).substr(1);
		node_map[s1] = r1;
		node_map[s2] = r2;
		addEdge_(s1, s2, u_sty, v_sty, e_sty, overwrite);
	}
	void addEdge(RTLIL::Cell *r1, RTLIL::SigSpec &r2, const string &u_sty, const string &v_sty, const string &e_sty, int overwrite=1) {
		string s1 = r1->name.str().substr(1);
		string s2 = string(log_signal(r2)).substr(1);
		node_map_mem_[s1] = r1;
		node_map[s2] = r2;
		addEdge_(s1, s2, u_sty, v_sty, e_sty, overwrite);
	}
	void addEdge(RTLIL::SigSpec &r1, RTLIL::Cell *r2, const string &u_sty, const string &v_sty, const string &e_sty, int overwrite=1) {
		string s1 = string(log_signal(r1)).substr(1);
		string s2 = r2->name.str().substr(1);
		node_map[s1] = r1;
		node_map_mem_[s2] = r2;
		addEdge_(s1, s2, u_sty, v_sty, e_sty, overwrite);
	}
	void addEdge(RTLIL::Cell *r1, RTLIL::Cell *r2, const string &u_sty, const string &v_sty, const string &e_sty, int overwrite=1) {
		string s1 = r1->name.str().substr(1);
		string s2 = r2->name.str().substr(1);
		node_map_mem_[s1] = r1;
		node_map_mem_[s2] = r2;
		addEdge_(s1, s2, u_sty, v_sty, e_sty, overwrite);
	}

	vector<string> color = { "#984ea3","#ff7f00","#ffff33","#e41a1c","#377eb8","#4daf4a","#a65628","#f781bf","#999999" };
	vector<pool<string>> uhb_stages; 
	void uhb_anchor(string const &inst_) {
		// assume all nodes are uhb nodes
		// collect seq_uhb 
		pool<string> visited; 
		pool<string> cur_pool, next_pool;
		cur_pool = edges[inst_]; 
		while (!cur_pool.empty()) {
			for (auto &c_: cur_pool) {
				for (auto &r_: edges[c_]) {
					if (visited.find(r_) != visited.end()) continue; 
					visited.insert(r_);
					next_pool.insert(r_);
				}
				visited.insert(c_);
			}
			seq_uhb.push_back(cur_pool);
			cur_pool = next_pool;
			next_pool.clear(); 
		}
	}
	void deps_anchor(string const &inst_) {
		pool<string> visited; 
		int stidx = 1;
		for (auto &cur_pool: seq_uhb) {
			int dep = stidx; // 1 is 1 cycle from instn reg
			for (auto &c_: cur_pool) {
				loc_per_reg[c_].insert({dep, true});
				for (auto &p_: parent_edges[c_]) {
					loc_per_reg[p_].insert({dep-1, false});
				}
			}
			stidx++;
		}
		log("deps_anchor loc_per_reg: %d\n", loc_per_reg.size());
		for (auto &itm_: loc_per_reg) {
			log("node %s\n", itm_.first.c_str());
			for (auto &ele: itm_.second)
				log("(%d,%d),", ele.dep, ele.write ? 1 : 0);
			log("\n");
		}

		log("gen uhb:\n");
		int i = 0;
		for (auto &st_: seq_uhb) {
			My_Disjoint_Set<string> locations;
			for (auto &s_: st_) {
				log("%s\n", s_.c_str());
				i++;
				locations.ds_makeset(s_);
			}
			locations.dump_stat();
			seq_uhb_sets.push_back(locations);
			log("---------\n");
		}
		log("-~~~~~~ nun of nodes: %d ~~~~~~~~\n", i);
		log("%d\n", loc_per_reg.size());
		for (auto &itm_: loc_per_reg) {
			log("node %s\n", itm_.first.c_str());
			for (auto &ele: itm_.second)
				log("(%d,%d),", ele.dep, ele.write ? 1 : 0);
			log("\n");
		}
	}
	void gen_uhb(string const &inst_) {
		pool<string> visited; 
		pool<string> cur_pool, next_pool;
		cur_pool = edges[inst_]; 
		while (!cur_pool.empty()) {
			for (auto &c_: cur_pool) {
				for (auto &r_: edges[c_]) {
					if (visited.find(r_) != visited.end()) continue; 
					visited.insert(r_);
					next_pool.insert(r_);
				}
				visited.insert(c_);
			}
			seq_uhb.push_back(cur_pool);
			int dep = seq_uhb.size(); // 1 is 1 cycle from instn reg
			for (auto &c_: cur_pool) {
				loc_per_reg[c_].insert({dep, true});
				for (auto &p_: parent_edges[c_]) {
					loc_per_reg[p_].insert({dep-1, false});
				}
			}
			cur_pool = next_pool;
			next_pool.clear(); 
		}
		log("gen uhb:\n");
		int i = 0;
		for (auto &st_: seq_uhb) {
			My_Disjoint_Set<string> locations;
			for (auto &s_: st_) {
				log("%s\n", s_.c_str());
				i++;
				locations.ds_makeset(s_);
			}
			locations.dump_stat();
			seq_uhb_sets.push_back(locations);
			log("---------\n");
		}
		log("-~~~~~~ nun of nodes: %d ~~~~~~~~\n", i);
		log("%d\n", loc_per_reg.size());
		for (auto &itm_: loc_per_reg) {
			log("node %s\n", itm_.first.c_str());
			for (auto &ele: itm_.second)
				log("(%d,%d),", ele.dep, ele.write ? 1 : 0);
			log("\n");
		}
	}
	const dict<string, pool<node_info>> get_loc_per_reg() {
		return loc_per_reg; 
	}
	void finish(std::ofstream &outfile) {
		outfile << "digraph G {\n"
			"// aliceblue: RD_ADDR\n"
			"// orange: mem\n"
			" edge [penwidth=2];"
			"\n node [ shape=box, fontsize=20, penwidth=3, fontname=\"roboto\"]; \n rankdir=LR;\n"
			"\"annotation\" [pos=\"0,50!\", label=\"black:unconditiona or instn_dep\nblue: wr_data\ngreen: wr_addrs\ngray: rd_addr\ngold: array\ngold1:rd_data\"];\n";
		
		vector<int> grid, cal_h;
		grid.resize(depth+2); cal_h.resize(depth+2);
		for (auto &n: node_sty) {
			int px = column_node.find(n.first) == column_node.end() ? depth+1 : column_node[n.first];
			cal_h[px] += 1;
		}
		int hmax = *std::max_element(cal_h.begin(), cal_h.end());
		for (auto &n: node_sty) {
			char pos_s[100];
			int px = column_node.find(n.first) == column_node.end() ? depth+1 : column_node[n.first];
			float t = 2 * hmax / (cal_h[px] + 1);
			sprintf(pos_s, " pos=\"%d,%f!\"", px * X_SCALE, t + Y_SCALE * grid[px]);

			if (n.second.find("color") == string::npos) {
				if (px < color.size())
					sprintf(pos_s, "color=\"%s\", pos=\"%d,%f!\"", color[px].c_str(), px * X_SCALE, t + Y_SCALE * grid[px]);
			} 
			grid[px] += 1;
			outfile <<  "\"" <<  n.first << "\" [" << n.second;
			if (n.second.length()) outfile << ",";
			outfile << pos_s << "]; \n"; 
		}
		pool<pair<string, string>> e_exists;
		for (int i = 0; i < sgraph; i++) {
			auto epool = subgraphs[i];
			outfile << "subgraph cluster" << i << "{ \n";
			for (auto &e: epool) {
				outfile << "\"" << e.first << "\"->\"" << e.second << "\" [" << edge_sty[e] <<  "];\n";
				e_exists.insert(e);
			}
			outfile << "label = \"" << subgraph_label[i] << "\"; } \n";
		}
		for (auto e: edge_sty) {
			if (e_exists.find(e.first) == e_exists.end()) {
				outfile << "\"" << e.first.first << "\"->\"" << e.first.second << "\" [" << e.second <<  "];\n";
			}
		}

		outfile << "}\n";
		outfile.close();
	}

	int get_bitwidth(string &s) { 
		bool b1 = node_map.find(s) == node_map.end();
		bool b2 = node_map_mem_.find(s) == node_map_mem_.end();
		if (b1 && b2) return -1;
		if (!(b1 || b2)) { log("well a sigspec or cell !!\n"); return -1;}
		if (!b1)
			return node_map[s].size();
		else 
			return node_map_mem_[s]->parameters[ID::WIDTH].as_int();
	}
	int ismem(const string &s) {
		bool b1 = node_map.find(s) == node_map.end();
		bool b2 = node_map_mem_.find(s) == node_map_mem_.end();
		if (!b1 && b2) return 0;
		if (b1 && !b2) return 1; 
		return -1;
	}
};


enum acc_type_ {read, write};
enum hbi_type_ {structural_spatial, structural_temporal, data};
enum hbi_rsrc_type {intercore, intracore};
enum assertion_res_ {proven, cex, error, undetermined, na};
struct hbi_res {
	int 	        file_seqno; 
	hbi_type_ 		hbi_type;
	int 			i1_idx;
	int				i2_idx; // instn idx -> type 
	string 			i1_loc;
	string			i2_loc; 
	bool 			samecore; 	// true -> reference order is program order 
	assertion_res_  assertion_res;
	int 			file_seqno_next;
	
    hbi_res(): file_seqno(-3) {}
	// temporal on inter-core, array, request ordered by same core
	hbi_res(int file_seqno, hbi_type_ hbi, int i1_idx, int i2_idx, string i1_loc, string i2_loc, int file_seqno_next):
		file_seqno(file_seqno), hbi_type(hbi), i1_idx(i1_idx), i2_idx(i2_idx), i1_loc(i1_loc), i2_loc(i2_loc), file_seqno_next(file_seqno_next), samecore(true) {}
	
	hbi_res(int file_seqno, hbi_type_ hbi, int i1_idx, int i2_idx,
	 string i1_loc, string i2_loc, bool samecore): 
		file_seqno(file_seqno), hbi_type(hbi), i1_idx(i1_idx), i2_idx(i2_idx), i1_loc(i1_loc), i2_loc(i2_loc), file_seqno_next(-1), samecore(samecore) {}
	string tostr() {
		char buff[150];
		sprintf(buff, "%d,%d,%d,%d,%d,%s,%s,%d", file_seqno, hbi_type, samecore, i1_idx, i2_idx, i1_loc.c_str(), i2_loc.c_str(), file_seqno_next);
		return string(buff);
	}
	static string header() {
		char buff[150];
		//              0     1         2       3        4      5        6      7
		sprintf(buff, "file_#,hbi_type,samecore,i0_type,i1_type,i0_loc,i1_loc,relevant_file_#");
		return string(buff);
	}
	inline bool operator==(const hbi_res& other) const {
		return (
		file_seqno == other.file_seqno &&  
		hbi_type == other.hbi_type && 
		i1_idx == other.i1_idx && 
		i2_idx == other.i2_idx &&  // instn idx -> type 
		i1_loc == other.i1_loc && 
		i2_loc == other.i2_loc && 
		samecore == other.samecore && 
		1
		);
	}
	inline unsigned int hash() const {
		// return hash_ops<int>::hash(file_seqno); 
		return mkhash(mkhash(
			mkhash(hash_ops<int>::hash(file_seqno),hash_ops<int>::hash(hbi_type)),
			mkhash(hash_ops<int>::hash(i1_idx),hash_ops<int>::hash(i2_idx)))
		,
		mkhash(
			mkhash(hash_ops<string>::hash(i1_loc),hash_ops<string>::hash(i2_loc)),
			hash_ops<int>::hash(samecore)
		));
	}
};



PRIVATE_NAMESPACE_END
#endif

