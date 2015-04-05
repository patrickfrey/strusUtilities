## Some command line programs to call strus functions.
The command line programs introduced here can call built-in functions and functions in loadable modules.

### strusCreate
Create a strus storage.

### strusDestroy
Remove a strus storage.

### strusInspect
Inspect elements of items inserted in a strus storage.

### strusAnalyze
Dump the document analyze result without feeding the storage. This program can be used to check the result of the document analysis.

### strusAnalyzePhrase
Call the query analyzer with a phrase to analyze. This program can also be used to check details of the document analyzer as it tokenizes and normalizes a text segment with the tokenizer and normalizer specified.

### strusSegment
Call the segmenter with a document and one or more expressions to exract with the segmenter. Dump the resulting segments to stdout.

### strusInsert
Insert a document or all files in a directory or in any descendant directory of it.

### strusCheckStorage
This program checks a strus storage for corrupt data.

### strusCheckInsert
Processes the documents the same way as strusInsert. But instead of inserting the documents, it checks if the document representation in the storage is complete compared with the checked documents.

### strusQuery
Evaluate a query per command line.

### strusAlterMetaData
Alter the table structure for document metadata of a storage.

### strusGenerateKeyMap
Dumps a list of terms as result of document anaylsis of a file or directory. The dump can be loaded by the storage on startup to create a map of frequently used terms.

### strusDumpStatistics
Dumps the statisics that would be populated to other peer storages in case of a distributed index to stout. 

### strusDumpStorage
This program dumps the contents of a strus storage to stout

