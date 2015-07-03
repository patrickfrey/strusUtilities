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
#include "strus/docnoRangeAllocatorInterface.hpp"
#include "strus/analyzer/document.hpp"
#include "strus/private/arithmeticVariantAsString.hpp"
#include "strus/private/fileio.hpp"
#include "private/inputStream.hpp"
#include "fileCrawlerInterface.hpp"
#include "commitQueue.hpp"
#include <memory>
#include <fstream>

using namespace strus;

InsertProcessor::InsertProcessor(
		StorageClientInterface* storage_,
		DocumentAnalyzerInterface* analyzer_,
		DocnoRangeAllocatorInterface* docnoAllocator_,
		CommitQueue* commitque_,
		FileCrawlerInterface* crawler_,
		unsigned int transactionSize_)

	:m_storage(storage_)
	,m_analyzer(analyzer_)
	,m_docnoAllocator(docnoAllocator_)
	,m_commitque(commitque_)
	,m_crawler(crawler_)
	,m_transactionSize(transactionSize_)
	,m_terminated(false)
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
	Index docnoRangeStart = 0;
	unsigned int docCount = 0;
	try
	{
		std::vector<std::string> files;
		std::vector<std::string>::const_iterator fitr;
	
		std::auto_ptr<strus::MetaDataReaderInterface> metadata( 
			m_storage->createMetaDataReader());
	
		bool hasDoclenAttribute
			= metadata->hasElement( strus::Constants::metadata_doclen());
	
		std::auto_ptr<strus::StorageTransactionInterface>
			transaction( m_storage->createTransaction());
	
		while (m_crawler->fetch( files))
		{
			fitr = files.begin();
			for (int fidx=0; !m_terminated && fitr != files.end(); ++fitr,++fidx)
			{
				try
				{
					strus::InputStream input( *fitr);
					std::auto_ptr<strus::DocumentAnalyzerContextInterface>
						analyzerContext( m_analyzer->createContext());
	
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
							if (m_docnoAllocator && !docnoRangeStart)
							{
								docnoRangeStart = m_docnoAllocator->allocDocnoRange( m_transactionSize);
								m_commitque->pushTransactionPromise( docnoRangeStart);
							}
							Index docno = (docnoRangeStart)?(docnoRangeStart + docCount):0;
							if (oi != oe)
							{
								storagedoc.reset(
									transaction->createDocument(
										oi->value(), docno));
								docid = oi->value().c_str();
								//... use the docid from the analyzer if defined there
							}
							else
							{
								storagedoc.reset(
									transaction->createDocument(
										*fitr, docno));
								storagedoc->setAttribute(
									strus::Constants::attribute_docid(), *fitr);
								docid = fitr->c_str();
								//... define file path as hardcoded docid attribute
							}
	
							// Define hardcoded document metadata, if known:
							if (hasDoclenAttribute)
							{
								strus::Index lastPos = (doc.searchIndexTerms().empty())
										?0:doc.searchIndexTerms()[ 
											doc.searchIndexTerms().size()-1].pos();
								storagedoc->setMetaData(
									strus::Constants::metadata_doclen(),
									strus::ArithmeticVariant( lastPos));
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
								std::cerr << "token positions of document '" << docid << "' are out or range (document too big, " << maxpos << " token positions assigned)" << std::endl;
							}
	
							// Finish document completed:
							storagedoc->done();
							docCount++;
	
							if (docCount == m_transactionSize && docCount && !m_terminated)
							{
								m_commitque->pushTransaction( transaction.release(), docnoRangeStart, docCount, docCount);
								transaction.reset( m_storage->createTransaction());
								docCount = 0;
								docnoRangeStart = 0;
							}
						}
					}
				}
				catch (const std::bad_alloc& err)
				{
					std::cerr << "failed to process document '" << *fitr << "': memory allocation error" << std::endl;
					if (docnoRangeStart) m_commitque->breachTransactionPromise( docnoRangeStart);
					docnoRangeStart = 0;
					docCount = 0;
				}
				catch (const std::runtime_error& err)
				{
					std::cerr << "failed to process document '" << *fitr << "': " << err.what() << std::endl;
					if (docnoRangeStart) m_commitque->breachTransactionPromise( docnoRangeStart);
					docnoRangeStart = 0;
					docCount = 0;
				}
			}
		}
		if (!m_terminated && docCount)
		{
			Index docCountAllocated = m_transactionSize;
			if (docnoRangeStart && docCount < m_transactionSize && m_docnoAllocator)
			{
				if (m_docnoAllocator->deallocDocnoRange(
					docnoRangeStart + docCount, m_transactionSize - docCount))
				{
					docCountAllocated -= (m_transactionSize - docCount);
				}
			}
			m_commitque->pushTransaction( transaction.release(), docnoRangeStart, docCount, docCountAllocated);
			docCount = 0;
			docnoRangeStart = 0;
		}
	}
	catch (const std::bad_alloc& err)
	{
		std::cerr << "failed to complete inserts due to a memory allocation error" << std::endl;
		if (docnoRangeStart) m_commitque->breachTransactionPromise( docnoRangeStart);
		docnoRangeStart = 0;
	}
	catch (const std::runtime_error& err)
	{
		std::cerr << "failed to complete inserts: " << err.what() << std::endl;
		if (docnoRangeStart) m_commitque->breachTransactionPromise( docnoRangeStart);
		docnoRangeStart = 0;
	}
}

