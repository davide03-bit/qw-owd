#!/bin/bash

for i in {1..30}
do
#new reno
ssh -T mascolo@10.50.150.69 << 'EOF'
      docker start davide-container
      docker exec davide-container bash -c "
          cd proxygen/proxygen/ &&
          ./_build/proxygen/httpserver/hq --mode=server --host=10.50.150.69 --static_root=/qw/server/ -qlogger_path=/qw/server/logs/newreno -congestion=newreno &"
EOF

./_build/proxygen/httpserver/hq --mode=client --host=10.50.150.69 --outdir=/qw/qw/client --path="/file.bin" -qlogger_path=/qw/qw/client/logs

sleep 5

ssh -T mascolo@10.50.150.69 << 'EOF'
      docker exec davide-container bash -c "
          cd proxygen/proxygen &&
          PID=\$(lsof -t -i :6667) &&
          kill \$PID
          QLOG_NAME=\$(ls -t /qw/server/logs/newreno | head -1) &&
          QLOG_PATH=/qw/server/logs/newreno/\$QLOG_NAME &&
          python3 /qw/stats/stats.py \$QLOG_PATH >> newreno.csv"
EOF
# cubic
ssh -T mascolo@10.50.150.69 << 'EOF'
      docker start davide-container
      docker exec davide-container bash -c "
          cd proxygen/proxygen &&
          ./_build/proxygen/httpserver/hq --mode=server --host=10.50.150.69 --static_root=/qw/server/ -qlogger_path=/qw/server/logs/cubic -congestion=cubic &"
EOF

./_build/proxygen/httpserver/hq --mode=client --host=10.50.150.69 --outdir=/qw/qw/client --path="/file.bin" -qlogger_path=/qw/qw/client/logs

sleep 5

ssh -T mascolo@10.50.150.69 << 'EOF'
      docker exec davide-container bash -c "
          cd proxygen/proxygen &&
          PID=\$(lsof -t -i :6667) &&
          kill \$PID
          QLOG_NAME=\$(ls -t /qw/server/logs/cubic | head -1) &&
          QLOG_PATH=/qw/server/logs/cubic/\$QLOG_NAME &&
          python3 /qw/stats/stats.py \$QLOG_PATH >> cubic.csv"
EOF

#bbr2
ssh -T mascolo@10.50.150.69 << 'EOF'
      docker start davide-container
      docker exec davide-container bash -c "
          cd proxygen/proxygen &&
          ./_build/proxygen/httpserver/hq --mode=server --host=10.50.150.69 --static_root=/qw/server/ -qlogger_path=/qw/server/logs/bbr2 -congestion=bbr2 -pacing=true &"
EOF

./_build/proxygen/httpserver/hq --mode=client --host=10.50.150.69 --outdir=/qw/qw/client --path="/file.bin" -qlogger_path=/qw/qw/client/logs

sleep 5

ssh -T mascolo@10.50.150.69 << 'EOF'
      docker exec davide-container bash -c "
          cd proxygen/proxygen &&
          PID=\$(lsof -t -i :6667) &&
          kill \$PID
          QLOG_NAME=\$(ls -t /qw/server/logs/bbr2 | head -1) &&
          QLOG_PATH=/qw/server/logs/bbr2/\$QLOG_NAME &&
          python3 /qw/stats/stats.py \$QLOG_PATH >> bbr2.csv"
EOF

#westwood
ssh -T mascolo@10.50.150.69 << 'EOF'
      docker start davide-container
      docker exec davide-container bash -c "
          cd proxygen/proxygen &&
          ./_build/proxygen/httpserver/hq --mode=server --host=10.50.150.69 --static_root=/qw/server/ -qlogger_path=/qw/server/logs/westwood+ -congestion=westwood &"
EOF

./_build/proxygen/httpserver/hq --mode=client --host=10.50.150.69 --outdir=/qw/qw/client --path="/file.bin" -qlogger_path=/qw/qw/client/logs

sleep 5

ssh -T mascolo@10.50.150.69 << 'EOF'
      docker exec davide-container bash -c "
          cd proxygen/proxygen &&
          PID=\$(lsof -t -i :6667) &&
          kill \$PID
          QLOG_NAME=\$(ls -t /qw/server/logs/westwood+ | head -1) &&
          QLOG_PATH=/qw/server/logs/westwood+/\$QLOG_NAME &&
          python3 /qw/stats/stats.py \$QLOG_PATH >> westwood.csv"
EOF

#delay control 20%
ssh -T mascolo@10.50.150.69 << 'EOF'
      docker start davide-container
      docker exec davide-container bash -c "
          cd proxygen/proxygen &&
          ./_build/proxygen/httpserver/hq --mode=server --host=10.50.150.69 --static_root=/qw/server/ -qlogger_path=/qw/server/logs/delay_control_20 -congestion=westwood_owd --use_ack_receive_timestamps=true --delay_control_fraction=0.2 > one_way_delay_stats.txt &"
EOF

./_build/proxygen/httpserver/hq --mode=client --host=10.50.150.69 --outdir=/qw/qw/client --path="/file.bin" -qlogger_path=/qw/qw/client/logs --use_ack_receive_timestamps=true

sleep 5

ssh -T mascolo@10.50.150.69 << 'EOF'
      docker exec davide-container bash -c "
          cd proxygen/proxygen &&
          PID=\$(lsof -t -i :6667) &&
          kill \$PID
          QLOG_NAME=\$(ls -t /qw/server/logs/delay_control_20 | head -1) &&
          QLOG_PATH=/qw/server/logs/delay_control_20/\$QLOG_NAME &&
          python3 /qw/stats/stats.py -owd one_way_delay_stats.txt \$QLOG_PATH >> qdc20.csv"
EOF

#delay control 50%
ssh -T mascolo@10.50.150.69 << 'EOF'
      docker start davide-container
      docker exec davide-container bash -c "
          cd proxygen/proxygen &&
          ./_build/proxygen/httpserver/hq --mode=server --host=10.50.150.69 --static_root=/qw/server/ -qlogger_path=/qw/server/logs/delay_control_50 -congestion=westwood_owd --use_ack_receive_timestamps=true --delay_control_fraction=0.5 > one_way_delay_stats.txt &"
EOF

./_build/proxygen/httpserver/hq --mode=client --host=10.50.150.69 --outdir=/qw/qw/client --path="/file.bin" -qlogger_path=/qw/qw/client/logs --use_ack_receive_timestamps=true

sleep 5

ssh -T mascolo@10.50.150.69 << 'EOF'
      docker exec davide-container bash -c "
          cd proxygen/proxygen &&
          PID=\$(lsof -t -i :6667) &&
          kill \$PID
          QLOG_NAME=\$(ls -t /qw/server/logs/delay_control_50 | head -1) &&
          QLOG_PATH=/qw/server/logs/delay_control_50/\$QLOG_NAME &&
          python3 /qw/stats/stats.py -owd one_way_delay_stats.txt \$QLOG_PATH >> qdc50.csv"
EOF
done


