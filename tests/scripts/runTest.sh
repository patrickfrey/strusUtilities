#!/bin/sh

set -e
. `dirname $0`/ENV

# Prepare test output:
ARG=$1
TESTROT=$PROJECT/tests/scripts
TESTDIR=$TESTROT/$ARG
EXECROT="$PROJECT/tests/scripts/exec"
EXECDIR="$EXECROT/$ARG"
rm -Rf $EXECDIR
mkdir -p $EXECDIR

# Run test and diff with expected output:
cd $TESTDIR
FILES=`ls | tr '\n' ' ' | tr '\r' ' '`
cd -
for ff in $FILES; do cp -Rf $TESTDIR/$ff $EXECDIR/; done;
cd $EXECDIR
echo "#!/bin/bash" > RUN
echo "" >> RUN
echo ". $PROJECT/tests/scripts/ENV" >> RUN
echo "" >> RUN
cat $TESTDIR/RUN >> RUN
chmod +x RUN
./RUN | perl -pe "s@$PROJECT@/home/strus/@g" > OUT
ERRNO=$?
if test "$ERRNO" -gt 0; then
	echo "ERROR (ERRNO $ERRNO)"
	exit 1
fi
cd -
diff $TESTDIR/EXP $EXECDIR/OUT > $EXECDIR/DIFF || true
ERR=$(wc -l <"$EXECDIR/DIFF")
echo "$ERR" > $EXECDIR/ERR
if test "$ERR" -gt 0; then
	echo "DIFF AT $ERR"
	exit 2
fi
# Cleanup:
for ff in $FILES; do
	rm -Rf $EXECDIR/$ff || true;
done;
rm $EXECDIR/OUT
rm $EXECDIR/DIFF
rm $EXECDIR/ERR
rmdir $EXECDIR
(rmdir $EXECROT || true) 2>/dev/null
echo "OK $1 [ $FILES]"

