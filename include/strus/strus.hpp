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
#ifndef _STRUS_MAIN_INCLUDE_HPP_INCLUDED
#define _STRUS_MAIN_INCLUDE_HPP_INCLUDED
// Basic storage data types:
#include "strus/index.hpp"
#include "strus/constants.hpp"
#include "strus/arithmeticVariant.hpp"
#include "strus/reference.hpp"
#include "strus/statCounterValue.hpp"

// Key/value store database used by the storage to store its persistent data:
#include "strus/lib/database_leveldb.hpp"
#include "strus/databaseInterface.hpp"
#include "strus/databaseCursorInterface.hpp"
#include "strus/databaseBackupCursorInterface.hpp"
#include "strus/databaseTransactionInterface.hpp"

// Storage (storage structure that defines all data tables needed for query evaluation):
#include "strus/lib/storage.hpp"
#include "strus/storageInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/storageDocumentInterface.hpp"
#include "strus/invAclIteratorInterface.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "strus/storageAlterMetaDataTableInterface.hpp"
#include "strus/attributeReaderInterface.hpp"
#include "strus/forwardIteratorInterface.hpp"
#include "strus/postingIteratorInterface.hpp"
#include "strus/postingJoinOperatorInterface.hpp"
#include "strus/storagePeerTransactionInterface.hpp"
#include "strus/storagePeerInterface.hpp"
#include "strus/peerStorageTransactionInterface.hpp"

// Query processor (functions for the query evaluation used for ranking and summarization):
#include "strus/lib/queryproc.hpp"
#include "strus/queryProcessorInterface.hpp"
#include "strus/summarizerFunctionInterface.hpp"
#include "strus/summarizerClosureInterface.hpp"
#include "strus/summarizerConfigInterface.hpp"
#include "strus/weightingClosureInterface.hpp"
#include "strus/weightingFunctionInterface.hpp"
#include "strus/weightingConfigInterface.hpp"

// Query evaluation (processing a query to get a ranked list of documents with attributes):
#include "strus/lib/queryeval.hpp"
#include "strus/queryEvalInterface.hpp"
#include "strus/queryInterface.hpp"
#include "strus/weightedDocument.hpp"
#include "strus/resultDocument.hpp"

// Document/query analyzer (create from text a structure suitable to insert into a storage):
#include "strus/lib/analyzer.hpp"
#include "strus/queryAnalyzerInterface.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/analyzer/attribute.hpp"
#include "strus/analyzer/document.hpp"
#include "strus/analyzer/metaData.hpp"
#include "strus/analyzer/term.hpp"

// Text processor (functions for the document analysis to produce index terms, attributes and meta data out of segments of text):
#include "strus/lib/textprocessor.hpp"
#include "strus/textProcessorInterface.hpp"

// Document segmenter (segmenting a document into typed text segments that can be processed by the analyzer):
#include "strus/lib/segmenter_textwolf.hpp"
#include "strus/segmenterInstanceInterface.hpp"
#include "strus/segmenterInterface.hpp"

// Tokenizer (functions for the text processor to split a text segment into tokens):
#include "strus/lib/tokenizer_word.hpp"
#include "strus/lib/tokenizer_punctuation.hpp"
#include "strus/tokenizer/token.hpp"
#include "strus/tokenizerConfig.hpp"
#include "strus/tokenizerInterface.hpp"

// Token normalizer (functions for the text processor to map tokens to normalized terms for the storage):
#include "strus/lib/normalizer_snowball.hpp"
#include "strus/normalizerConfig.hpp"
#include "strus/normalizerInterface.hpp"

// Loading and building strus objects from source (some parsers for languages to configure strus objects from source):
#include "strus/programLoader.hpp"
#endif

