set PROP_PATH $env(HOME)/ridecore/src/fpga
set TOP_MOD reorderbuf
set GETSTAT 0


yosys log "PROP_PATH ${PROP_PATH}";
yosys log "TOP_MOD ${TOP_MOD}"
yosys verific  -vlog-incdir ${PROP_PATH};
yosys verific  -vlog-libdir ${PROP_PATH};

yosys verific -sv \
${PROP_PATH}/reorderbuf.v


#${PROP_PATH}/alloc_issue_ino.v \
#${PROP_PATH}/alu.v \
#${PROP_PATH}/arf.v \
#${PROP_PATH}/brimm_gen.v \
#${PROP_PATH}/btb.v \
#${PROP_PATH}/decoder.v \
#${PROP_PATH}/dmem.v \
#${PROP_PATH}/dualport_ram.v \
#${PROP_PATH}/exunit_alu.v \
#${PROP_PATH}/exunit_branch.v \
#${PROP_PATH}/exunit_ldst.v \
#${PROP_PATH}/exunit_mul.v \
#${PROP_PATH}/gshare.v \
#${PROP_PATH}/imem.v \
#${PROP_PATH}/imem_outa.v \
#${PROP_PATH}/imm_gen.v \
#${PROP_PATH}/mpft.v \
#${PROP_PATH}/multiplier.v \
#${PROP_PATH}/pipeline_if.v \
#${PROP_PATH}/pipeline.v \
#${PROP_PATH}/prioenc.v \
#${PROP_PATH}/ram_sync_nolatch.v \
#${PROP_PATH}/ram_sync.v \
#${PROP_PATH}/rrf_freelistmanager.v \
#${PROP_PATH}/rrf.v \
#${PROP_PATH}/rs_alu.v \
#${PROP_PATH}/rs_branch.v \
#${PROP_PATH}/rs_ldst.v \
#${PROP_PATH}/rs_mul.v \
#${PROP_PATH}/rs_reqgen.v \
#${PROP_PATH}/src_manager.v \
#${PROP_PATH}/srcopr_manager.v \
#${PROP_PATH}/srcsel.v \
#${PROP_PATH}/tag_generator.v \
#${PROP_PATH}/oldest_finder.v \
#${PROP_PATH}/storebuf.v \
#${PROP_PATH}/search_be.v \
#${PROP_PATH}/topsim.v


#yosys verific -import -d ./build/dump/verific_netlist_${TOP_MOD}.v ${TOP_MOD} ;
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
set EVAL_EVER_UPDATE 0
yosys rtl2uspec_cdfg ${EVAL_EVER_UPDATE};

#yosys verific -import -d ./build/dump_netlist_${TOP_MOD}_ ${TOP_MOD} ;
#yosys hierarchy -check -top ${TOP_MOD};  
#
#yosys dump -o ./build/${TOP_MOD}rtliil_preproc_ridecore_pre.txt;
#yosys proc; # replace processes in the design with MUXs, FFs, Latches
#yosys dump -o ./build/${TOP_MOD}rtliil_preproc_ridecore.txt;
#
#if {$GETSTAT == 1} {
#    yosys flatten;
#    yosys stat;
#    yosys ff_stat;
#    yosys ff_stat;
#    exit;   
#}
#
## yosys memory -nomap -nordff; 
## opt_clean will remove all cells ???
## memory_dff [-nordff]                (-memx implies -nordff)
##     opt_clean
##     memory_share
##     opt_clean
##     memory_memx                         (when called with -memx)
##     memory_collect
##     memory_bram -rules <bram_rules>     (when called with -bram)
##     memory_map                          (skipped if called with -nomap)
## yosys stat;
## yosys ls;
#yosys select -module ${TOP_MOD};
#set EVAL_EVER_UPDATE 0
#yosys rtl2uspec_cdfg ${EVAL_EVER_UPDATE};
## yosys dump -o ./build/${TOP_MOD}rtliil.txt;
#
