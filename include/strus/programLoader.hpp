/*
---------------------------------------------------------------------
    The C++ library strus implements basic operations to build
    a search engine for structured search on unstructured data.

    Copyright (C) 2013,2014 Patrick Frey

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

--------------------------------------------------------------------

	The latest version of strus can be found at 'http://github.com/patrickfrey/strus'
	For documentation see 'http://patrickfrey.github.com/strus'

--------------------------------------------------------------------
*/
#ifndef _STRUS_UTILITIES_PROGRAM_LOADER_HPP_INCLUDED
#define _STRUS_UTILITIES_PROGRAM_LOADER_HPP_INCLUDED
#include <string>
#include <vector>
#include <istream>

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


/// \brief Load a document analyzer program from source
/// \param[in] analyzer analyzer program to instatiate
/// \param[in] textproc provider for text processing functions
/// \param[in] source source string (not a file name!) to parse
void loadDocumentAnalyzerProgram(
		DocumentAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& source);

/// \brief Load a query analyzer program from source
/// \param[in] analyzer analyzer program to instatiate
/// \param[in] textproc provider for text processing functions
/// \param[in] source source string (not a file name!) to parse
void loadQueryAnalyzerProgram(
		QueryAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& source);

/// \brief Load a phrase type definition from its source components
/// \param[in] query query interface to instrument
/// \param[in] analyzer program for analyzing text segments in the query
/// \param[in] textproc provider for text processing functions
/// \param[in] phrasetype name of phrase type to define
/// \param[in] featuretype name of the feature type produced by the defined phrase type
/// \param[in] normalizersrc source with normalizer definitions
/// \param[in] tokenizersrc source with tokenizer definitions
void loadQueryAnalyzerPhraseType(
		QueryAnalyzerInterface& analyzer,
		const TextProcessorInterface* textproc,
		const std::string& phrasetype,
		const std::string& featuretype,
		const std::string& normalizersrc,
		const std::string& tokenizersrc);

/// \brief Load a query evaluation program from source
/// \param[in] qeval query evaluation interface to instrument
/// \param[in] qproc query processor interface for info about objects loaded
/// \param[in] source source string (not a file name!) to parse
void loadQueryEvalProgram(
		QueryEvalInterface& qeval,
		const QueryProcessorInterface* qproc,
		const std::string& source);

/// \brief Load a query from source (query language)
/// \param[in] query query interface to instrument
/// \param[in] analyzer program for analyzing text segments in the query
/// \param[in] qproc query processor interface for info about objects loaded
/// \param[in] source source string (not a file name!) to parse
void loadQuery(
		QueryInterface& query,
		const QueryAnalyzerInterface* analyzer,
		const QueryProcessorInterface* qproc,
		const std::string& source);


/// \brief Scan a source for the next program segment in a source that contains multiple programs.
///		The programs are separated by "\r\n.\r\n" or "\n.\n".
///		No escaping of this sequence possible.
/// \param[out] the program segment scanned
/// \param[in] itr scanning iterator on a source containing one or multiple programs
/// \param[in] end end iterator of the source to scan
/// \return true, if there was a segment left to scan
/// \note This function is mainly used for loading test programs
/// \remark The scanner skips whitespaces at the start of each program segment and returns initial end of line that belongs to the separator. So whitespaces should have no meaning in the languages of the programs loaded this way.
bool scanNextProgram(
		std::string& segment,
		std::string::const_iterator& itr,
		const std::string::const_iterator& end);

/// \brief Load the global statistics for a storage from a stream
/// \param[in,out] storage the storage to instrument
/// \param[in,out] stream the stream to read from
void loadGlobalStatistics(
		StorageClientInterface& storage,
		std::istream& stream);

/// \brief Load some meta data assignments for a storage from a stream
/// \param[in,out] storage the storage to instrument
/// \param[in] metadataName name of the meta data field to assign
/// \param[in,out] stream the stream to read from
/// \param[in] commitsize number of documents to update until an implicit commit is called (0 => no implicit commit)
/// \return the number of documents (non distinct) updated
unsigned int loadDocumentMetaDataAssignments(
		StorageClientInterface& storage,
		const std::string& metadataName,
		std::istream& stream,
		unsigned int commitsize=0);

/// \brief Load some attribute assignments for a storage from a stream
/// \param[in,out] storage the storage to instrument
/// \param[in] attributeName name of the attribute to assign
/// \param[in,out] stream the stream to read from
/// \param[in] commitsize number of documents to update until an implicit commit is called (0 => no implicit commit)
/// \return the number of documents (non distinct) updated
unsigned int loadDocumentAttributeAssignments(
		StorageClientInterface& storage,
		const std::string& attributeName,
		std::istream& stream,
		unsigned int commitsize=0);

/// \brief Load some user rights assignments for a storage from a stream
/// \param[in,out] storage the storage to instrument
/// \param[in,out] stream the stream to read from
/// \param[in] commitsize number of documents to update until an implicit commit is called (0 => no implicit commit)
/// \return the number of documents (non distinct) updated
unsigned int loadDocumentUserRightsAssignments(
		StorageClientInterface& storage,
		std::istream& stream,
		unsigned int commitsize=0);

}//namespace
#endif

