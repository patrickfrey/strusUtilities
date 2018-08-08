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
#include "strus/index.hpp"
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
class QueryAnalyzerInstanceInterface;
/// \brief Forward declaration
class ErrorBufferInterface;

/// \brief Load a query from source (query language)
/// \param[in,out] query query interface to instrument
/// \param[in] analyzer program for analyzing text segments in the query
/// \param[in] selectionFeatureSet default selection feature set
/// \param[in] weightingFeatureSet default weighting feature set
/// \param[in] qproc query processor interface for info about objects loaded
/// \param[in] source source string (not a file name!) to parse
/// \param[in] analyzerfields query fields configured in the query analysis configuration
/// \param[in,out] errorhnd buffer for reporting errors (exceptions)
/// \return true on success, false on failure
bool loadQuery(
		QueryInterface& query,
		const QueryAnalyzerInstanceInterface* analyzer,
		const std::string& selectionFeatureSet,
		const std::string& weightingFeatureSet,
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

}//namespace
#endif

