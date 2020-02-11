TERM sent "":sent;

SELECT selfeat;
WEIGHT docfeat;

EVAL bm25pff( debug="query", avgdoclen=700, metadata_doclen=doclen, maxdf=0.2, .punct=sent, .match=docfeat );

SUMMARIZE attribute( name=title );
SUMMARIZE matchphrase( debug="summary", content=orig, .punct=sent, .match=docfeat );

