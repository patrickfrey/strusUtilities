/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "checkInsertProcessor.hpp"
#include "strus/constants.hpp"
#include "strus/index.hpp"
#include "strus/numericVariant.hpp"
#include "strus/analyzer/documentClass.hpp"
#include "strus/documentAnalyzerInterface.hpp"
#include "strus/documentAnalyzerContextInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/storageClientInterface.hpp"
#include "strus/storageTransactionInterface.hpp"
#include "strus/metaDataReaderInterface.hpp"
#include "strus/analyzer/document.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/inputStream.hpp"
#include "private/utils.hpp"
#include "private/errorUtils.hpp"
#include "private/internationalization.hpp"
#include "fileCrawlerInterface.hpp"
#include <cmath>
#include <limits>
#include <boost/thread.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/interprocess/smart_ptr/unique_ptr.hpp>
#include <stdarg.h>

using namespace strus;

CheckInsertProcessor::CheckInsertProcessor(
		StorageClientInterface* storage_,
		const TextProcessorInterface* textproc_,
		const AnalyzerMap* analyzerMap_,
		FileCrawlerInterface* crawler_,
		const std::string& logfile_,
		ErrorBufferInterface* errorhnd_)

	:m_storage(storage_)
	,m_textproc(textproc_)
	,m_analyzerMap(analyzerMap_)
	,m_crawler(crawler_)
	,m_terminated(false)
	,m_logfile(logfile_)
	,m_errorhnd(errorhnd_)
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
	try
	{
		std::vector<std::string> files;
		std::vector<std::string>::const_iterator fitr;
	
		boost::scoped_ptr<strus::MetaDataReaderInterface> metadata( 
			m_storage->createMetaDataReader());
		if (!metadata.get()) throw strus::runtime_error(_TXT("error creating meta data reader"));
	
		// Evaluate the expected types of the meta data elements to make them comparable
		std::vector<strus::NumericVariant::Type> metadatatype;
		strus::Index mi=0, me=metadata->nofElements();
		for (; mi != me; ++mi)
		{
			const char* tp = metadata->getType( mi);
			if ((tp[0]|32) == 'i')
			{
				metadatatype.push_back( strus::NumericVariant::Int);
			}
			else if ((tp[0]|32) == 'u')
			{
				metadatatype.push_back( strus::NumericVariant::UInt);
			}
			else if ((tp[0]|32) == 'f')
			{
				metadatatype.push_back( strus::NumericVariant::Float);
			}
			else
			{
				metadatatype.push_back( strus::NumericVariant::Null);
			}
		}
	
		unsigned int filesChecked = 0;
		while (m_crawler->fetch( files))
		{
			fitr = files.begin();
			for (int fidx=0; !m_terminated && fitr != files.end(); ++fitr,++fidx)
			{
				try
				{
					strus::InputStream input( *fitr);
					std::auto_ptr<strus::DocumentAnalyzerContextInterface> analyzerContext;
					if (!m_analyzerMap->documentClass().defined())
					{
						// Read the input file to analyze and detect its document type:
						char hdrbuf[ 1024];
						std::size_t hdrsize = input.readAhead( hdrbuf, sizeof( hdrbuf));
						if (input.error())
						{
							std::cerr << string_format( _TXT( "failed to read document file '%s': %s"), fitr->c_str(), ::strerror( input.error())) << std::endl; 
							continue;
						}
						strus::analyzer::DocumentClass dclass;
						if (!m_textproc->detectDocumentClass( dclass, hdrbuf, hdrsize))
						{
							std::cerr << string_format( _TXT( "failed to detect document class of file '%s'"), fitr->c_str()) << std::endl; 
							continue;
						}
						const strus::DocumentAnalyzerInterface* analyzer = m_analyzerMap->get( dclass);
						if (!analyzer)
						{
							std::cerr << string_format( _TXT( "no analyzer defined for document class with MIME type '%s' scheme '%s'"), dclass.mimeType().c_str(), dclass.scheme().c_str()) << std::endl; 
							continue;
						}
						analyzerContext.reset( analyzer->createContext( dclass));
					}
					else
					{
						const strus::DocumentAnalyzerInterface* analyzer = m_analyzerMap->get( m_analyzerMap->documentClass());
						if (!analyzer)
						{
							std::cerr << string_format( _TXT( "no analyzer defined for document class with MIME type '%s' scheme '%s'"), m_analyzerMap->documentClass().mimeType().c_str(), m_analyzerMap->documentClass().scheme().c_str()) << std::endl; 
							continue;
						}
						analyzerContext.reset( analyzer->createContext( m_analyzerMap->documentClass()));
					}
					if (!analyzerContext.get()) throw strus::runtime_error(_TXT("error creating analyzer context"));
	
					// Analyze the document (with subdocuments) and check it:
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
								if (!storagedoc.get()) throw strus::runtime_error(_TXT("error creating document checker"));
								docid = oi->value().c_str();
								//... use the docid from the analyzer if defined there
							}
							else
							{
								storagedoc.reset(
									m_storage->createDocumentChecker(
										*fitr, m_logfile));
								if (!storagedoc.get()) throw strus::runtime_error(_TXT("error creating document checker"));
								storagedoc->setAttribute(
									strus::Constants::attribute_docid(), *fitr);
								docid = fitr->c_str();
								//... define file path as hardcoded docid attribute
							}
			
							// Define hardcoded document attributes:
							storagedoc->setAttribute(
								strus::Constants::attribute_docid(), *fitr);
					
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
								Index midx = metadata->elementHandle( mi->name());
								if (midx < 0)
								{
									std::cerr << string_format( _TXT( "unknown meta data element '%s'"), mi->name().c_str()) << std::endl;
								}
								else switch (metadatatype[midx])
								{
									case strus::NumericVariant::Int:
										if (val - std::floor( val) < std::numeric_limits<float>::epsilon())
										{
											strus::NumericVariant av( (int)(std::floor( val) + std::numeric_limits<float>::epsilon()));
											storagedoc->setMetaData( mi->name(), av);
										}
										else
										{
											std::cerr << string_format( _TXT( "meta data assignment is not convertible to the type expected: (%s) %.4f"), "int", val) << std::endl;
										}
										break;
									case strus::NumericVariant::UInt:
										if (val - std::floor( val) < std::numeric_limits<float>::epsilon()
										|| (val + std::numeric_limits<float>::epsilon()) < 0.0)
										{
											strus::NumericVariant av( (unsigned int)(std::floor( val) + std::numeric_limits<float>::epsilon()));
											storagedoc->setMetaData( mi->name(), av);
										}
										else
										{
											std::cerr << string_format( _TXT( "meta data assignment is not convertible to the type expected: (%s) %.4f"), "unsigned int", val) << std::endl;
										}
										break;
									case strus::NumericVariant::Float:
									case strus::NumericVariant::Null:
										storagedoc->setMetaData( mi->name(), (float) val);
										break;
								}
							}
							// Issue warning for documents cut because they are too big to insert:
							if (maxpos > Constants::storage_max_position_info())
							{
								std::cerr << string_format( _TXT( "token positions of document '%s' are out or range (document too big, %u token positions assigned)"), docid, maxpos) << std::endl;
							}
							storagedoc->done();
						}
					}
				}
				catch (const std::bad_alloc& err)
				{
					std::cerr << string_format( _TXT( "failed to check document '%s': memory allocation error"), fitr->c_str()) << std::endl;
				}
				catch (const std::runtime_error& err)
				{
					const char* errmsg = m_errorhnd->fetchError();
					if (errmsg)
					{
						std::cerr << string_format( _TXT( "failed to check document '%s': %s; %s"), fitr->c_str(), err.what(), errmsg) << std::endl;
					}
					else
					{
						std::cerr << string_format( _TXT( "failed to check document '%s': %s"), fitr->c_str(), err.what()) << std::endl;
					}
				}
			}
			filesChecked += files.size();
			std::cerr << string_format( (filesChecked == 1)
					?_TXT( "\rchecked %u file"):_TXT( "\rchecked %u files"),
					filesChecked) << std::endl;
		}
	}
	catch (const boost::thread_interrupted&)
	{
		std::cerr << _TXT("failed to complete check inserts: thread interrupted") << std::endl; 
	}
	catch (const std::bad_alloc& err)
	{
		std::cerr << _TXT("failed to complete check inserts: memory allocation error") << std::endl; 
	}
	catch (const std::runtime_error& err)
	{
		std::cerr << string_format( _TXT( "failed to check documents: %s"), err.what()) << std::endl;
	}
	m_errorhnd->releaseContext();
}


