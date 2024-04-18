
FAIL=0

if [ "$1" == "" ]; then
	echo Please provide arg
	FAIL=1
fi

if ! readlink -f "$1"; then
	echo Warning: Path to enable "'$1'" does not exist
	FAIL=1
fi

if [ "$FAIL" == "0" ]; then
	PATH="$(readlink -f "$1")/bin/:$PATH"
	LD_LIBRARY_PATH="$(readlink -f "$1")/lib/:$LD_LIBRARY_PATH"
	LIBRARY_PATH="$(readlink -f "$1")/lib/:$LIBRARY_PATH"
fi

exit $FAIL
