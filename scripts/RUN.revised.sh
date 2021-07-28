#!/bin/bash
usage() {
    printf "\
usage: ./RUN.sh -e <0/1>  -t <tcl file name>
1.to generate SVA to extract DFG pass \'-e 0'; 
after it finishes, intra_hbi.zip is generated;
To evaluate intra_hbi.zip on any machine with JG,
`python3 script/intra_hbi_0.py`
assume evaluation returns intra_hbi/ folder with new file *.res
2. get DFG result pass \'-e 1'"
}
eval=0
build=1
tclfile=./scripts/multicore_yosys_verific.tcl
srcfile=main
if [ $# -eq 0 ]; then
    usage
    exit
fi
while [[ $# -gt 0 ]]
do
key="$1"
case $key in
    -e|--eval)
    eval="$2"
    shift # past argument
    shift # past value
    ;;
    -b|--build)
    build="$2"
    shift
    shift
    ;;
    -s|--src)
    srcfile="$2"
    shift # past argument
    shift # past value
    ;;
    -t|--tcl)
    tclfile="$2"
    shift # past argument
    shift # past value
    ;;
    -h|--help)
    usage
    exit
    ;;
    *)    # unknown option
    POSITIONAL+=("$1") # save it in an array for later
    shift # past argument
    ;;
esac
done

echo "tclfile is ${tclfile}"
sed -i "s~set EVAL_EVER_UPDATE.*~set EVAL_EVER_UPDATE ${eval}~" $tclfile
sed -i "s~tcl.*~tcl ${tclfile}~" ./scripts/run.ys

if [ "${build}" -eq "1" ]; then
    echo "---- build ----"
    make -f Makefile.revised  clean
    make -f Makefile.revised  
fi

set -x
yosys -s ./scripts/run.ys -m ./build/obj/${srcfile}.so > b.log
# ./scripts/drop_sva_ctrl.sh
exit 3
