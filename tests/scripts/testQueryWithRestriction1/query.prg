SELECT selfeat;
WEIGHT docfeat;

EVAL bm25( b=0.75, k1=1.2, avgdoclen=7, .match=docfeat);

SUMMARIZE attribute( name=title );
SUMMARIZE attribute( name=docid );
