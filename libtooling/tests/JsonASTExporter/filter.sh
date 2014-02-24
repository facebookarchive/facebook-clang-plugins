#/bin/bash
"$@" | grep -E -o '(?:"[^"]*" [,:]|\["[^"]*"\])' | awk -F\" '{ print $2 }' | sort | uniq -c
