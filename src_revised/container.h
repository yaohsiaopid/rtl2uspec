
#ifndef CONTAINERS_H
#define CONTAINERS_H
#include "kernel/yosys.h"
#include "kernel/sigtools.h"
#include "kernel/celltypes.h"
#include "backends/ilang/ilang_backend.h"

USING_YOSYS_NAMESPACE
PRIVATE_NAMESPACE_BEGIN
template <class T>
class My_Disjoint_Set {
	// disjoint set 
	dict<T, T> parents;
	public: 
		std::string name;
        
		My_Disjoint_Set(){}
		void ds_makeset(T sig) {
			if (parents.find(sig) == parents.end()) 
				parents[sig] = sig;
		}
		T ds_find(T sig) {
			ds_makeset(sig);
			if (parents[sig] == sig)
				return sig;
			else {
				auto t_ = ds_find(parents[sig]);
				parents[sig] = t_;
				return t_;
			}
		}

		void ds_union(T x, T y) {
			if (x == y) return;
			auto x_ = ds_find(x);
			auto y_ = ds_find(y);
			if (x_ == y_) 
				return; // log("[warn] dj cycle....\n");
			else 
				parents[x_] = y_;
		}
		pool<T> dump_stat() {
			pool<T> num;
			int r = 0;
			for (auto &p : parents) {
				auto tmp = ds_find(p.second);
				num.insert(tmp);
				r++;
				log("n: %s \t %s \n", (p.first).c_str(), (tmp).c_str());
			}
			log("r : %d\n", r);
			log("number of sets: %zu", num.size());
			return num;
		}
};

PRIVATE_NAMESPACE_END
#endif
