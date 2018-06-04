/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_UTILITIES_DOCUMENT_ANALYZER_IMPLEMENTATION_HPP_INCLUDED
#define _STRUS_UTILITIES_DOCUMENT_ANALYZER_IMPLEMENTATION_HPP_INCLUDED
#include "strus/lib/analyzer_prgload_std.hpp"
#include "strus/analyzer/documentClass.hpp"
#include "strus/documentAnalyzerInstanceInterface.hpp"
#include "strus/documentAnalyzerMapInterface.hpp"
#include "strus/reference.hpp"
#include "strus/base/localErrorBuffer.hpp"
#include "private/internationalization.hpp"
#include <string>

namespace strus {

/// \brief Forward declaration
class AnalyzerObjectBuilderInterface;
/// \brief Forward declaration
class TextProcessorInterface;
/// \brief Forward declaration
class ErrorBufferInterface;

class DocumentAnalyzer
{
public:
	DocumentAnalyzer(
		const AnalyzerObjectBuilderInterface* builder,
		const analyzer::DocumentClass& documentClass,
		const std::string& segmenterName,
		const std::string& prgfile,
		ErrorBufferInterface* errorhnd);

	const DocumentAnalyzerInstanceInterface* get(
		const analyzer::DocumentClass& dclass) const;

private:
	void addAnalyzerMap( const char* segmenterName, const AnalyzerObjectBuilderInterface* builder, const TextProcessorInterface* textproc, const std::string& programFile, ErrorBufferInterface* errorhnd);

private:
	strus::Reference<DocumentAnalyzerInstanceInterface> analyzerinst;
	strus::Reference<DocumentAnalyzerMapInterface> analyzermap;
};

}//namespace
#endif

