#!/bin/sh

set -e

OS=$(uname -s)

case $OS in
	Linux)
		sudo apt-get update -qq
		sudo apt-get install -y \
			cmake \
			libleveldb-dev
		sudo add-apt-repository -y ppa:kojoley/boost
		sudo apt-get -q update
		sudo apt-get install libboost-atomic1.58-dev libboost-thread1.58-dev libboost-system1.58-dev libboost-filesystem1.58-dev libboost-regex1.58-dev
		if test "x$STRUS_WITH_VECTOR" = "xYES"; then
			sudo apt-get install -y libatlas-dev liblapack-dev libblas-dev libarmadillo-dev
		fi
		if test "x$STRUS_WITH_PATTERN" = "xYES"; then
			sudo apt-get install -y libtre-dev ragel
		fi
		;;

	Darwin)
		brew update
		brew upgrade cmake
		# -- brew upgrade boost
		brew install gettext snappy leveldb || true

		# make sure cmake finds the brew version of gettext
		brew link --force gettext || true
		brew link leveldb || true
		brew link snappy || true
		if test "x$STRUS_WITH_VECTOR" = "xYES"; then
			brew install lapack openblas armadillo || true
		fi
		if test "x$STRUS_WITH_PATTERN" = "xYES"; then
			brew install tre ragel
		fi
		;;
	
	*)
		echo "ERROR: unknown operating system '$OS'."
		;;
esac

