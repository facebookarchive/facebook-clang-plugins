#/bin/bash
"$@" 2>&1 | tail -n 100
