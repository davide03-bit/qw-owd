#!/bin/bash
sub=$1
for i in {0..39}
do
file=$(ls -t /qw/qw/server/logs/$1 | head -n $((i+1)) | tail -1)
python3 /qw/qw/stats/stats.py --output $sub_$i.png /qw/qw/server/logs/$sub/$file
done

