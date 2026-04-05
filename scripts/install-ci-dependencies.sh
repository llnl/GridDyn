#!/bin/bash

# Set variables based on build environment
if [[ "$TRAVIS" == "true" ]]; then
	if [[ "$TRAVIS_OS_NAME" == "osx" ]]; then
		HOMEBREW_NO_AUTO_UPDATE=1 brew install ccache
		export PATH="/usr/local/opt/ccache/libexec:$PATH"
	fi

	export CI_DEPENDENCY_DIR=${TRAVIS_BUILD_DIR}/dependencies

	WAIT_COMMAND=travis_wait

	# Convert commit message to lower case
	commit_msg=$(tr '[:upper:]' '[:lower:]' <<<${TRAVIS_COMMIT_MESSAGE})

	if [[ "$TRAVIS_OS_NAME" == "linux" ]]; then
		os_name="Linux"
	elif [[ "$TRAVIS_OS_NAME" == "osx" ]]; then
		os_name="Darwin"
	fi
else
	export CI_DEPENDENCY_DIR=$1
	commit_msg=""
	os_name="$(uname -s)"
fi

boost_version=$CI_BOOST_VERSION
if [[ -z "$CI_BOOST_VERSION" ]]; then
	boost_version=1.63.0
fi
boost_install_path=${CI_DEPENDENCY_DIR}/boost

cmake_version=$CI_CMAKE_VERSION
if [[ -z "$CI_CMAKE_VERSION" ]]; then
	cmake_version=3.5.2
fi
cmake_install_path=${CI_DEPENDENCY_DIR}/cmake

if [[ "$USE_MPI" ]]; then
	mpi_install_path=${CI_DEPENDENCY_DIR}/mpi
	case $USE_MPI in
	mpich*)
		mpi_implementation=mpich
		mpi_version=3.2
		;;
	openmpi*)
		mpi_implementation=openmpi
		mpi_version=3.0.0
		;;
	*)
		echo "USE_MPI must be either mpich or openmpi to build mpi as a dependency"
		;;
	esac
fi

# Wipe out cached dependencies if commit message has '[update_cache]'
if [[ $commit_msg == *'[update_cache]'* ]]; then
	individual="false"
	if [[ $commit_msg == *'boost'* ]]; then
		rm -rf ${boost_install_path}
		individual="true"
	fi
	if [[ "$USE_MPI" ]]; then
		if [[$commit_msg == *'mpi'* ]]; then
			rm -rf ${mpi_install_path}
			individual="true"
		fi
	fi

	# If no dependency named in commit message, update entire cache
	if [[ "$individual" != 'true' ]]; then
		rm -rf ${CI_DEPENDENCY_DIR}
	fi
fi

if [[ ! -d "${CI_DEPENDENCY_DIR}" ]]; then
	mkdir -p ${CI_DEPENDENCY_DIR}
fi

# Install CMake
if [[ ! -d "${cmake_install_path}" ]]; then
	./scripts/install-dependency.sh cmake ${cmake_version} ${cmake_install_path}
fi

# Set path to CMake executable depending on OS
if [[ "$os_name" == "Linux" ]]; then
	export PATH="${cmake_install_path}/bin:${PATH}"
	echo "*** cmake installed ($PATH)"
elif [[ "$os_name" == "Darwin" ]]; then
	export PATH="${cmake_install_path}/CMake.app/Contents/bin:${PATH}"
	echo "*** cmake installed ($PATH)"
fi

# Install MPI if USE_MPI is set
if [[ "$USE_MPI" ]]; then
	if [[ ! -d "${mpi_install_path}" ]]; then
		# if mpi_implementation isn't set, then the mpi implementation requested wasn't recognized
		if [[ ! -z "${mpi_implementation}" ]]; then
			echo "*** build ${mpi_implementation}"
			travis_wait ./scripts/install-dependency.sh ${mpi_implementation} ${mpi_version} ${mpi_install_path}
			echo "*** built ${mpi_implementation} successfully"
		fi
	fi
fi

# Install Boost
if [[ ! -d "${boost_install_path}" ]]; then
	echo "*** build boost"
	${WAIT_COMMAND} ./scripts/install-dependency.sh boost ${boost_version} ${boost_install_path}
	echo "*** built boost successfully"
fi

if [[ "$os_name" == "Linux" ]]; then
	export LD_LIBRARY_PATH=${boost_install_path}/lib:$LD_LIBRARY_PATH
elif [[ "$os_name" == "Darwin" ]]; then
	export DYLD_FALLBACK_LIBRARY_PATH=${boost_install_path}/lib:$DYLD_FALLBACK_LIBRARY_PATH
fi
