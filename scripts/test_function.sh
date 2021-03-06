#!/bin/bash

# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

. $(dirname $0)/commons.sh

export LLVM_PROFILE_FILE=/tmp/mozillavpn.llvm-0
REPORT_FILE=/tmp/report.html

print N "This script runs the functional tests"
print N ""

ID=0

runTest() {
  ID=$((ID+1))
  export LLVM_PROFILE_FILE=/tmp/mozillavpn.llvm-$ID

  print Y "Running the app..."
  "$1" &>/tmp/VPN_LOG.txt &
  PID=$!
  print G "done."

  print Y "Running the test: $2"
  mocha $2 || ERROR=yes

  wait $PID

  cat /tmp/VPN_LOG.txt

  if [ "$ERROR" = yes ]; then
    print R "Nooo"
    exit 1
  fi
}

if [ "$1" == "" ] || ! [ -f "$1" ]; then
  print G "Usage:"
  print N "\t$0 /path/mozillavpn"
  exit 1
fi

if ! [ -d "src" ] || ! [ -d "tests" ]; then
  die "This script must be executed at the root of the repository."
fi

printn Y "Retrieving mozillavpn version... "
"$1" -v 2>/dev/null || die "Failed."
print G "done."

if [ -f "$2" ]; then
  runTest "$1" "$2"
else
  for i in tests/functional/test*; do
    runTest "$1" "$i"
  done
fi

FILES=()
for ((I=0; $I <= $ID; I=$I+1)); do
  LLVM_PROFILE_FILE=/tmp/mozillavpn.llvm-$I
  if ! [ -f $LLVM_PROFILE_FILE ]; then
    print Y "$LLVM_PROFILE_FILE: No report generated!"
    continue
  fi

  FILES[$I]="$LLVM_PROFILE_FILE"
done

if [ ${#FILES[*]} == 0 ]; then
  print Y "No reports generated"
  exit 0
fi

printn Y "Merge the profile data... "
llvm-profdata-10 merge ${FILES[@]} -o /tmp/mozillavpn.llvm-final || die "Failed to merge the coverage report"
print G "done."

print Y "Report:"
llvm-cov-10 report "$1" -instr-profile=/tmp/mozillavpn.llvm-final src

printn Y "Generating the HTML report... "
llvm-cov-10 show "$1" -instr-profile=/tmp/mozillavpn.llvm-final src -format=html > $REPORT_FILE || die "Failed to generate the HTML report"
print G $REPORT_FILE
