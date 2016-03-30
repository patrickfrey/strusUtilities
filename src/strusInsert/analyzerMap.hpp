/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_ANALYZER_MAP_HPP_INCLUDED
#define _STRUS_ANALYZER_MAP_HPP_INCLUDED
#include "strus/documentClass.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "private/utils.hpp"
#include <string>

namespace strus {

/// \brief Forward declaration
class DocumentClass;
/// \brief Forward declaration
class AnalyzerObjectBuilderInterface;
/// \brief Forward declaration
class ErrorBufferInterface;

class AnalyzerMap
{
public:
	AnalyzerMap( const AnalyzerObjectBuilderInterface* builder_, ErrorBufferInterface* errorhnd_)
		:m_builder(builder_),m_errorhnd(errorhnd_){}
	AnalyzerMap( const AnalyzerMap& o)
		:m_map(o.m_map),m_builder(o.m_builder),m_errorhnd(o.m_errorhnd){}

	void defineProgram(
			const std::string& scheme,
			const std::string& segmenter,
			const std::string& prgfile);

	DocumentAnalyzerInterface* get( const DocumentClass& dclass) const;

private:
	void defineAnalyzerProgramSource(
			const std::string& scheme,
			const std::string& segmenter,
			const std::string& analyzerProgramSource);

	typedef std::map<std::string,utils::SharedPtr<DocumentAnalyzerInterface> > Map;
	Map m_map;
	const strus::AnalyzerObjectBuilderInterface* m_builder;
	ErrorBufferInterface* m_errorhnd;
};

}//namespace
#endif

