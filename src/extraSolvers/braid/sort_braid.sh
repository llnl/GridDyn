#!/bin/bash

file=$1
cat ${file}* >tmp
rm ${file}*
sort -g tmp | awk 'BEGIN {tprev=0} {t=$1;$1="";print t" "t-tprev""$0;tprev=t}' >${file}_000.dat
rm tmp
