/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "insertProcessor.hpp"
#include "strus/constants.hpp"
#include "strus/index.hpp"
#include "strus/numericVariant.hpp"
#include "strus/documentAnalyzerInstanceInterface.hpp"
#include "strus/documentAnalyzerContextInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "private/fileCrawlerInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/analyzer/document.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/inputStream.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/thread.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "commitQueue.hpp"
#include <memory>
#include <iostream>
#include <fstream>

using namespace strus;

InsertProcessor::InsertProcessor(
		StorageClientInterface* storage_,
		const TextProcessorInterface* textproc_,
		const strus::DocumentAnalyzer* analyzerMap_,
		const analyzer::DocumentClass& defaultDocumentClass_,
		CommitQueue* commitque_,
		FileCrawlerInterface* crawler_,
		int transactionSize_,
		bool verbose_,
		ErrorBufferInterface* errorhnd_)
	:m_storage(storage_)
	,m_textproc(textproc_)
	,m_analyzerMap(analyzerMap_)
	,m_defaultDocumentClass(defaultDocumentClass_)
	,m_commitque(commitque_)
	,m_crawler(crawler_)
	,m_transaction()
	,m_transactionSize(transactionSize_)
	,m_docCount(0)
	,m_verbose(verbose_)
	,m_gotError(false)
	,m_terminated(false)
	,m_errorhnd(errorhnd_)
{}

InsertProcessor::~InsertProcessor()
{
}

void InsertProcessor::sigStop()
{
	m_terminated.set( true);
}

void InsertProcessor::processDocument( const std::string& filename)
{
	strus::InputStream input( filename);
	strus::local_ptr<strus::DocumentAnalyzerContextInterface> analyzerContext;
	strus::analyzer::DocumentClass dclass;
	if (!m_defaultDocumentClass.defined())
	{
		// Read the input file to analyze and detect its document type:
		char hdrbuf[ 4096];
		std::size_t hdrsize = input.readAhead( hdrbuf, sizeof( hdrbuf));
		if (input.error())
		{
			std::cerr << string_format( _TXT( "failed to read document file '%s': %s"), filename.c_str(), ::strerror( input.error())) << std::endl; 
			m_gotError = true;
			return;
		}
		if (!m_textproc->detectDocumentClass( dclass, hdrbuf, hdrsize, hdrsize < sizeof(hdrbuf)))
		{
			std::cerr << string_format( _TXT( "failed to detect document class of file '%s'"), filename.c_str()) << std::endl; 
			m_gotError = true;
			return;
		}
	}
	else
	{
		dclass = m_defaultDocumentClass;
	}
	const strus::DocumentAnalyzerInstanceInterface* analyzer = m_analyzerMap->get( dclass);
	if (!analyzer)
	{
		std::cerr << string_format( _TXT( "no analyzer defined for document class with MIME type '%s' schema '%s'"), dclass.mimeType().c_str(), dclass.schema().c_str()) << std::endl; 
		m_gotError = true;
		return;
	}
	analyzerContext.reset( analyzer->createContext( dclass));
	if (!analyzerContext.get()) throw strus::runtime_error( _TXT("error creating analyzer context: %s"), m_errorhnd->fetchError());

	// Analyze the document (with subdocuments) and insert it:
	enum {AnalyzerBufSize=8192};
	char buf[ AnalyzerBufSize];
	bool eof = false;

	while (!eof)
	{
		std::size_t readsize = input.read( buf, sizeof(buf));
		if (readsize < sizeof(buf))
		{
			if (input.error())
			{
				std::cerr << string_format( _TXT( "failed to read document file '%s': %s"), filename.c_str(), ::strerror( input.error())) << std::endl; 
				m_gotError = true;
				break;
			}
			eof = true;
		}
		analyzerContext->putInput( buf, readsize, eof);
		// Analyze the document and print the result:
		strus::analyzer::Document doc;
		while (!m_terminated.test() && analyzerContext->analyzeNext( doc))
		{
			// Create the document with the correct docid:
			std::vector<strus::analyzer::DocumentAttribute>::const_iterator
				oi = doc.attributes().begin(),
				oe = doc.attributes().end();
			for (;oi != oe
				&& oi->name() != strus::Constants::attribute_docid();
				++oi){}
			const char* docid = 0;
			strus::local_ptr<strus::StorageDocumentInterface> storagedoc;
			if (oi != oe)
			{
				storagedoc.reset( m_transaction->createDocument( oi->value()));
				if (!storagedoc.get()) throw strus::runtime_error( _TXT("error creating document: %s"), m_errorhnd->fetchError());
				docid = oi->value().c_str();
				//... use the docid from the analyzer if defined there
			}
			else
			{
				storagedoc.reset( m_transaction->createDocument( filename));
				if (!storagedoc.get()) throw strus::runtime_error( _TXT("error creating document"));
				storagedoc->setAttribute( strus::Constants::attribute_docid(), filename);
				docid = filename.c_str();
				//... define file path as hardcoded docid attribute
			}
			// Define all search index term occurrencies:
			std::vector<strus::analyzer::DocumentTerm>::const_iterator
				ti = doc.searchIndexTerms().begin(),
				te = doc.searchIndexTerms().end();
			for (; ti != te; ++ti)
			{
				storagedoc->addSearchIndexTerm( ti->type(), ti->value(), ti->pos());
			}
			// Define all search index structures:
			std::vector<strus::analyzer::DocumentStructure>
				structlist = doc.searchIndexStructures();
			std::vector<strus::analyzer::DocumentStructure>::const_iterator
				si = structlist.begin(), se = structlist.end();
			for (; si != se; ++si)
			{
				strus::IndexRange source( si->source().start(),si->source().end());
				strus::IndexRange sink( si->sink().start(),si->sink().end());
				storagedoc->addSearchIndexStructure( si->name(), source, sink);
			}
			// Define all forward index terms:
			std::vector<strus::analyzer::DocumentTerm>::const_iterator
				fi = doc.forwardIndexTerms().begin(),
				fe = doc.forwardIndexTerms().end();
			for (; fi != fe; ++fi)
			{
				storagedoc->addForwardIndexTerm( fi->type(), fi->value(), fi->pos());
			}
			// Define all attributes extracted from the document analysis:
			std::vector<strus::analyzer::DocumentAttribute>::const_iterator
				ai = doc.attributes().begin(), ae = doc.attributes().end();
			for (; ai != ae; ++ai)
			{
				storagedoc->setAttribute( ai->name(), ai->value());
			}
			// Define all metadata elements extracted from the document analysis:
			std::vector<strus::analyzer::DocumentMetaData>::const_iterator
				mi = doc.metadata().begin(), me = doc.metadata().end();
			for (; mi != me; ++mi)
			{
				NumericVariant val = mi->value();
				storagedoc->setMetaData( mi->name(), val);
			}
			// Finish document completed:
			storagedoc->done();
			if (m_errorhnd->hasInfo())
			{
				std::vector<std::string> info = m_errorhnd->fetchInfo();
				std::vector<std::string>::const_iterator ei = info.begin(), ee = info.end();
				for (; ei != ee; ++ei)
				{
					std::cerr << strus::string_format( _TXT( "%s in document '%s'"), ei->c_str(), docid) << std::endl;
				}
			}
			if (m_errorhnd->hasError())
			{
				const char* errmsg = m_errorhnd->fetchError();
				throw strus::runtime_error( _TXT( "error in file %s': %s"), filename.c_str(), errmsg);
			}
			m_docCount++;
			if (m_docCount == m_transactionSize && m_docCount && !m_terminated.test())
			{
				m_commitque->pushTransaction( m_transaction.get());
				m_transaction.release();
				m_transaction.reset( m_storage->createTransaction());
				if (!m_transaction.get()) throw strus::runtime_error( _TXT("error recreating storage transaction: %s"), m_errorhnd->fetchError());
				m_docCount = 0;
			}
		}
	}
	if (m_verbose)
	{
		std::cerr << "processed file '" << filename << "' (" << m_docCount << ")" << std::endl;
	}
}

void InsertProcessor::run()
{
	m_docCount = 0;
	try
	{
		std::vector<std::string> files;
		std::vector<std::string>::const_iterator fitr;
	
		m_transaction.reset( m_storage->createTransaction());
		if (!m_transaction.get()) throw strus::runtime_error( _TXT("error creating storage transaction: %s"), m_errorhnd->fetchError());

		while (!(files=m_crawler->fetch()).empty())
		{
			fitr = files.begin();
			for (int fidx=0; !m_terminated.test() && fitr != files.end(); ++fitr,++fidx)
			{
				try
				{
					processDocument( *fitr);
				}
				catch (const std::bad_alloc& err)
				{
					std::cerr << string_format( _TXT( "memory allocation error")) << std::endl;
					m_transaction.reset( m_storage->createTransaction());
					if (!m_transaction.get()) throw strus::runtime_error( _TXT("error recreating storage transaction: %s"), m_errorhnd->fetchError());
					m_docCount = 0;
					m_gotError = true;
				}
				catch (const std::runtime_error& err)
				{
					const char* errmsg = m_errorhnd->fetchError();
					if (errmsg)
					{
						std::cerr << "ERROR " << strus::string_format( "%s; %s", err.what(), errmsg) << std::endl;
					}
					else
					{
						std::cerr << "ERROR " << err.what() << std::endl;
					}
					m_transaction.reset( m_storage->createTransaction());
					if (!m_transaction.get()) throw strus::runtime_error( _TXT("error recreating storage transaction: %s"), m_errorhnd->fetchError());
					m_docCount = 0;
					m_gotError = true;
				}
			}
		}
		if (!m_terminated.test() && m_docCount)
		{
			m_commitque->pushTransaction( m_transaction.get());
			m_transaction.release();
			m_docCount = 0;
		}
	}
	catch (const std::bad_alloc& err)
	{
		std::cerr << _TXT("failed to complete inserts due to a memory allocation error") << std::endl;
		m_gotError = true;
	}
	catch (const std::runtime_error& err)
	{
		const char* errmsg = m_errorhnd->fetchError();
		if (errmsg)
		{
			std::cerr << string_format( _TXT("failed to complete inserts: %s; %s"), err.what(), errmsg) << std::endl;
		}
		else
		{
			std::cerr << string_format( _TXT("failed to complete inserts: %s"), err.what()) << std::endl;
		}
		m_gotError = true;
	}
	catch (...)
	{
		std::cerr << _TXT("failed to complete inserts: uncaught exception in thread") << std::endl; 
		m_gotError = true;
	}
	m_errorhnd->releaseContext();
}

