#!/bin/sh

set -e
. `dirname $0`/ENV

# Prepare test output:
ARG=$1
TESTROT=`pwd`"/"`dirname $ARG`
TESTDIR="$TESTROT/"`basename $ARG`
EXECROT="$PROJECT/tests/scripts/exec"
EXECDIR="$EXECROT/$ARG"
mkdir -p $EXECDIR

# Run test and diff with expected output:
cd $TESTDIR
FILES=`ls | tr '\n' ' ' | tr '\r' ' '`
cd -
for ff in $FILES; do cp $TESTDIR/$ff $EXECDIR/; done;
cd $EXECDIR
echo "#!/bin/bash" > RUN
echo "" >> RUN
echo ". $PROJECT/tests/scripts/ENV" >> RUN
echo "" >> RUN
cat $TESTDIR/RUN >> RUN
chmod +x RUN
./RUN | perl -pe "s@$PROJECT@/home/strus/@g" > OUT
cd -
diff $TESTDIR/EXP $EXECDIR/OUT > $EXECDIR/DIFF || true
ERR=$(wc -l <"$EXECDIR/DIFF")
echo "$ERR" > $EXECDIR/ERR
if [[ "$ERR" -gt 0 ]]; then
	echo "ERROR $ERR $1"
	exit 1
fi
# Cleanup:
for ff in $FILES; do
	rm $EXECDIR/$ff || true;
done;
rm $EXECDIR/OUT
rm $EXECDIR/DIFF
rm $EXECDIR/ERR
rmdir $EXECDIR
(rmdir $EXECROT || true) 2>/dev/null
echo "OK $1 [ $FILES]"

