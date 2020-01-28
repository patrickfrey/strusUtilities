SELECT selfeat;
WEIGHT docfeat;

EVAL bm25( b=0.75, k1=1.2, avgdoclen=700, .match=docfeat);
EVAL metadata( name=pageweight );
FORMULA "0.7 * _1 * _0 + 0.3 * _0";

SUMMARIZE title = attribute( name=title );
SUMMARIZE docid = attribute( name=docid );
