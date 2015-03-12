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
#include "strus/documentAnalyzerInstanceInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "strus/analyzer/document.hpp"
#include "strus/private/arithmeticVariantAsString.hpp"
#include "strus/private/fileio.hpp"
#include "docnoAllocatorInterface.hpp"
#include "fileCrawlerInterface.hpp"
#include "commitQueue.hpp"
#include <memory>
#include <fstream>

using namespace strus;

InsertProcessor::InsertProcessor(
		StorageClientInterface* storage_,
		DocumentAnalyzerInterface* analyzer_,
		DocnoAllocatorInterface* docnoAllocator_,
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

static strus::DocumentAnalyzerInstanceInterface*
	createAnalyzerInstance(
		const DocumentAnalyzerInterface* analyzer,
		const std::string& docpath)
{
	if (docpath == "-")
	{
		return analyzer->createDocumentAnalyzerInstance( std::cin);
	}
	else
	{
		std::ifstream documentFile;
		try 
		{
			documentFile.open( docpath.c_str(), std::fstream::in | std::ios::binary);
		}
		catch (const std::ifstream::failure& err)
		{
			throw std::runtime_error( std::string( "failed to read file to insert '") + docpath + "': " + err.what());
		}
		catch (const std::runtime_error& err)
		{
			throw std::runtime_error( std::string( "failed to read file to insert '") + docpath + "': " + err.what());
		}
		if(!documentFile)
		{
			throw std::runtime_error( std::string( "failed to read file to insert '") + docpath + "'");
		}
		return analyzer->createDocumentAnalyzerInstance( documentFile);
	}
}

void InsertProcessor::run()
{
	Index docno;
	std::vector<std::string> files;
	std::vector<std::string>::const_iterator fitr;

	std::auto_ptr<strus::MetaDataReaderInterface> metadata( 
		m_storage->createMetaDataReader());

	bool hasDoclenAttribute
		= metadata->hasElement( strus::Constants::metadata_doclen());

	while (m_crawler->fetch( files))
	{
		docno = m_docnoAllocator?m_docnoAllocator->allocDocnoRange( files.size()):0;

		std::auto_ptr<strus::StorageTransactionInterface>
			transaction( m_storage->createTransaction());
		unsigned int docCount = 0;

		fitr = files.begin();
		for (int fidx=0; !m_terminated && fitr != files.end(); ++fitr,++fidx)
		{
			try
			{
				std::auto_ptr<strus::DocumentAnalyzerInstanceInterface>
					analyzerInstance(
						createAnalyzerInstance( m_analyzer, *fitr));

				while (analyzerInstance->hasMore())
				{
					while (analyzerInstance->hasMore()
						&& docCount < m_transactionSize)
					{
						// Analyze the next sub document:
						strus::analyzer::Document
							doc = analyzerInstance->analyzeNext();
	
						// Create the storage transaction document with the correct docid:
						std::vector<strus::analyzer::Attribute>::const_iterator
							oi = doc.attributes().begin(),
							oe = doc.attributes().end();
						for (;oi != oe
							&& oi->name() != strus::Constants::attribute_docid();
							++oi){}
						std::auto_ptr<strus::StorageDocumentInterface> storagedoc;
						if (oi != oe)
						{
							storagedoc.reset(
								transaction->createDocument(
									oi->value(), docno?(docno + fidx):0));
							//... use the docid from the analyzer if defined there
						}
						else
						{
							storagedoc.reset(
								transaction->createDocument(
									*fitr, docno?(docno + fidx):0));
							storagedoc->setAttribute(
								strus::Constants::attribute_docid(), *fitr);
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
						// Define all search index term occurrencies:
						std::vector<strus::analyzer::Term>::const_iterator
							ti = doc.searchIndexTerms().begin(),
							te = doc.searchIndexTerms().end();
						for (; ti != te; ++ti)
						{
							storagedoc->addSearchIndexTerm(
								ti->type(), ti->value(), ti->pos());
						}
	
						// Define all forward index terms:
						std::vector<strus::analyzer::Term>::const_iterator
							fi = doc.forwardIndexTerms().begin(),
							fe = doc.forwardIndexTerms().end();
						for (; fi != fe; ++fi)
						{
							storagedoc->addForwardIndexTerm(
								fi->type(), fi->value(), fi->pos());
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
							strus::ArithmeticVariant value(
								strus::arithmeticVariantFromString( mi->value()));
							storagedoc->setMetaData( mi->name(), value);
						}
	
						// Finish document completed:
						storagedoc->done();
						docCount++;
					}
					if (docCount == m_transactionSize)
					{
						if (!m_terminated)
						{
							m_commitque->push( transaction.release(), docno, docCount);
							transaction.reset( m_storage->createTransaction());
							docCount = 0;
						}
					}
				}
			}
			catch (const std::bad_alloc& err)
			{
				std::cerr << "failed to process document '" << *fitr << "': memory allocation error" << std::endl;
			}
			catch (const std::runtime_error& err)
			{
				std::cerr << "failed to process document '" << *fitr << "': " << err.what() << std::endl;
			}
		}
		if (!m_terminated)
		{
			m_commitque->push( transaction.release(), docno, docCount);
		}
	}
}

