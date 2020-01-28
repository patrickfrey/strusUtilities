TERM sent "":sent;

SELECT selfeat;
WEIGHT docfeat;

EVAL smart( function="if_gt( ff, 0, 1, 0) / qf", .match=docfeat);

SUMMARIZE title = attribute( name=title );
SUMMARIZE docid = attribute( name=docid );
