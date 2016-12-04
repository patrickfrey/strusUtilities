/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Various functions to instantiate strus components from configuration programs loaded from source
/// \file programLoader.hpp
#ifndef _STRUS_UTILITIES_PROGRAM_LOADER_HPP_INCLUDED
#define _STRUS_UTILITIES_PROGRAM_LOADER_HPP_INCLUDED
#include "strus/analyzer/documentClass.hpp"
#include "strus/base/stdint.h"
#include <string>
#include <vector>
#include <set>
#include <map>

/// \brief strus toplevel namespace
namespace strus {

/// \brief Forward declaration
class TextProcessorInterface;
/// \brief Forward declaration
class QueryProcessorInterface;
/// \brief Forward declaration
class QueryEvalInterface;
/// \brief Forward declaration
class QueryInterface;
/// \brief Forward declaration
class DocumentAnalyzerInterface;
/// \brief Forward declaration
class QueryAnalyzerInterface;
/// \brief Forward declaration
class StorageClientInterface;
/// \brief Forward declaration
class PatternLexerInterface;
/// \brief Forward declaration
class PatternLexerInstanceInterface;
/// \brief Forward declaration
class PatternTermFeederInterface;
/// \brief Forward declaration
class PatternTermFeederInstanceInterface;
/// \brief Forward declaration
class PatternMatcherInterface;
/// \brief Forward declaration
class PatternMatcherInstanceInterface;
/// \brief Forward declaration
class VectorSpaceModelBuilderInterface;
/// \brief Forward declaration
class ErrorBufferInterface;

/// \brief Some default settings for parsing and building the query
struct QueryDescriptors
{
	std::string defaultFieldType;			///< default field type name if not explicitely specified
	std::string selectionFeatureSet;		///< default feature set used for document selection
	std::string weightingFeatureSet;		///< default feature set used for document weighting
	float defaultSelectionTermPart;			///< default percentage of weighting terms required in selection
	std::string defaultSelectionJoin;		///< default operator used to join terms for selection

	QueryDescriptors()
		:defaultFieldType(),selectionFeatureSet(),weightingFeatureSet()
		,defaultSelectionTermPart(0.6),defaultSelectionJoin("contains"){}
	QueryDescriptors( const QueryDescriptors& o)
		:defaultFieldType(o.defaultFieldType)
		,selectionFeatureSet(o.selectionFeatureSet)
		,weightingFeatureSet(o.weightingFeatureSet)
		,defaultSelectionTermPart(o.defaultSelectionTermPart)
		,defaultSelectionJoin(o.defaultSelectionJoin)
		{}
};

/// \brief Load a document analyzer program from source
/// \param[in,out] analyzer analyzer program to instatiate
/// \param[in] textproc provider for text processing functions
/// \param[in] source source string (not a file name!) to parse
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success, false on failure
/// \note The grammar of the analyzer program source is defined <a href="http://www.project-strus.net/grammar_analyerprg.htm">here</a>.
bool loadDocumentAnalyzerProgram(
		DocumentAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& source,
		ErrorBufferInterface* errorhnd);

/// \brief Load a query analyzer program from source
/// \param[in,out] analyzer analyzer program to instatiate
/// \param[in,out] qdescr some defaults for query language parsing filled by this procedure
/// \param[in] textproc provider for text processing functions
/// \param[in] source source string (not a file name!) to parse
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success, false on failure
bool loadQueryAnalyzerProgram(
		QueryAnalyzerInterface& analyzer,
		QueryDescriptors& qdescr,
		const TextProcessorInterface* textproc,
		const std::string& source,
		ErrorBufferInterface* errorhnd);

/// \brief Description of one element of an analyzer map
struct AnalyzerMapElement
{
	AnalyzerMapElement(){}
	AnalyzerMapElement( const AnalyzerMapElement& o)
		:scheme(o.scheme),segmenter(o.segmenter),prgFilename(o.prgFilename){}
	void clear()
		{scheme.clear(); segmenter.clear(); prgFilename.clear();}

	std::string scheme;		///< document class id type or list of element descriptions
	std::string segmenter;		///< segmenter to use
	std::string prgFilename;	///< analyzer program to use
};

/// \brief Determine if 'source' is most likely a source describing an analyzer map
/// \param[in] source source candidate
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true, if yes, false, else
bool isAnalyzerMapSource(
		const std::string& source,
		ErrorBufferInterface* errorhnd);

/// \brief Load a map of definitions describing how different document types are mapped to an analyzer program
/// \param[in] mapdef list of definitions to instrument
/// \param[in] source source with definitions
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success, false on failure
bool loadAnalyzerMap(
		std::vector<AnalyzerMapElement>& mapdef,
		const std::string& source,
		ErrorBufferInterface* errorhnd);

/// \brief Load a query evaluation program from source
/// \param[in,out] qeval query evaluation interface to instrument
/// \param[in,out] qdescr query descriptors to instrument
/// \param[in] qproc query processor interface for info about objects loaded
/// \param[in] source source string (not a file name!) to parse
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success, false on failure
bool loadQueryEvalProgram(
		QueryEvalInterface& qeval,
		QueryDescriptors& qdescr,
		const QueryProcessorInterface* qproc,
		const std::string& source,
		ErrorBufferInterface* errorhnd);

/// \brief Load a query from source (query language)
/// \param[in,out] query query interface to instrument
/// \param[in] analyzer program for analyzing text segments in the query
/// \param[in] qproc query processor interface for info about objects loaded
/// \param[in] source source string (not a file name!) to parse
/// \param[in] qdescr query descriptors to use in case something is not explicitely specified
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success, false on failure
bool loadQuery(
		QueryInterface& query,
		const QueryAnalyzerInterface* analyzer,
		const QueryProcessorInterface* qproc,
		const std::string& source,
		const QueryDescriptors& qdescr,
		ErrorBufferInterface* errorhnd);

/// \brief Load a simple query analyzer config for one field (name "") producing one feature type ("")
/// \param[in,out] query analyzer interface to instrument
/// \param[in] textproc textprocessor to retrieve the functions loaded
/// \param[in] normalizersrc source string (not a file name!) of the normalizers to parse
/// \param[in] tokenizersrc source string (not a file name!) of the tokenizer to parse
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success, false on failure
/// \note This simplistic function is mainly intended for debugging and the program strusAnalyzePhrase that just checks the result of a tokenizer with some normalizers
bool loadPhraseAnalyzer(
		QueryAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& normalizersrc,
		const std::string& tokenizersrc,
		ErrorBufferInterface* errorhnd);

/// \brief Scan a source for the next program segment in a source that contains multiple programs.
///		The programs are separated by "\r\n.\r\n" or "\n.\n".
///		No escaping of this sequence possible.
/// \param[out] segment the program segment scanned
/// \param[in] itr scanning iterator on a source containing one or multiple programs
/// \param[in] end end iterator of the source to scan
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true, if there was a segment left to scan
/// \note This function is mainly used for loading test programs
/// \remark The scanner skips whitespaces at the start of each program segment and returns initial end of line that belongs to the separator. So whitespaces should have no meaning in the languages of the programs loaded this way.
bool scanNextProgram(
		std::string& segment,
		std::string::const_iterator& itr,
		const std::string::const_iterator& end,
		ErrorBufferInterface* errorhnd);

/// \brief Load some meta data assignments for a storage from a stream
/// \param[in,out] storage the storage to instrument
/// \param[in] metadataName name of the meta data field to assign
/// \param[in] file the file to read from
/// \param[in] commitsize number of documents to update until an implicit commit is called (0 => no implicit commit)
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return the number of documents (non distinct) updated
unsigned int loadDocumentMetaDataAssignments(
		StorageClientInterface& storage,
		const std::string& metadataName,
		const std::string& file,
		unsigned int commitsize,
		ErrorBufferInterface* errorhnd);

/// \brief Load some attribute assignments for a storage from a stream
/// \param[in,out] storage the storage to instrument
/// \param[in] attributeName name of the attribute to assign
/// \param[in] file the file to read from
/// \param[in] commitsize number of documents to update until an implicit commit is called (0 => no implicit commit)
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return the number of documents (non distinct) updated
unsigned int loadDocumentAttributeAssignments(
		StorageClientInterface& storage,
		const std::string& attributeName,
		const std::string& file,
		unsigned int commitsize,
		ErrorBufferInterface* errorhnd);

/// \brief Load some user rights assignments for a storage from a stream
/// \param[in,out] storage the storage to instrument
/// \param[in] file the file to read from
/// \param[in] commitsize number of documents to update until an implicit commit is called (0 => no implicit commit)
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return the number of documents (non distinct) updated
unsigned int loadDocumentUserRightsAssignments(
		StorageClientInterface& storage,
		const std::string& file,
		unsigned int commitsize,
		ErrorBufferInterface* errorhnd);

/// \brief Parses a document class from a declaration like 'content="application/xml"; charset=UTF-8"'
/// \param[out] result returned document class
/// \param[in] source content type declaration
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success
bool parseDocumentClass(
		analyzer::DocumentClass& result,
		const std::string& source,
		ErrorBufferInterface* errorhnd);

/// \brief Adds the feature definitions in the file with path vectorfile to a vector space model builder
/// \param[in] vsmbuilder VSM builder object where to add the loaded vectors to
/// \param[in] vectorfile Path of the file to parse, either a google binary vector file format or text
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success
bool loadVectorSpaceModelVectors( 
		VectorSpaceModelBuilderInterface* vsmbuilder,
		const std::string& vectorfile,
		ErrorBufferInterface* errorhnd);


/// \brief Result of a loadPatternMatcherProgram call, all structures created and instrumented by the loader
class PatternMatcherProgram
{
public:
	PatternMatcherProgram()
		:m_lexer(0),m_termFeeder(0),m_matcher(0){}
	~PatternMatcherProgram();

	void init( 
		PatternLexerInstanceInterface* lexer_,
		PatternTermFeederInstanceInterface* termFeeder_,
		PatternMatcherInstanceInterface* matcher_,
		const std::vector<std::string>& regexidmap_,
		const std::vector<uint32_t>& symbolRegexIdList_);

	PatternLexerInstanceInterface* fetchLexer();
	PatternTermFeederInstanceInterface* fetchTermFeeder();
	PatternMatcherInstanceInterface* fetchMatcher();

	const char* tokenName( unsigned int id) const;

private:
	PatternLexerInstanceInterface* m_lexer;
	PatternTermFeederInstanceInterface* m_termFeeder;
	PatternMatcherInstanceInterface* m_matcher;
	std::vector<std::string> m_regexidmap;
	std::vector<uint32_t> m_symbolRegexIdList;
};

/// \brief Loads and compiles a list of pattern matcher programs from source and instruments a lexer and a matcher instance with it
/// \param[out] result returned structures instrumented
/// \param[in] lexer lexer class
/// \param[in] matcher matcher class
/// \param[in] sources sources to parse
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success
bool loadPatternMatcherProgram(
		PatternMatcherProgram& result,
		const PatternLexerInterface* lexer,
		const PatternMatcherInterface* matcher,
		const std::vector<std::pair<std::string,std::string> >& sources,
		ErrorBufferInterface* errorhnd);

/// \brief Loads and compiles a list of pattern matcher programs from source and instruments a term feeder for pattern matching as analyzer post processing and a matcher instance
/// \param[out] result returned structures instrumented
/// \param[in] termFeeder feeder for analyzer output terms
/// \param[in] matcher matcher class
/// \param[in] sources sources to parse
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success
bool loadPatternMatcherProgramForAnalyzerOutput(
		PatternMatcherProgram& result,
		const PatternTermFeederInterface* termFeeder,
		const PatternMatcherInterface* matcher,
		const std::vector<std::pair<std::string,std::string> >& sources,
		ErrorBufferInterface* errorhnd);

}//namespace
#endif

