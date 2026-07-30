#!/bin/sh
# Compare sc15 block's verdict with the real skidset scanner's over the corpus.
#
#     diff.sh <path-to-sc15> <cases-dir>
#
# Exit 0 only when every case was judged twice and the two agreed. Anything
# else fails, including anything that stops a judgement being made.
#
# Both judges answer 0 for accept and 1 for refuse, and nothing else is a
# verdict. This used to read every non-zero status as a refusal, which turned a
# crash, a missing binary and refjudge's own operational status 2 into
# agreement with whichever reader legitimately refused. A gate that reports
# success when it could not run is worse than no gate, and this one exists to
# catch that class of fault elsewhere.
set -eu

sc15="$1"
d="$2"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

judge()
{
    label="$1"
    shift
    if "$@" >"$tmp/out" 2>"$tmp/err"; then
        status=0
    else
        status=$?
    fi
    case "$status" in
    0) verdict=accept ;;
    1) verdict=refuse ;;
    *)
        echo "$label exited $status, which is not a verdict: $*" >&2
        sed 's/^/    /' "$tmp/err" >&2 || true
        exit 2
        ;;
    esac
}

# The corpus has to be the one corpus.sh generated. An unmatched glob stays
# literal in POSIX sh, so an empty or missing directory used to leave both
# judges failing to open the same non-existent file and agreeing about it.
test -f "$d/MANIFEST" || {
    echo "no $d/MANIFEST: run corpus.sh first" >&2
    exit 2
}
want=$(wc -l <"$d/MANIFEST")
test "$want" -gt 0 || {
    echo "$d/MANIFEST is empty" >&2
    exit 2
}
while read -r name; do
    test -f "$d/$name.drv" || {
        echo "case $name is in the manifest and not in $d" >&2
        exit 2
    }
done <"$d/MANIFEST"
have=$(find "$d" -name '*.drv' | wc -l)
test "$have" -eq "$want" || {
    echo "$d holds $have cases, the manifest lists $want" >&2
    exit 2
}

bad=0
n=0
while read -r name; do
    f="$d/$name.drv"
    n=$((n + 1))
    judge "sc15 block $name" "$sc15" block "$f"
    mine=$verdict
    judge "refjudge $name" ./refjudge "$f"
    ref=$verdict
    if [ "$mine" != "$ref" ]; then
        bad=$((bad + 1))
        why=$(./refjudge "$f" || true)
        printf 'MISMATCH  %-26s sc15=%s  skidset=%s  (%s)\n' \
            "$name" "$mine" "$ref" "$why"
    fi
done <"$d/MANIFEST"

echo "$n cases, $bad mismatches"
test "$bad" -eq 0
