#!/bin/bash
cd /home/ai/tttt/thirdparty/corpora/DefinitelyTyped && /home/ai/tttt/release/tttt -q --keep-going --all-errors --threads 1 --checkers 1 -p types/$1 2>&1 | sed 's/\x1b\[[0-9;]*m//g' | cut -c1-400
