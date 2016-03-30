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
#include <string>
#include <vector>

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
class ErrorBufferInterface;


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
/// \param[in] textproc provider for text processing functions
/// \param[in] source source string (not a file name!) to parse
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success, false on failure
bool loadQueryAnalyzerProgram(
		QueryAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& source,
		ErrorBufferInterface* errorhnd);

/// \brief Load a phrase type definition from its source components
/// \param[in,out] analyzer program for analyzing text segments in the query
/// \param[in] textproc provider for text processing functions
/// \param[in] phrasetype name of phrase type to define
/// \param[in] featuretype name of the feature type produced by the defined phrase type
/// \param[in] normalizersrc source with normalizer definitions
/// \param[in] tokenizersrc source with tokenizer definitions
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success, false on failure
bool loadQueryAnalyzerPhraseType(
		QueryAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& phrasetype,
		const std::string& featuretype,
		const std::string& normalizersrc,
		const std::string& tokenizersrc,
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
/// \param[in] qproc query processor interface for info about objects loaded
/// \param[in] source source string (not a file name!) to parse
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success, false on failure
bool loadQueryEvalProgram(
		QueryEvalInterface& qeval,
		const QueryProcessorInterface* qproc,
		const std::string& source,
		ErrorBufferInterface* errorhnd);

/// \brief Load a query from source (query language)
/// \param[in,out] query query interface to instrument
/// \param[in] analyzer program for analyzing text segments in the query
/// \param[in] qproc query processor interface for info about objects loaded
/// \param[in] source source string (not a file name!) to parse
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success, false on failure
bool loadQuery(
		QueryInterface& query,
		const QueryAnalyzerInterface* analyzer,
		const QueryProcessorInterface* qproc,
		const std::string& source,
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

}//namespace
#endif

