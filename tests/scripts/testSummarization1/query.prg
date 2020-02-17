TERM sent "":sent;

SELECT selfeat;
WEIGHT docfeat;

EVAL bm25pff( avgdoclen=700, metadata_doclen=doclen, maxdf=1.0, .punct=sent, .match=docfeat );

SUMMARIZE attribute( name=title );
SUMMARIZE matchphrase( text=orig, maxdf = 1.0, .punct=sent, .match=docfeat );

