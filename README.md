# General flow
* User annotation should be made at `src_revised/design.h`
* Modify tcl to run yosys (that load the design) on the design: `scripts/multicore_yosys_verific.tcl`  
# Example
Below example goes through multicore-v-scale design. Assume the design is at /home/multicore_vscale_rtl2uspec

1. Run the flow to generate the uspec
    a. `make init`
    b. `make intra_hbi`
        2.1 copy `build/sva/intra_hbi` to the jaspergold environemt and put under `multicore_vscale_rtl2uspec/gensva/`
    c. `make inter_hbi`
    d. `make uspec`
2. Evaluate the generated uspec with pipecheck
    `make eval_uspec`
 
# Temporary fix for migration to tabbyCAD
Refer to file: `micro21_fix_tmp.png` and `hsiao_micro21_ae_tabby.pdf`


