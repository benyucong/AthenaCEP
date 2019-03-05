#!/bin/bash
rm latency_SlPMS.dat
rm throughput_SlPMS.dat

for i in `seq 1 60`;
do
../../src_testing_bike_sharing/bin/cep_match -q P1 -s -c bike.eql -T 1  -x 0."$i" -p monitoring_P1.csv > SlPMS_"$i".txt
python process-latency.py latency.csv $i >> latency_SlPMS.dat
python process-throughput.py monitoring_P1.csv $i >> throughput_SlPMS.dat
done
