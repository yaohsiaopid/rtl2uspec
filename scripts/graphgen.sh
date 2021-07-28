#!/bin/bash
if [ "$#" -lt 1 ]; then
   echo "./graphgen.sh -i <IFG prefix> -s <spline use?> -d <dot use>"
   exit 0
fi 

while getopts i:s:d: flag
do
    case "${flag}" in
        i) IFG=${OPTARG};;
        s) use_spline=${OPTARG};;
        d) use_dot=${OPTARG};;
    esac
done
echo "i: $IFG";
echo "s: $use_spline";
echo "d: $use_dot";
cmd="neato"
if [ "$use_dot" = "true" ]; then
    cmd="dot"
fi

spline=""
if [ "$use_spline" = "true" ]; then
    spline="-Gsplines=polyline"
fi
set -x
eval "$cmd $spline -Tpng ${IFG}.dot -o ${IFG}.png"

