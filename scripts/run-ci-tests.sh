#!/bin/bash

#TEST_CONFIG="Continuous"
for i in "$@"; do
	case $i in
	--valgrind)
		echo "Running Valgrind tests"
		RUN_VALGRIND=true
		;;
	--cachegrind)
		echo "Running cachegrind tests"
		RUN_CACHEGRIND=true
		;;
	--asan)
		echo "Tests using address sanitizer"
		export ASAN_OPTIONS=detect_leaks=0
		export LSAN_OPTIONS=verbosity=1:log_threads=1
		;;
	--msan)
		echo "Tests using memory sanitizer"
		;;
	--ubsan)
		echo "Tests using undefined behavior sanitizer"
		export UBSAN_OPTIONS=print_stacktrace=1
		CTEST_OPTIONS+=" --verbose"
		;;
	--no-ctest)
		echo "Disable tests using ctest as a runner"
		NO_CTEST=true
		;;
	--disable-unit-tests)
		DISABLE_UNIT_TESTS=true
		;;
	--ctest-xml-output)
		CTEST_OPTIONS+=" -T Test"
		;;
	*)
		TEST_CONFIG=$i
		TEST_CONFIG_GIVEN=true
		;;
	esac
done

if [[ "$RUN_CACHEGRIND" == "true" ]]; then
	valgrind --track-origins=yes --tool=cachegrind src/gridDynMain/gridDynMain ../examples/179busDynamicTest.xml
fi

if [[ "$NO_CTEST" == "true" ]]; then
	echo "CTest disabled, full set of CI tests may not run"
	if [[ "$RUN_VALGRIND" == "true" ]]; then
		echo "-- Valgrind will not run"
	fi

	# LSan doesn't like being run under CTest; running a single dynamics case instead of hardcoding commands for all unit tests
	# ASAN_OPTIONS=detect_leaks=0 LSAN_OPTIONS=verbosity=1:log_threads=1 src/gridDynMain/gridDynMain ../examples/179busDynamicTest.xml
else
	# Include quicktest, nightlytest, or releasetest in the branch name to run a particular set of tests
	export CTEST_OUTPUT_ON_FAILURE=true

	TEST_CONFIG="Continuous"
	if [[ "$TEST_CONFIG_GIVEN" == "true" ]]; then
		test_label=$(tr '[:upper:]' '[:lower:]' <<<$TEST_CONFIG)
		case "${test_label}" in
		*quick*)
			TEST_CONFIG="Quick"
			;;
		*nightly*)
			TEST_CONFIG="Nightly"
			;;
		*release*)
			TEST_CONFIG="Release"
			;;
		*)
			TEST_CONFIG="Continuous"
			;;
		esac

	# If no argument was given, but we are running in Travis, check the branch name for tests to run
	elif [[ "$TRAVIS" == "true" ]]; then
		case "${TRAVIS_BRANCH}" in
		*quicktest*)
			TEST_CONFIG="Quick"
			;;
		*nightlytest*)
			TEST_CONFIG="Nightly"
			;;
		*releasetest*)
			TEST_CONFIG="Release"
			;;
		*)
			TEST_CONFIG="Continuous"
			;;
		esac
	fi

	if [[ "$RUN_VALGRIND" == "true" ]]; then
		echo "Running Valgrind tests"
		ctest -T memcheck -L Valgrind && cat Testing/Temporary/MemoryChecker*.log
	fi

	# Run the CI tests last so that the execution status is used for the pass/fail status shown
	if [[ "$DISABLE_UNIT_TESTS" != "true" ]]; then
		echo "Running ${TEST_CONFIG} tests"
		ctest -L ${TEST_CONFIG} ${CTEST_OPTIONS}
	fi
fi
