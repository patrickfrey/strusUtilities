/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_ANALYZER_MAP_HPP_INCLUDED
#define _STRUS_ANALYZER_MAP_HPP_INCLUDED
#include "strus/analyzer/documentClass.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "private/utils.hpp"
#include <string>

namespace strus {

/// \brief Forward declaration
class AnalyzerObjectBuilderInterface;
/// \brief Forward declaration
class TextProcessorInterface;
/// \brief Forward declaration
class SegmenterInterface;
/// \brief Forward declaration
class ErrorBufferInterface;

class AnalyzerMap
{
public:
	AnalyzerMap( const AnalyzerObjectBuilderInterface* builder_, const std::string& prgfile_, const analyzer::DocumentClass& documentClass_, const std::string& defaultSegmenter_, ErrorBufferInterface* errorhnd_);

	std::string warnings() const			{return m_warnings.str();}

	const DocumentAnalyzerInterface* get( const analyzer::DocumentClass& dclass) const;

	const strus::analyzer::DocumentClass& documentClass() const
	{
		return m_documentClass;
	}

private:
	AnalyzerMap( const AnalyzerMap&){}		//... non copyable

	void defineProgram(
			const std::string& scheme,
			const std::string& segmenter,
			const std::string& prgfile);
	void defineDefaultProgram(
			const std::string& prgfile);
	void defineAnalyzerProgramSource(
			const std::string& scheme,
			const std::string& segmenter,
			const std::string& analyzerProgramSource);
	void defineAnalyzerProgramSource(
			const std::string& scheme,
			const strus::SegmenterInterface* segmenter,
			const std::string& analyzerProgramSource);

	typedef std::map<std::string,utils::SharedPtr<DocumentAnalyzerInterface> > Map;
	Map m_map;
	analyzer::DocumentClass m_documentClass;
	std::string m_defaultSegmenterName;
	const strus::SegmenterInterface* m_defaultSegmenter;
	const strus::AnalyzerObjectBuilderInterface* m_builder;
	const strus::TextProcessorInterface* m_textproc;
	std::ostringstream m_warnings;
	ErrorBufferInterface* m_errorhnd;
};

}//namespace
#endif

