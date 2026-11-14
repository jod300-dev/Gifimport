#!/bin/bash
# Uso: tests/run_corpus.sh <corpus_dir> <objetos/frame> <salida.txt> [limite_segundos] [metodos 0-based...]
# Reanudable: salta las combinaciones (gif, metodo) que ya estan en <salida.txt>. Pensado para llamarse varias veces hasta completar.
C=$1; B=$2; OUT=$3; LIM=${4:-270}; shift 4
M=${@:-"2 17 18 19 20 21 22 23 24 25 26 27 28"}
touch $OUT
for g in $C/*.gif; do for m in $M; do
  n=$(basename $g); if ! grep -q "^$n *m$((m+1)) " $OUT; then echo "$g $m"; fi
done; done > /tmp/jobs.$$
timeout $LIM xargs -P 6 -L 1 bash -c 'timeout 200 /tmp/corpus_test $0 $1 '$B' 100 4 2>&1 >> '$OUT' || echo "$(basename $0) m$(( $1 + 1 )) TIMEOUT/CRASH" >> '$OUT < /tmp/jobs.$$
rm -f /tmp/jobs.$$
echo "hechos: $(wc -l < $OUT)"
