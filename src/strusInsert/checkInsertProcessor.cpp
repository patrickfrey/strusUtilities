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
#include "checkInsertProcessor.hpp"
#include "strus/constants.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/storageInterface.hpp"
#include "strus/arithmeticVariant.hpp"
#include "strus/fileio.hpp"
#include <boost/scoped_ptr.hpp>
#include <boost/interprocess/smart_ptr/unique_ptr.hpp>

using namespace strus;

CheckInsertProcessor::CheckInsertProcessor(
		StorageInterface* storage_,
		AnalyzerInterface* analyzer_,
		FileCrawlerInterface* crawler_,
		const std::string& logfile_)

	:m_storage(storage_)
	,m_analyzer(analyzer_)
	,m_crawler(crawler_)
	,m_terminated(false)
	,m_logfile(logfile_)
{}

CheckInsertProcessor::~CheckInsertProcessor()
{
}

void CheckInsertProcessor::sigStop()
{
	m_terminated = true;
}

template<typename T>
struct ShouldBeDefaultDeleter {
	void operator()(T *p)
	{
		delete p;
	}
};

template<typename T>
class unique_ptr
	:public boost::interprocess::unique_ptr<T,ShouldBeDefaultDeleter<T> >
{
public:
	unique_ptr( T* p)
		:boost::interprocess::unique_ptr<T,ShouldBeDefaultDeleter<T> >(p){}
};

void CheckInsertProcessor::run()
{
	Index docno;
	std::vector<std::string> files;
	std::vector<std::string>::const_iterator fitr;

	boost::scoped_ptr<strus::MetaDataReaderInterface> metadata( 
		m_storage->createMetaDataReader());

	bool hasDoclenAttribute
		= metadata->hasElement( strus::Constants::metadata_doclen());

	unsigned int filesChecked = 0;
	while (m_crawler->fetch( docno, files))
	{
		fitr = files.begin();
		for (int fidx=0; !m_terminated && fitr != files.end(); ++fitr,++fidx)
		{
			try
			{
				// Read the input file to analyze:
				std::string documentContent;
				unsigned int ec = strus::readFile( *fitr, documentContent);
				if (ec)
				{
					std::ostringstream msg;
					std::cerr << "failed to load document to analyze " << *fitr << " (file system error " << ec << ")" << std::endl;
				}
		
				// Call the analyzer and create the document:
				strus::analyzer::Document doc
					= m_analyzer->analyze( documentContent);
		
				boost::scoped_ptr<strus::StorageDocumentInterface>
					storagedoc( m_storage->createDocumentChecker( *fitr, m_logfile));

				strus::Index lastPos = (doc.searchIndexTerms().empty())
						?0:doc.searchIndexTerms()[
							doc.searchIndexTerms().size()-1].pos();

				// Define hardcoded document attributes:
				storagedoc->setAttribute(
					strus::Constants::attribute_docid(), *fitr);
		
				// Define hardcoded document metadata, if known:
				if (hasDoclenAttribute)
				{
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
						ti->type(), ti->value(), ti->pos(), 0.0/*weight*/);
				}

				// Define all forward index term occurrencies:
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
					strus::ArithmeticVariant value( mi->value());
					storagedoc->setMetaData( mi->name(), value);
				}
				storagedoc->done();
			}
			catch (const std::bad_alloc& err)
			{
				std::cerr << "failed to check document '" << *fitr << "': memory allocation error" << std::endl;
			}
			catch (const std::runtime_error& err)
			{
				std::cerr << "failed to check document '" << *fitr << "': " << err.what() << std::endl;
			}
		}
		filesChecked += files.size();
		std::cerr << "checked " << filesChecked << " documents" << std::endl;
	}
}

