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
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/documentAnalyzerContextInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/analyzer/document.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/inputStream.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/thread.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "fileCrawlerInterface.hpp"
#include "commitQueue.hpp"
#include <memory>
#include <iostream>
#include <fstream>

using namespace strus;

InsertProcessor::InsertProcessor(
		StorageClientInterface* storage_,
		const TextProcessorInterface* textproc_,
		const AnalyzerMap* analyzerMap_,
		const analyzer::DocumentClass& defaultDocumentClass_,
		CommitQueue* commitque_,
		FileCrawlerInterface* crawler_,
		unsigned int transactionSize_,
		bool verbose_,
		ErrorBufferInterface* errorhnd_)
	:m_storage(storage_)
	,m_textproc(textproc_)
	,m_analyzerMap(analyzerMap_)
	,m_defaultDocumentClass(defaultDocumentClass_)
	,m_commitque(commitque_)
	,m_crawler(crawler_)
	,m_transactionSize(transactionSize_)
	,m_verbose(verbose_)
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

void InsertProcessor::run()
{
	unsigned int docCount = 0;
	try
	{
		std::vector<std::string> files;
		std::vector<std::string>::const_iterator fitr;
	
		strus::local_ptr<strus::StorageTransactionInterface>
			transaction( m_storage->createTransaction());
		if (!transaction.get()) throw strus::runtime_error( "%s", _TXT("error creating storage transaction"));

		while (!(files=m_crawler->fetch()).empty())
		{
			fitr = files.begin();
			for (int fidx=0; !m_terminated.test() && fitr != files.end(); ++fitr,++fidx)
			{
				try
				{
					strus::InputStream input( *fitr);
					strus::local_ptr<strus::DocumentAnalyzerContextInterface> analyzerContext;
					strus::analyzer::DocumentClass dclass;
					if (!m_defaultDocumentClass.defined())
					{
						// Read the input file to analyze and detect its document type:
						char hdrbuf[ 4096];
						std::size_t hdrsize = input.readAhead( hdrbuf, sizeof( hdrbuf));
						if (input.error())
						{
							std::cerr << string_format( _TXT( "failed to read document file '%s': %s"), fitr->c_str(), ::strerror( input.error())) << std::endl; 
							continue;
						}
						if (!m_textproc->detectDocumentClass( dclass, hdrbuf, hdrsize, hdrsize < sizeof(hdrbuf)))
						{
							std::cerr << string_format( _TXT( "failed to detect document class of file '%s'"), fitr->c_str()) << std::endl; 
							continue;
						}
					}
					else
					{
						dclass = m_defaultDocumentClass;
					}
					const strus::DocumentAnalyzerInterface* analyzer = m_analyzerMap->get( dclass);
					if (!analyzer)
					{
						std::cerr << string_format( _TXT( "no analyzer defined for document class with MIME type '%s' scheme '%s'"), dclass.mimeType().c_str(), dclass.scheme().c_str()) << std::endl; 
						continue;
					}
					analyzerContext.reset( analyzer->createContext( dclass));
					if (!analyzerContext.get()) throw strus::runtime_error( "%s", _TXT("error creating analyzer context"));

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
								std::cerr << string_format( _TXT( "failed to read document file '%s': %s"), fitr->c_str(), ::strerror( input.error())) << std::endl; 
								break;
							}
							eof = true;
						}
						analyzerContext->putInput( buf, readsize, eof);
						// Analyze the document and print the result:
						strus::analyzer::Document doc;
						while (!m_terminated.test() && analyzerContext->analyzeNext( doc))
						{
							// Create the storage transaction document with the correct docid:
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
								storagedoc.reset( transaction->createDocument( oi->value()));
								if (!storagedoc.get()) throw strus::runtime_error( "%s", _TXT("error creating document structure"));
								docid = oi->value().c_str();
								//... use the docid from the analyzer if defined there
							}
							else
							{
								storagedoc.reset( transaction->createDocument( *fitr));
								if (!storagedoc.get()) throw strus::runtime_error( "%s", _TXT("error creating document structure"));
								storagedoc->setAttribute(
									strus::Constants::attribute_docid(), *fitr);
								docid = fitr->c_str();
								//... define file path as hardcoded docid attribute
							}
							unsigned int maxpos = 0;
							// Define all search index term occurrencies:
							std::vector<strus::analyzer::DocumentTerm>::const_iterator
								ti = doc.searchIndexTerms().begin(),
								te = doc.searchIndexTerms().end();
							for (; ti != te; ++ti)
							{
								if (ti->pos() > Constants::storage_max_position_info())
								{
									// Cut positions away that are out of range.
									//	Issue a warning later:
									if (ti->pos() > maxpos)
									{
										maxpos = ti->pos();
									}
								}
								else
								{
									storagedoc->addSearchIndexTerm(
										ti->type(), ti->value(), ti->pos());
								}
							}
		
							// Define all forward index terms:
							std::vector<strus::analyzer::DocumentTerm>::const_iterator
								fi = doc.forwardIndexTerms().begin(),
								fe = doc.forwardIndexTerms().end();
							for (; fi != fe; ++fi)
							{
								if (fi->pos() > Constants::storage_max_position_info())
								{
									// Cut positions away that are out of range. Issue a warning later:
									if (fi->pos() > maxpos)
									{
										maxpos = fi->pos();
									}
								}
								else
								{
									storagedoc->addForwardIndexTerm(
										fi->type(), fi->value(), fi->pos());
								}
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
							// Issue warning for documents cut because they are too big to insert:
							if (maxpos > Constants::storage_max_position_info())
							{
								std::cerr << string_format( _TXT( "token positions of document '%s' are out or range (document too big, %u token positions assigned)"), docid, maxpos) << std::endl;
							}
	
							// Finish document completed:
							storagedoc->done();
							docCount++;

							if (docCount == m_transactionSize && docCount && !m_terminated.test())
							{
								m_commitque->pushTransaction( transaction.get());
								transaction.release();
								transaction.reset( m_storage->createTransaction());
								if (!transaction.get()) throw strus::runtime_error( "%s", _TXT("error recreating storage transaction"));
								docCount = 0;
							}
						}
						if (m_errorhnd->hasError())
						{
							const char* errmsg = m_errorhnd->fetchError();
							throw strus::runtime_error( _TXT( "failed to process document '%s': %s"), fitr->c_str(), errmsg);
						}
					}
					if (m_verbose)
					{
						std::cerr << "processed file '" << *fitr << "' (" << docCount << ")" << std::endl;
					}
				}
				catch (const std::bad_alloc& err)
				{
					std::cerr << string_format( _TXT( "failed to process document '%s': memory allocation error"), fitr->c_str()) << std::endl;
					transaction.reset( m_storage->createTransaction());
					if (!transaction.get()) throw strus::runtime_error( "%s", _TXT("error recreating storage transaction"));
					docCount = 0;
				}
				catch (const std::runtime_error& err)
				{
					const char* errmsg = m_errorhnd->fetchError();
					if (errmsg)
					{
						std::cerr << string_format( _TXT( "failed to process document '%s': %s; %s"), fitr->c_str(), err.what(), errmsg) << std::endl;
					}
					else
					{
						std::cerr << string_format( _TXT( "failed to process document '%s': %s"), fitr->c_str(), err.what()) << std::endl;
					}
					transaction.reset( m_storage->createTransaction());
					if (!transaction.get()) throw strus::runtime_error( "%s", _TXT("error recreating storage transaction"));
					docCount = 0;
				}
			}
		}
		if (!m_terminated.test() && docCount)
		{
			m_commitque->pushTransaction( transaction.get());
			transaction.release();
			docCount = 0;
		}
	}
	catch (const std::bad_alloc& err)
	{
		std::cerr << _TXT("failed to complete inserts due to a memory allocation error") << std::endl;
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
	}
	catch (...)
	{
		std::cerr << _TXT("failed to complete inserts: uncaught exception in thread") << std::endl; 
	}
	m_errorhnd->releaseContext();
}

