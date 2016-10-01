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
class SegmenterInterface;
/// \brief Forward declaration
class ErrorBufferInterface;

class AnalyzerMap
{
public:
	AnalyzerMap( const AnalyzerObjectBuilderInterface* builder_, const std::string& prgfile_, const analyzer::DocumentClass& documentClass_, const std::string& defaultSegmenter_, ErrorBufferInterface* errorhnd_);
	AnalyzerMap( const AnalyzerMap& o)
		:m_map(o.m_map),m_documentClass(o.m_documentClass),m_defaultAnalyzerProgramSource(o.m_defaultAnalyzerProgramSource)
		,m_defaultSegmenterName(o.m_defaultSegmenterName),m_defaultSegmenter(o.m_defaultSegmenter)
		,m_builder(o.m_builder),m_errorhnd(o.m_errorhnd){}

	void defineProgram(
			const std::string& scheme,
			const std::string& segmenter,
			const std::string& prgfile);

	const strus::analyzer::DocumentClass& documentClass() const
	{
		return m_documentClass;
	}

	const DocumentAnalyzerInterface* get( const analyzer::DocumentClass& dclass);

private:
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
	std::string m_defaultAnalyzerProgramSource;
	std::string m_defaultSegmenterName;
	const strus::SegmenterInterface* m_defaultSegmenter;
	const strus::AnalyzerObjectBuilderInterface* m_builder;
	ErrorBufferInterface* m_errorhnd;
};

}//namespace
#endif

