#!/bin/sh
# usage: analyze.sh <cpu.csv>   (matrix rows only; extras are reported separately)
f="$1"
m="$f.matrix.tmp"
awk -F, 'NR>1 && $2=="matrix"' "$f" > "$m"
echo "matrix_rows $(wc -l < "$m" | tr -d ' ')"
sort -t, -k12,12g "$m" | awk -F, '{a[NR]=$12} END{ if (NR%2) printf "median_cpu_pct %.3f\n", a[(NR+1)/2]; else printf "median_cpu_pct %.3f\n", (a[NR/2]+a[NR/2+1])/2; printf "min_cpu_pct %.3f\n", a[1] }'
echo "worst_matrix_row:"; sort -t, -k12,12gr "$m" | head -1
echo "worst_all_row:"; awk -F, 'NR>1 && $12+0>mx{mx=$12+0; line=$0} END{print line}' "$f"
echo "variable_marginal_means (cpu_pct mean over matrix rows at each level):"
for spec in 3:sr 4:block 5:body 7:mic 8:room_pct 9:amp; do
  c=$(echo "$spec" | cut -d: -f1); name=$(echo "$spec" | cut -d: -f2)
  awk -F, -v c="$c" -v name="$name" '{s[$c]+=$12; k[$c]++} END{
     mn=1e18; mx=-1e18
     for (x in s) { v=s[x]/k[x]; printf "  %s=%s mean %.3f (n=%d)\n", name, x, v, k[x]; if (v<mn) mn=v; if (v>mx) mx=v }
     printf "SPREAD %s %.3f pct-points (max/min %.2fx)\n", name, mx-mn, (mn>0 ? mx/mn : 0)
  }' "$m" | sort
done
rm -f "$m"
