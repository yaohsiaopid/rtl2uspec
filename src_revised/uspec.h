#ifndef USPEC_H
#define USPEC_H
#include <iostream>
#include <utility>
#include <string> 
#include <cmath>
#include "util.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN

struct mem_proc_req_ {
    // mem proc req op
    // should be signal names in the memory module port name of memory module 
	pair<string, string> trans_siz; // trans of word is first == second , it is mask 
    // trans_samecore.first (the addr signal) should be in part of waddr and raddr
    pool<string> trans_w; // condition that represent write enable 
    pair<string, string> waddr; // first: for same core -> 0, second for the part of actual txn addr 
    int waddr_offset;
    int waddr_offset_cell;
	pool<string> trans_r;      // condition that represent read enable , usually empty if rd_transparent
    pair<string, string> raddr; // first: for same core -> 0, second for the part of actual txn addr and comparison with the request address >> raddr_offset
    int raddr_offset;   // 
    pair<bool, int> trans_r_transparent; // does read requires trans being valid, if not, we requires trans_r_transparent.first cycle should receive request
    // string wdata;
	string rdata;
    string array_name;
    string tostr(bool rw, string addr, string mem_prefix_, const string req_event="", bool diffcore=false) {
        string mem_prefix = mem_prefix_ + "." ;
        assert(!addr.empty() && !mem_prefix.empty());
        string fmt_ = R"(
(
    TXNSZ &&  // if write
    TXNENABLE && 
    TXNADDR_CORE &&
    TXNADDR_ADDR && 
    REQEVENT &&
    1)
)";
        string txn_enable = "1 ";
        auto tmppool = (rw ? trans_w : trans_r);
        for (auto &itm: tmppool) 
            txn_enable +=  "&& " + mem_prefix + itm; 
        string addr_ = rw ? (mem_prefix + waddr.second + " == " + addr): (mem_prefix + raddr.second + " == " + addr);
        vector<s_map> replist = {
            s_map("TXNSZ", rw ?  (mem_prefix + trans_siz.first + " == " + trans_siz.second) : "1"),
            s_map("TXNENABLE", txn_enable),
            s_map("TXNADDR_CORE", rw ? (mem_prefix + waddr.first + " == 0") : (mem_prefix + raddr.first + " == " + (diffcore ? "1" : "0"))),
            s_map("TXNADDR_ADDR", mem_prefix + (rw ? waddr.second : raddr.second) + " == " + addr + " >> " + std::to_string(rw ? waddr_offset : raddr_offset)),
            s_map("REQEVENT", req_event.empty() ? "1" : req_event),
        };

        return replace_template(fmt_, replist);
    }
    
    string wdata_tostr(string addr, string val, string mem_prefix_) {
        string event = "(MEM[ADDR >> OFF] == VAL)";
        return replace_template(event, {s_map("MEM", mem_prefix_ + "." + array_name), s_map("VAL", val), s_map("ADDR", addr), s_map("OFF", waddr_offset_cell)});
    }
    string rdata_non_intervene(const string &dataval, const string &cntr, string mem_prefix_) {
        string event = "(RDATA == VAL || (RDATA != VAL && CNTR == 2))";
        return replace_template(event, {s_map("RDATA", mem_prefix_ + "." + rdata), s_map("VAL", dataval), s_map("CNTR", cntr)});
    }
	
};

struct mem_rec_req_ {
    // mem rec req format
    // should be input port name of memory module 
	pair<string, string> trans_rw;// write is first == second
	pair<string, string> trans_valid; // valid trx is first == second
    pair<string, string> trans_siz; // trans of word is first == second 
    // trans_samecore.first (the addr signal) should be in part of waddr
    pair<string, string> trans_samecore; // trans from same core condition first == second 
    pair<string, string> trans_diffcore; // trans from diff core condition first == second 
	string waddr;

	// string wdata;
	string raddr;
	// string rdata;
    string tostr(bool rw, string addr, string mem_prefix_, bool samecore=true) {
        string mem_prefix = mem_prefix_ + "." ;
        assert(!addr.empty() && !mem_prefix.empty());
        string fmt_ = R"(
(TXN_TYPE == TYPE_VAL && 
 TXN_VALID && 
 TXN_ADDR == ADDRVAL && 
 TXNCORE &&
 TXN_SZ_WD && 
    1)
)";
        vector<s_map> replist = {
            s_map("TXN_TYPE", mem_prefix + trans_rw.first),
            s_map("TYPE_VAL", rw ? trans_rw.second : "!(" +  trans_rw.second + ")" ),
            s_map("TXN_VALID", mem_prefix + trans_valid.first + " == " + trans_valid.second),
            s_map("TXN_ADDR", mem_prefix + (rw ? waddr : raddr)),
            s_map("ADDRVAL", addr),
            s_map("TXNCORE", (samecore ? 
            (mem_prefix + trans_samecore.first + " == " +  trans_samecore.second) : 
            (mem_prefix + trans_diffcore.first + " == " +  trans_diffcore.second)) 
            ),
            s_map("TXN_SZ_WD", mem_prefix + trans_siz.first + " == " + trans_siz.second),
        };

        return replace_template(fmt_, replist);
    }
    
	
};
struct core_mem_req_ {
	// percore output signals
    string pc_;
	pair<string, string> trans_rw;// write is first == second
	pair<string, string> trans_valid; // valid trx is first == second
    pair<string, string> trans_siz; // trans of word is first == second 
	string waddr;
	string wdata;
	string raddr;
	string rdata;
	// should be one of output port name of core module 
    string tostr(bool rw, string addr, string percore_prefix_) {
        string percore_prefix = percore_prefix_ + ".";
        assert(!addr.empty() && !percore_prefix.empty());
        string fmt_ = R"(
(TXN_TYPE == TYPE_VAL && 
 TXN_VALID && 
 TXN_ADDR == ADDRVAL && 
 TXN_SZ_WD && 
    1)
)";
        vector<s_map> replist = {
            s_map("TXN_TYPE", percore_prefix + trans_rw.first),
            s_map("TYPE_VAL", rw ? trans_rw.second : "!(" +  trans_rw.second + ")" ),
            s_map("TXN_VALID", percore_prefix + trans_valid.first + " == " + trans_valid.second),
            s_map("TXN_ADDR", percore_prefix + (rw ? waddr : raddr)),
            s_map("ADDRVAL", addr),
            s_map("TXN_SZ_WD", percore_prefix + trans_siz.first + " == " + trans_siz.second),
        };

        return replace_template(fmt_, replist);
    }
};


enum prop_type {assert, assume};
enum data_type {reg, wire};
enum expr_type {
    primary, 
    compound,
    func}; 
// primary = literal | literal [lr:rr] (as lvalue)
// compound = primary | (primary op primary)
class EXPR_ {
    expr_type type_;
    string op_; // binary op or op_(some_val)
    EXPR_ *left, *right;
    string name_;
    int bit_;
    int lr, rr; // name_[lr:rr]

public:
    expr_type type() const { return type_; }

    string str() const {
        if (type_ == primary) { 
            if (lr > 0) 
                return name_ + "[" + std::to_string(lr) + " : " + std::to_string(rr);
            else 
                return name_;
        }
        if (type_ == func) {
            assert(left != nullptr);
            return op_ + "( " + left->str() + " ) ";
        }
        string ret = "";
        if (left != nullptr)
            ret += left->str();
        ret += " " + op_ + " ";
        if (right != nullptr)
            ret += right->str();
        return ret;
    }

    int bit() const { return bit_; }

    EXPR_(): name_(""), left(nullptr), right(nullptr) {}; // empty 
    EXPR_(const EXPR_ &a, int l, int r) {
        assert(a.type_ == primary && a.lr == -1); 
        type_ = a.type_;
        name_ = a.name_;
        bit_ = a.bit_;
        lr = l;
        rr = r;
    }
    EXPR_(const string literal): name_(literal), type_(primary), lr(-1) { }; // just value
    EXPR_(const string name, int bit): name_(name), type_(primary), bit_(bit), lr(-1) {};
    EXPR_(const string op, EXPR_ *l, EXPR_ *r): name_(""), type_(compound), op_(op), left(l), right(r) { }; // general
    EXPR_(const string op, EXPR_ *arg): op_(op), name_(""), type_(func), left(arg) { };

    friend class USPEC_SVA; 
    //string addDeclaration(data_type type_, EXPR_ &lval);
}; 

enum seq_type_ { expr_dist, seq_expr };
//class SEQ_ {
//    seq_type_ type_;
//    string delay_pre;
//    EXPR_ cur_expr; // ::= .... | expr [* const_range_expr]
//    string delay_post;
//    bool consecutive; // add [*0:$] or not
//    int lr, rr;
//    SEQ_ *pre_seq, *post_seq;
//public:
//    SEQ_(): type_(expr_dist), consecutive(false) {}
//    SEQ_(const EXPR_ &a, bool cont=false, int lr_=-1, int rr_=-1, const string pre="", const string post=""): type_(expr_dist), cur_expr(a), consecutive(cont), delay_pre(pre), delay_post(post), lr(lr_), rr(rr_) {} // expr [*0:$] or expr only 
//
//    SEQ_(SEQ_ *pre, string dpre, SEQ_ *post): type_(seq_expr), pre_seq(pre), delay_pre(dpre), post_seq(post) {} // pre dpre post
//    string str() const {
//        string ret = "";
//        if (type_ == expr_dist) {
//            ret = cur_expr.str();
//            if (consecutive) {
//                string l_ = std::to_string(lr);
//                string r_ = std::to_string(rr);
//                if (lr < 0) l_ = "0";
//                if (rr < 0) r_ = "$";
//                ret += " [*" + l_ + ":" + r_ + "] ";
//            } 
//            return ret;
//        }
//        if (type_ == seq_expr) {
//            if (pre_seq != nullptr)
//                ret += pre_seq->str();
//            ret += " " + delay_pre + " ";
//            if (post_seq != nullptr)
//                ret += post_seq->str();
//        }
//    }
//
//
//};
//
class USPEC_SVA {
    vector<string> declarations_;       // wire ...
    vector<string> statements_;         // assign .... 
    //vector<SEQ_> seqs_;                 // sequences 
    vector<string> properties_;         // assume/assert


    int consts_cntr;
public:
    USPEC_SVA(): consts_cntr(0) {
        declarations_.clear();
        statements_.clear();
        //seqs_.clear();
        properties_.clear();
    }
    void addImmImpli(string pre, string post, bool noninterfere = false, prop_type p_ = assume) {
        string ret = (p_ == assume ? "assume" : "assert" );
        ret += " property (@(posedge CLK) ";
        ret += "( " + pre +  " ) |-> ( " + post + " )"; 
        ret += ");";
        properties_.push_back(ret);
        if (noninterfere) {
            string ret = "assume property (@(posedge CLK) ";
            ret += "!( " + pre +  " ) |-> !( " + post + " )"; 
            ret += ");";
            properties_.push_back(ret);
        }
    }
    void addImmImpli(EXPR_ pre, EXPR_ post, bool noninterfere = false, prop_type p_ = assume) {
        string ret = (p_ == assume ? "assume" : "assert" );
        ret += " property (@(posedge CLK) ";
        ret += "( " + pre.str() +  " ) |-> ( " + post.str() + " )"; 
        ret += ");";
        properties_.push_back(ret);
        if (noninterfere) {
            string ret = "assume property (@(posedge CLK) ";
            ret += "!( " + pre.str() +  " ) |-> !( " + post.str() + " )"; 
            ret += ");";
            properties_.push_back(ret);
        }
    }
    void addImpli(EXPR_ pre, EXPR_ post){
        string ret = "assume property (@(posedge CLK) ";
        ret += "( " + pre.str() +  " ) |=> ( " + post.str() + " )"; 
        ret += ");";
        properties_.push_back(ret);
    }
    void addInOrderPartial(prop_type p_, EXPR_ e1, EXPR_ e2, bool strong=false) {
        string ret = "";
        // add odering that e1 happens before e2 
        // >> a = "!(e1 || e2) [*0:$] ##1 e1 && !e2"
        string t0 = "(e1) [*0:$] ##1 \n(e2)";
        ret = replace_template(t0, {
                s_map("e1", e1.str()),
                s_map("e2", e2.str())});
        string s = (p_ == assert ? "assert" : "assume");
        s += " property (@(posedge CLK) first |-> (";
        if (strong) 
            s += "strong";
        s += "( " + ret + " )";
        s += ")); // addInOrderPartial";
        properties_.push_back(s);
    }

    void addInOrder(prop_type p_, EXPR_ e1, EXPR_ e2, bool strong=false, bool progress=true) {
        string ret = "";
        // add odering that e1 happens before e2 
        // >> a = "!(e1 || e2) [*0:$] ##1 e1 && !e2"
        string t0 = R"(
!((e1) || (e2)) [*0:$] ##1 
(e1 && !(e2))
)";
        ret = replace_template(t0, {
                s_map("e1", e1.str()),
                s_map("e2", e2.str())});
        if (progress) {
            // e2 happens (strong)
            // >> a + "##1" + "!e2 [*0:$] ##1 e2 [*1:$] ##1 !e2"
            string t1 = R"(##1 !(e2) [*0:$] ##1 
(e2) [*1:$] ##1 !(e2)
)";
            ret += replace_template(t1, {
                    s_map("e2", e2.str())});
       }
        string s = (p_ == assert ? "assert" : "assume");
        s += " property (@(posedge CLK) first |-> (";
        if (strong) 
            s += "strong";
        s += "( " + ret + " )";
        s += ")); // addInOrder";
        properties_.push_back(s);
    }

    void addProperty(prop_type p_, string pp, string comment="") {
        string s = (p_ == assert ? "assert" : "assume");
        s += " property (@(posedge CLK) (" + pp + ")); // " + comment;
        properties_.push_back(s);
    }
    void addProperty(prop_type p_, EXPR_ l, string comment="") {
        string s = (p_ == assert ? "assert" : "assume");
        s += " property (@(posedge CLK) (" + l.str() + ")); // " + comment; 
        properties_.push_back(s);

    }

    string trans(EXPR_ *e) {
        EXPR_ paste = EXPR_("$past", e); // $past(e)
        EXPR_ nege = EXPR_("!", e); // $past(e)
        EXPR_ ret = EXPR_("&&", &paste, &nege);
        return ret.str(); 
    } 
    EXPR_ addPrev(EXPR_ e, int bit=1) {
       EXPR_ prev_("prev_" + std::to_string(consts_cntr), bit);
       consts_cntr++;
       addDeclaration(reg, prev_);

       string tmp_ = R"(
always @(posedge CLK) begin
    if (RESET) REG <= 0;
    else REG <= EVT;
end)";

       string ret = replace_template(tmp_, {s_map("REG", prev_.str()), s_map("EVT", e.str())});
       declarations_.push_back(ret);
       //tmp_e.push_back(prev_);
       //int idx = tmp_e.size() - 1;
       //return &tmp_e[idx];
       return prev_;
    }
    EXPR_ addEvent(data_type type_, string e) {
        EXPR_ evt("event_" + std::to_string(consts_cntr));
        consts_cntr++;
        string ret = (type_ == reg ? "reg " : "wire ");
        ret += evt.str() +  " = ( " + e + " ); // addEvent";
        declarations_.push_back(ret);
        return evt;
    }
    void addDeclaration(string &s) {
        declarations_.push_back(s);
    }
    void addDeclaration(data_type type_, EXPR_ &lval) { 
        string ret = (type_ == reg ? "reg " : "wire ");
        if (lval.bit_ > 1) 
            ret += "[" + std::to_string(lval.bit_) + "-1 : 0] " ;
        ret += lval.str() + ";"; 
        declarations_.push_back(ret);
    }

    string addStatement(EXPR_ &lval, EXPR_ &rval) {
        assert (lval.type() == primary);
        return "assign " + lval.str() + " = " + rval.str(); 
    }
    string genstr() const {
        string ret = "";
        ret += "// ----- declarations ---- \n";
        for (auto &d: declarations_) ret += d + "\n";
        ret += "// ----- statements ---- \n";
        for (auto &d: statements_) ret += d + "\n";
        ret += "// ----- seqs ---- \n";
        //for (auto &d: seqs_) ret += d + "\n"; 
        ret += "// ----- props ---- \n";
        for (auto &d: properties_) ret += d + "\n";
        return ret;
    }

    void finish(const string &file, const string clk_s, const string reset_s) const {
		std::ofstream outf(file, std::ios::out);
        string ret = genstr();//"";
        //ret += "// ----- declarations ---- \n";
        //for (auto &d: declarations_) ret += d + "\n";
        //ret += "// ----- statements ---- \n";
        //for (auto &d: statements_) ret += d + "\n";
        //ret += "// ----- seqs ---- \n";
        ////for (auto &d: seqs_) ret += d + "\n"; 
        //ret += "// ----- props ---- \n";
        //for (auto &d: properties_) ret += d + "\n";
        outf << replace_template(ret, {s_map("CLK", clk_s), s_map("RESET", reset_s)}); 
        outf.close();
        //return ret;
    }

    EXPR_ addConst(int bit,  string comment="") {
       // 1. add primary expression
       string prefix = (comment.empty() ? "c" : comment);
       EXPR_ e(prefix + "_" + std::to_string(consts_cntr), bit);
       consts_cntr++;
       // 2. add declaration
       addDeclaration(wire, e);
       // 3. add property
       addProperty(assume, EXPR_("CONST", &e));
       // replace??? rtemplate
       return e;
    }
    void addAlign(EXPR_ &a, int lr, int rr) {
        EXPR_ e(a, lr, rr);
        EXPR_ zero("0");
        EXPR_ cmp("==", &e, &zero);
        addProperty(assume, cmp);
    }
    void addValConstraint(EXPR_ &a, int val) {
        assert(a.type() == primary);
        int b_ = a.bit();
        assert(((1<<b_)-1) >= val);
        EXPR_ v(std::to_string(b_) + "'d" + std::to_string(val));
        EXPR_ cmp("<=", &a, &v);
        addProperty(assume, cmp);
    }


/*
    addEvent = 
        * declare primary expression
        * declare compound expression
    addSeq = 
        * input: expr | (seq op seq)
    addProp =
        * input: expr | seq
        * assume/assert (input)
*/
        
};
PRIVATE_NAMESPACE_END
#endif
