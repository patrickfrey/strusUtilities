strusUtilities program loader	 {#mainpage}
=============================

The project strusUtilities provides some command line programs to create, delete and access 
a strus information retrieval collection storage.
It also provides a library to load data into strus define in configuration programs in a 
specific syntax. The interface of this program loader library is introduced here.
The program loader library uses the public interfaces of strus and does not create a parallel
universe. Everything you can instantiate with this interface can also be constructed using
the strus core or analyzer interface.


Programs for analyzer configuration:
------------------------------------
* [Load document analyzer program](@ref strus::loadDocumentAnalyzerProgram)
* [Load query analyzer program](@ref strus::loadQueryAnalyzerProgram)
* [Define a phrase type for the query analyzer by its subparts](@ref strus::loadQueryAnalyzerPhraseType)

Programs for query evaluation configuration:
--------------------------------------------
* [Load a query evaluation program](@ref strus::loadQueryEvalProgram)

Programs for parsing a query from source (query language implementation):
-------------------------------------------------------------------------
* [Load a query from source](@ref strus::loadQuery)

Scan a source file containing a list multiple programs:
-------------------------------------------------------
* [Scan the next program source](@ref strus::scanNextProgram)

Loading storage contents from file:
-----------------------------------
* [Load meta data assignments from file](@ref strus::loadDocumentMetaDataAssignments)
* [Load document attribute assignments from file](@ref strus::loadDocumentAttributeAssignments)
* [Load user right assignments from file](@ref strus::loadDocumentUserRightsAssignments)


