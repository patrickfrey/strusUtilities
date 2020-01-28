TERM sent "":sent;

SELECT selfeat;
WEIGHT docfeat;

EVAL bm25( b=0.75, k1=1.2, avgdoclen=700, .match=docfeat);

SUMMARIZE title = attribute( name=title );
SUMMARIZE docid = attribute( name=docid );
