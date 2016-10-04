#!/bin/bash

set -e
. `dirname $0`/ENV

# Prepare test output:
TESTROT=`pwd`"/"`dirname $1`
TESTDIR="$TESTROT/"`basename $1`
EXECROT="$PROJECT/tests/scripts/exec"
EXECDIR="$EXECROT/$1"
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
diff $1/EXP $EXECDIR/OUT > $EXECDIR/DIFF
ERR=`cat $EXECDIR/DIFF | wc -l`
if [[ "$ERR" -gt 0 ]]; then
	echo "ERROR $1"
	exit 1
fi
# Cleanup:
for ff in $FILES; do
	rm $EXECDIR/$ff || true;
done;
rm $EXECDIR/OUT
rm $EXECDIR/DIFF
rmdir $EXECDIR
rmdir $EXECROT || true
echo "OK $1 [ $FILES]"
