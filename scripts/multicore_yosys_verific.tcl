# set PROP_PATH <path to verilog src>
set PROP_PATH /home/multicore_vscale_rtl2uspec/src/main/verilog
# set TOP_MOD <top design module name>
set TOP_MOD vscale_sim_top
set GETSTAT 0

yosys log "PROP_PATH ${PROP_PATH}";
yosys log "TOP_MOD ${TOP_MOD}"
yosys verific  -vlog-incdir ${PROP_PATH};
yosys verific  -vlog-libdir ${PROP_PATH};

yosys log "--------full processor--------"
# full processor verilog lists
# yosys verific -sv <all verilog files>
yosys verific -sv \
${PROP_PATH}/vscale_sim_top_unmod.v \
${PROP_PATH}/vscale_alu.v \
${PROP_PATH}/vscale_ctrl.v \
${PROP_PATH}/vscale_mul_div.v \
${PROP_PATH}/vscale_regfile.v \
${PROP_PATH}/vscale_core.v \
${PROP_PATH}/vscale_hasti_bridge.v \
${PROP_PATH}/vscale_PC_mux.v \
${PROP_PATH}/vscale_src_a_mux.v \
${PROP_PATH}/vscale_csr_file.v \
${PROP_PATH}/vscale_imm_gen.v \
${PROP_PATH}/vscale_pipeline.v \
${PROP_PATH}/vscale_src_b_mux.v \
${PROP_PATH}/vscale_arbiter.v \
${PROP_PATH}/vscale_multicore_constants.vh \
${PROP_PATH}/vscale_dp_hasti_sram.v \
${PROP_PATH}/vscale_ctrl_constants.vh \
${PROP_PATH}/rv32_opcodes.vh \
${PROP_PATH}/vscale_alu_ops.vh \
${PROP_PATH}/vscale_md_constants.vh \
${PROP_PATH}/vscale_hasti_constants.vh \
${PROP_PATH}/vscale_csr_addr_map.vh 

set TEST 0
if {$TEST == 1} {
    yosys verific -import -d ./build/dump_netlist_${TOP_MOD}_ ${TOP_MOD} ; 
    # -d <dump_file>: dump verific netlist as verilog file
    # -V: import verific netlist as is without translating to yosys cell types
    yosys dump -o ./build/vscale.ilang ; 
    yosys hierarchy -check -top ${TOP_MOD};  
    # yosys select -module ${TOP_MOD};
    yosys select -module vscale_core;
    yosys log "---------- scc -----------------"
    yosys scc 
    yosys log "---------- ltp -----------------"
    yosys ltp
    yosys log "----------- fsm ----------------"
    yosys fsm_detectls
    exit
} else {
    yosys verific -import -d ./build/dump/verific_netlist_${TOP_MOD}.v ${TOP_MOD} ;
    yosys hierarchy -check -top ${TOP_MOD};  

    yosys dump -o ./build/dump/${TOP_MOD}rtliil_preproc.txt;
    yosys proc; # replace processes in the design with MUXs, FFs, Latches

    if {$GETSTAT == 1} {
        yosys flatten;
        yosys stat;
        yosys ff_stat;
        yosys ff_stat;
        exit; 
    }

    yosys memory -nomap -nordff; 
    yosys stat;
    set INTRAHBI -intradone
    set INTERHBI -interdone
    yosys rtl2uspec_cdfg ${INTRAHBI} ${INTERHBI};

}
