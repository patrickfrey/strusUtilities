TERM sent "":sent;

SELECT selfeat;
WEIGHT docfeat;

EVAL bm25pff( debug="debugquery",
              b=0.75, k1=1.2, avgdoclen=700, metadata_doclen=doclen,
              titleinc=4.0, windowsize=60, cardinality="60%", ffbase=0.4,
              maxdf=0.2,
              .para=para, .struct=sent, .match=docfeat );

SUMMARIZE attribute( name=title );
SUMMARIZE matchphrase(
              debug="debugsummary",
              type=orig, windowsize=40, sentencesize=100, cardinality="60%",
              maxdf=0.2, matchmark='$<b>$</b>',
              .struct=sent, .para=para, .match=docfeat );

