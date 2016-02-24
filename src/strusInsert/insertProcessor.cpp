/*
---------------------------------------------------------------------
    The C++ library strus implements basic operations to build
    a search engine for structured search on unstructured data.

    Copyright (C) 2015 Patrick Frey

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public
    License as published by the Free Software Foundation; either
    version 3 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    General Public License for more details.

    You should have received a copy of the GNU General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

--------------------------------------------------------------------

	The latest version of strus can be found at 'http://github.com/patrickfrey/strus'
	For documentation see 'http://patrickfrey.github.com/strus'

--------------------------------------------------------------------
*/
#include "insertProcessor.hpp"
#include "strus/constants.hpp"
#include "strus/index.hpp"
#include "strus/arithmeticVariant.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/documentAnalyzerContextInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/analyzer/document.hpp"
#include "strus/private/fileio.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "private/inputStream.hpp"
#include "private/utils.hpp"
#include "fileCrawlerInterface.hpp"
#include "commitQueue.hpp"
#include <memory>
#include <fstream>

using namespace strus;

InsertProcessor::InsertProcessor(
		StorageClientInterface* storage_,
		const TextProcessorInterface* textproc_,
		const AnalyzerMap& analyzerMap_,
		CommitQueue* commitque_,
		FileCrawlerInterface* crawler_,
		unsigned int transactionSize_,
		bool verbose_,
		ErrorBufferInterface* errorhnd_)
	:m_storage(storage_)
	,m_textproc(textproc_)
	,m_analyzerMap(analyzerMap_)
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
	m_terminated = true;
}

void InsertProcessor::run()
{
	unsigned int docCount = 0;
	try
	{
		std::vector<std::string> files;
		std::vector<std::string>::const_iterator fitr;
	
		std::auto_ptr<strus::MetaDataReaderInterface> metadata( 
			m_storage->createMetaDataReader());
		if (!metadata.get()) throw strus::runtime_error(_TXT("error creating meta data reader"));

		std::auto_ptr<strus::StorageTransactionInterface>
			transaction( m_storage->createTransaction());
		if (!transaction.get()) throw strus::runtime_error(_TXT("error creating storage transaction"));

		while (m_crawler->fetch( files))
		{
			fitr = files.begin();
			for (int fidx=0; !m_terminated && fitr != files.end(); ++fitr,++fidx)
			{
				try
				{
					// Read the input file to analyze and detect its document type:
					strus::InputStream input( *fitr);
					char hdrbuf[ 1024];
					std::size_t hdrsize = input.readAhead( hdrbuf, sizeof( hdrbuf));
					strus::DocumentClass dclass;
					if (!m_textproc->detectDocumentClass( dclass, hdrbuf, hdrsize))
					{
						std::cerr << utils::string_sprintf( _TXT( "failed to detect document class of file '%s'"), fitr->c_str()) << std::endl; 
						continue;
					}
					strus::DocumentAnalyzerInterface* analyzer = m_analyzerMap.get( dclass);
					if (!analyzer)
					{
						std::cerr << utils::string_sprintf( _TXT( "no analyzer defined for document class with MIME type '%s' scheme '%s'"), dclass.mimeType().c_str(), dclass.scheme().c_str()) << std::endl; 
						continue;
					}
					std::auto_ptr<strus::DocumentAnalyzerContextInterface>
						analyzerContext( analyzer->createContext( dclass));
					if (!analyzerContext.get()) throw strus::runtime_error(_TXT("error creating analyzer context"));

					// Analyze the document (with subdocuments) and insert it:
					enum {AnalyzerBufSize=8192};
					char buf[ AnalyzerBufSize];
					bool eof = false;
	
					while (!eof)
					{
						std::size_t readsize = input.read( buf, sizeof(buf));
						eof = (readsize != sizeof(buf));
						analyzerContext->putInput( buf, readsize, eof);
	
						// Analyze the document and print the result:
						strus::analyzer::Document doc;
						while (!m_terminated && analyzerContext->analyzeNext( doc))
						{
							// Create the storage transaction document with the correct docid:
							std::vector<strus::analyzer::Attribute>::const_iterator
								oi = doc.attributes().begin(),
								oe = doc.attributes().end();
							for (;oi != oe
								&& oi->name() != strus::Constants::attribute_docid();
								++oi){}
							const char* docid = 0;
							std::auto_ptr<strus::StorageDocumentInterface> storagedoc;
							if (oi != oe)
							{
								storagedoc.reset( transaction->createDocument( oi->value()));
								if (!storagedoc.get()) throw strus::runtime_error(_TXT("error creating document structure"));
								docid = oi->value().c_str();
								//... use the docid from the analyzer if defined there
							}
							else
							{
								storagedoc.reset( transaction->createDocument( *fitr));
								if (!storagedoc.get()) throw strus::runtime_error(_TXT("error creating document structure"));
								storagedoc->setAttribute(
									strus::Constants::attribute_docid(), *fitr);
								docid = fitr->c_str();
								//... define file path as hardcoded docid attribute
							}
							unsigned int maxpos = 0;
							// Define all search index term occurrencies:
							std::vector<strus::analyzer::Term>::const_iterator
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
							std::vector<strus::analyzer::Term>::const_iterator
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
							std::vector<strus::analyzer::Attribute>::const_iterator
								ai = doc.attributes().begin(), ae = doc.attributes().end();
							for (; ai != ae; ++ai)
							{
								storagedoc->setAttribute( ai->name(), ai->value());
							}
	
							// Define all metadata elements extracted from the document analysis:
							std::vector<strus::analyzer::MetaData>::const_iterator
								mi = doc.metadata().begin(), me = doc.metadata().end();
							for (; mi != me; ++mi)
							{
								double val = mi->value();
								if (val - std::floor( val) < std::numeric_limits<float>::epsilon())
								{
									if (val < 0.0)
									{
										strus::ArithmeticVariant av( (int)(std::floor( val) + std::numeric_limits<float>::epsilon()));
										storagedoc->setMetaData( mi->name(), av);
									}
									else
									{
										strus::ArithmeticVariant av( (unsigned int)(std::floor( val) + std::numeric_limits<float>::epsilon()));
										storagedoc->setMetaData( mi->name(), av);
									}
								}
								else
								{
									storagedoc->setMetaData( mi->name(), (float) val);
								}
							}
	
							// Issue warning for documents cut because they are too big to insert:
							if (maxpos > Constants::storage_max_position_info())
							{
								std::cerr << utils::string_sprintf( _TXT( "token positions of document '%s' are out or range (document too big, %u token positions assigned)"), docid, maxpos) << std::endl;
							}
	
							// Finish document completed:
							storagedoc->done();
							docCount++;
	
							if (docCount == m_transactionSize && docCount && !m_terminated)
							{
								m_commitque->pushTransaction( transaction.get());
								transaction.release();
								transaction.reset( m_storage->createTransaction());
								if (!transaction.get()) throw strus::runtime_error(_TXT("error recreating storage transaction"));
								docCount = 0;
							}
						}
					}
					if (m_verbose)
					{
						std::cerr << "processed file '" << *fitr << "' (" << docCount << ")" << std::endl;
					}
				}
				catch (const std::bad_alloc& err)
				{
					std::cerr << utils::string_sprintf( _TXT( "failed to process document '%s': memory allocation error"), fitr->c_str()) << std::endl;
					transaction.reset( m_storage->createTransaction());
					if (!transaction.get()) throw strus::runtime_error(_TXT("error recreating storage transaction"));
					docCount = 0;
				}
				catch (const std::runtime_error& err)
				{
					const char* errmsg = m_errorhnd->fetchError();
					if (errmsg)
					{
						std::cerr << utils::string_sprintf( _TXT( "failed to process document '%s': %s; %s"), fitr->c_str(), err.what(), errmsg) << std::endl;
					}
					else
					{
						std::cerr << utils::string_sprintf( _TXT( "failed to process document '%s': %s"), fitr->c_str(), err.what()) << std::endl;
					}
					transaction.reset( m_storage->createTransaction());
					if (!transaction.get()) throw strus::runtime_error(_TXT("error recreating storage transaction"));
					docCount = 0;
				}
			}
		}
		if (!m_terminated && docCount)
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
			std::cerr << utils::string_sprintf( _TXT("failed to complete inserts: %s; %s"), err.what(), errmsg) << std::endl;
		}
		else
		{
			std::cerr << utils::string_sprintf( _TXT("failed to complete inserts: %s"), err.what()) << std::endl;
		}
	}
}

