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
#include "strus/index.hpp"
#include "strus/arithmeticVariant.hpp"
#include "strus/private/arithmeticVariantAsString.hpp"
#include "strus/documentClass.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/documentAnalyzerContextInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "strus/analyzer/document.hpp"
#include "strus/docnoRangeAllocatorInterface.hpp"
#include "strus/private/fileio.hpp"
#include "private/utils.hpp"
#include "private/inputStream.hpp"
#include "fileCrawlerInterface.hpp"
#include <cmath>
#include <limits>
#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/interprocess/smart_ptr/unique_ptr.hpp>

using namespace strus;

CheckInsertProcessor::CheckInsertProcessor(
		StorageClientInterface* storage_,
		const TextProcessorInterface* textproc_,
		const AnalyzerMap& analyzerMap_,
		FileCrawlerInterface* crawler_,
		const std::string& logfile_)

	:m_storage(storage_)
	,m_textproc(textproc_)
	,m_analyzerMap(analyzerMap_)
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
	std::vector<std::string> files;
	std::vector<std::string>::const_iterator fitr;

	boost::scoped_ptr<strus::MetaDataReaderInterface> metadata( 
		m_storage->createMetaDataReader());

	bool hasDoclenAttribute
		= metadata->hasElement( strus::Constants::metadata_doclen());

	unsigned int filesChecked = 0;
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
					std::cerr << "failed to detect document class of file '" << *fitr << "'" << std::endl; 
					continue;
				}
				strus::DocumentAnalyzerInterface* analyzer = m_analyzerMap.get( dclass);
				if (!analyzer)
				{
					std::cerr << "no analyzer defined for document class with MIME type '" << dclass.mimeType() << "' scheme '" << dclass.scheme() << "'" << std::endl; 
					continue;
				}
				std::auto_ptr<strus::DocumentAnalyzerContextInterface>
					analyzerContext( analyzer->createContext( dclass));

				// Analyze the document (with subdocuments) and check it:
				enum {AnalyzerBufSize=8192};
				char buf[ AnalyzerBufSize];
				bool eof = false;
		
				while (!eof)
				{
					std::size_t readsize = input.read( buf, sizeof(buf));
					if (!readsize)
					{
						eof = true;
						continue;
					}
					analyzerContext->putInput( buf, readsize, readsize != AnalyzerBufSize);

					// Analyze the document and print the result:
					strus::analyzer::Document doc;
					while (analyzerContext->analyzeNext( doc))
					{
						std::vector<strus::analyzer::Attribute>::const_iterator
							oi = doc.attributes().begin(),
							oe = doc.attributes().end();
						for (;oi != oe
							&& oi->name() != strus::Constants::attribute_docid();
							++oi){}
						const char* docid = 0;
						boost::scoped_ptr<strus::StorageDocumentInterface> storagedoc;
						if (oi != oe)
						{
							storagedoc.reset(
								m_storage->createDocumentChecker(
									oi->value(), m_logfile));
							docid = oi->value().c_str();
							//... use the docid from the analyzer if defined there
						}
						else
						{
							storagedoc.reset(
								m_storage->createDocumentChecker(
									*fitr, m_logfile));
							storagedoc->setAttribute(
								strus::Constants::attribute_docid(), *fitr);
							docid = fitr->c_str();
							//... define file path as hardcoded docid attribute
						}
		
						// Define hardcoded document attributes:
						storagedoc->setAttribute(
							strus::Constants::attribute_docid(), *fitr);
				
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
		
						// Define all forward index term occurrencies:
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
						storagedoc->done();
					}
				}
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

