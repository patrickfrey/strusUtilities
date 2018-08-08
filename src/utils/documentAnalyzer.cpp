/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "private/documentAnalyzer.hpp"
#include "strus/lib/analyzer_prgload_std.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/segmenterInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/reference.hpp"
#include "strus/base/localErrorBuffer.hpp"
#include "private/internationalization.hpp"

using namespace strus;

DocumentAnalyzer::DocumentAnalyzer(
	const AnalyzerObjectBuilderInterface* builder,
	const analyzer::DocumentClass& documentClass,
	const std::string& segmenterName,
	const std::string& prgfile,
	ErrorBufferInterface* errorhnd)
{
	const TextProcessorInterface* textproc = builder->getTextProcessor();
	if (!textproc) throw std::runtime_error(_TXT("failed to get text processor"));

	if (strus::is_DocumentAnalyzer_programfile( textproc, prgfile, errorhnd))
	{
		analyzer::SegmenterOptions segmenterOpts;
		if (!segmenterName.empty())
		{
			const SegmenterInterface* segmenter = textproc->getSegmenterByName( segmenterName);
			analyzerinst.reset( builder->createDocumentAnalyzer( segmenter, segmenterOpts));
			if (!analyzerinst.get()
			||  !strus::load_DocumentAnalyzer_programfile_std( analyzerinst.get(), textproc, prgfile, errorhnd))
			{
				throw std::runtime_error( errorhnd->fetchError());
			}
		}
		else if (documentClass.defined())
		{
			segmenterOpts = textproc->getSegmenterOptions( documentClass.scheme());
			const SegmenterInterface* segmenter = textproc->getSegmenterByMimeType( documentClass.mimeType());
			analyzerinst.reset( builder->createDocumentAnalyzer( segmenter, segmenterOpts));
			if (!analyzerinst.get()
			||  !strus::load_DocumentAnalyzer_programfile_std( analyzerinst.get(), textproc, prgfile, errorhnd))
			{
				throw std::runtime_error( errorhnd->fetchError());
			}
		}
		else
		{
			analyzermap.reset( builder->createDocumentAnalyzerMap());
			addAnalyzerMap( "textwolf", builder, textproc, prgfile, errorhnd);
			addAnalyzerMap( "cjson", builder, textproc, prgfile, errorhnd);
			addAnalyzerMap( "tsv", builder, textproc, prgfile, errorhnd);
			addAnalyzerMap( "plain", builder, textproc, prgfile, errorhnd);
		}
	}
	else
	{
		if (!segmenterName.empty())
		{
			throw std::runtime_error(_TXT("not allowed to define segmenter and to load an analyzer map"));
		}
		analyzermap.reset( builder->createDocumentAnalyzerMap());
		if (!strus::load_DocumentAnalyzerMap_programfile( analyzermap.get(), textproc, prgfile, errorhnd))
		{
			throw std::runtime_error( errorhnd->fetchError());
		}
	}
}

const DocumentAnalyzerInstanceInterface* DocumentAnalyzer::get(
	const analyzer::DocumentClass& dclass) const
{
	if (analyzerinst.get())
	{
		return analyzerinst.get();
	}
	else
	{
		return analyzermap->getAnalyzer( dclass.mimeType(), dclass.scheme());
	}
}

void DocumentAnalyzer::addAnalyzerMap( const char* segmenterName, const AnalyzerObjectBuilderInterface* builder, const TextProcessorInterface* textproc, const std::string& programFile, ErrorBufferInterface* errorhnd)
{
	if (errorhnd->hasError()) return;
	const SegmenterInterface* segmenter = textproc->getSegmenterByName( segmenterName);
	if (!segmenter)
	{
		throw strus::runtime_error(_TXT("segmenter '%s' not defined"), segmenterName);
	}
	const char* mimeType = segmenter->mimeType();
	analyzer::SegmenterOptions opts;
	strus::Reference<DocumentAnalyzerInstanceInterface> ana;
	ana.reset( builder->createDocumentAnalyzer( segmenter, opts));
	if (!ana.get())
	{
		throw std::runtime_error( errorhnd->fetchError());
	}
	if (!strus::load_DocumentAnalyzer_programfile_std( ana.get(), textproc, programFile, errorhnd))
	{
		(void)errorhnd->fetchError();
	}
	else
	{
		analyzermap->addAnalyzer( mimeType, ""/*scheme*/, ana.get());
	}
	ana.release();
}




