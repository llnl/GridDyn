#!/bin/bash
tests=(
	/root/project/build/bin/testLibrary
	/root/project/build/bin/testComponents
	/root/project/build/bin/testSystem
)

SUMRESULT=0
for test in "${tests[@]}"; do
	echo "${test}"
	eval "${test}"
	RESULT=$?
	echo "***Latest test result: "${RESULT}
	SUMRESULT=$((SUMRESULT + RESULT))
done
# Return 0 or a positive integer for failure
exit ${SUMRESULT}
