#!/bin/bash
for i in {0..49}; do 
	echo "============="
	echo " Patching $i"
	echo "============="
	line=`sed -n $(($i+1))p /home/hjq/lab/crash/backward_maillist/list.txt`
	info=${line:17}
	patch -p1 < /home/hjq/lab/crash/backward_maillist/patches/$i.patch
	git add *
	git commit -a -m "$info"
done;
