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
class PatternMatcherInterface;
/// \brief Forward declaration
class PatternMatcherInstanceInterface;
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

/// \brief Parses a document class from a declaration like 'content="application/xml"; charset=UTF-8"'
/// \param[out] result returned document class
/// \param[in] source content type declaration
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success
bool parseDocumentClass(
		analyzer::DocumentClass& result,
		const std::string& source,
		ErrorBufferInterface* errorhnd);


/// \brief Structure for a big list of feature vector definitions
struct FeatureVectorList
{
	/// \brief Default constructor
	FeatureVectorList( std::size_t collsize_=0, std::size_t vecsize_=0)
		:m_nameofs(),m_namestrings(),m_vecvalues(),m_vecsize(vecsize_)
	{
		m_nameofs.reserve( collsize_);
		m_namestrings.reserve( collsize_ * 12);
		m_vecvalues.reserve( collsize_);
		
	}
	/// \brief Copy constructor
	FeatureVectorList( const FeatureVectorList& o)
		:m_nameofs(o.m_nameofs),m_namestrings(o.m_namestrings),m_vecvalues(o.m_vecvalues),m_vecsize(o.m_vecsize){}

	/// \brief Add a new term definition
	/// \param[in] term_ pointer to the name of the term (does not have to be 0 terminated)
	/// \param[in] termsize_ length of term_ in bytes
	/// \param[in] vec_ vector assigned to the term added
	void add( const char* term_, std::size_t termsize_, const std::vector<double>& vec_);

	class const_iterator;

	class Element
	{
	public:
		const char* name() const	{return m_name;}
		const double* vec() const	{return m_vec;}
		std::size_t vecsize() const	{return m_vecsize;}

		Element( const char* term_, const double* vec_, std::size_t vecsize_)
			:m_name(term_),m_vec(vec_),m_vecsize(vecsize_){}
		Element( const Element& o)
			:m_name(o.m_name),m_vec(o.m_vec),m_vecsize(o.m_vecsize){}
	private:
		friend class FeatureVectorList::const_iterator;
		const char* m_name;
		const double* m_vec;
		std::size_t m_vecsize;
	};

	/// \brief Get term vector definition by index
	/// \param[in] idx index starting from 0
	Element operator[]( std::size_t idx) const
	{
		return Element( m_namestrings.c_str() + m_nameofs[ idx], &m_vecvalues[ idx * m_vecsize], m_vecsize);
	}

	/// \brief Term definition iterator
	class const_iterator
	{
	public:
		/// \brief Constructor
		const_iterator( std::size_t itr_, const char* termstrings_base_, const std::size_t* termofs_base_, const double* vecvalues_base_, const std::size_t& vecsize_)
			:content(termstrings_base_,vecvalues_base_,vecsize_)
			,itr(itr_)
			,termstrings_base(termstrings_base_)
			,termofs_base(termofs_base_)
			,vecvalues_base(vecvalues_base_){}
		/// \brief Copy constructor
		const_iterator( const const_iterator& o)
			:content(o.content)
			,itr(o.itr)
			,termstrings_base(o.termstrings_base)
			,termofs_base(o.termofs_base)
			,vecvalues_base(o.vecvalues_base){}

		/// \brief Increment operator
		const_iterator& operator++()				{++itr; initElement(); return *this;}
		/// \brief Post increment operator
		const_iterator operator++(int)				{const_iterator rt=*this; ++itr; initElement(); return rt;}

		const Element& operator*() const			{return content;}
		const Element* operator->() const			{return &content;}

		bool operator==( const const_iterator& o) const		{return itr == o.itr;}
		bool operator!=( const const_iterator& o) const		{return itr != o.itr;}
		bool operator<( const const_iterator& o) const		{return itr < o.itr;}
		bool operator<=( const const_iterator& o) const		{return itr <= o.itr;}
		bool operator>( const const_iterator& o) const		{return itr > o.itr;}
		bool operator>=( const const_iterator& o) const		{return itr >= o.itr;}

	private:
		void initElement()					{content.m_name = termstrings_base + termofs_base[itr]; content.m_vec = vecvalues_base + itr * content.vecsize();}

		Element content;
		std::size_t itr;
		const char* termstrings_base;
		const std::size_t* termofs_base;
		const double* vecvalues_base;
	};

	/// \brief Get the begin iterator
	const_iterator begin() const		{return const_iterator( 0, m_namestrings.c_str(), m_nameofs.data(), m_vecvalues.data(), m_vecsize);}
	/// \brief Get the end iterator
	const_iterator end() const		{return const_iterator( m_nameofs.size(), 0, m_nameofs.data(), 0, m_vecsize);}

	/// \brief Get size of the term definition list
	std::size_t size() const		{return m_nameofs.size();}

private:
	std::vector<std::size_t> m_nameofs;	///< term offsets
	std::string m_namestrings;		///< term of the feature
	std::vector<double> m_vecvalues;	///< vector assigned to this feature
	std::size_t m_vecsize;			///< size of a vector
};


/// \brief Source format variants for feature vector definitions
enum FeatureVectorDefFormat {
	FeatureVectorDefTextssv,	///< text file with lines starting with the term, followed by the vector elements as double precision floating point numbers separated by spaces
	FeatureVectorDefWord2vecbin	///< binary format of Google Word2Vec
};

/// \brief Parses a feature vector definition format identifier
/// \param[out] result format id
/// \param[in] source format identifier as string
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success
bool parseFeatureVectorDefFormat(
		FeatureVectorDefFormat& result,
		const std::string& source,
		ErrorBufferInterface* errorhnd);

/// \brief Parses a list of feature vector definitions for processing with a vector space model
/// \param[out] result returned list of term to feature vector definition assignments
/// \param[in] sourceFormat format of source to parse
/// \param[in] sourceString source to parse
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success
bool parseFeatureVectors(
		FeatureVectorList& result,
		const FeatureVectorDefFormat& sourceFormat,
		const std::string& sourceString,
		ErrorBufferInterface* errorhnd);


/// \brief Result of a loadPatternMatcherProgram call, all structures created and instrumented by the loader
class PatternMatcherProgram
{
public:
	PatternMatcherProgram()
		:m_lexer(0),m_matcher(0){}
	~PatternMatcherProgram();

	void init( 
		PatternLexerInstanceInterface* lexer_,
		PatternMatcherInstanceInterface* matcher_,
		const std::vector<std::size_t>& regexidmap_,
		const std::string& regexnames_,
		const std::vector<uint32_t>& symbolRegexIdList_);

	PatternLexerInstanceInterface* fetchLexer();
	PatternMatcherInstanceInterface* fetchMatcher();

	const char* tokenName( unsigned int id) const;

private:
	PatternLexerInstanceInterface* m_lexer;
	PatternMatcherInstanceInterface* m_matcher;
	std::vector<std::size_t> m_regexidmap;
	std::string m_regexnames;
	std::vector<uint32_t> m_symbolRegexIdList;
};

/// \brief Loads and compiles a list of pattern matcher programs from source and instruments a lexer and a matcher instance with it
/// \param[out] result returned structures instrumented
/// \param[in] lexer lexer class
/// \param[in] matcher matcher class
/// \param[in] sourceString source to parse
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success
bool loadPatternMatcherProgram(
		PatternMatcherProgram& result,
		const PatternLexerInterface* lexer,
		const PatternMatcherInterface* matcher,
		const std::vector<std::pair<std::string,std::string> >& sources,
		ErrorBufferInterface* errorhnd);

}//namespace
#endif

