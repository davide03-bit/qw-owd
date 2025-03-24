#!/bin/bash

ssh -T studenti@10.73.0.20 << 'EOF'
      docker start davide-container
      docker exec davide-container bash -c "
          cd qw/proxygen/proxygen/ &&
          ./_build/proxygen/httpserver/hq --mode=server --host=10.73.0.20 --static_root=/qw/qw/server/ -qlogger_path=/qw/qw/server/logs/cubic -congestion=cubic &"
EOF

./_build/proxygen/httpserver/hq --mode=client --host=10.73.0.20 --outdir=/qw/qw/client --path="/file.bin" -qlogger_path=/qw/qw/client/logs

ssh -T studenti@10.73.0.20 << 'EOF'
      docker exec davide-container bash -c "
          cd qw/proxygen/proxygen/ &&
          PID=\$(lsof -t -i :6667) &&
          kill \$PID
          QLOG_NAME=\$(ls -lt /qw/qw/server/logs/cubic | head -2 | tail -1 | cut -f 9 -d ' ') &&
          QLOG_PATH=/qw/qw/server/logs/cubic/\$QLOG_NAME &&
          python3 /qw/qw/stats/stats.py \$QLOG_PATH"
EOF
