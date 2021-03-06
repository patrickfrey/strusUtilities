/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "checkInsertProcessor.hpp"
#include "strus/constants.hpp"
#include "strus/storage/index.hpp"
#include "strus/numericVariant.hpp"
#include "strus/analyzer/documentClass.hpp"
#include "strus/documentAnalyzerInstanceInterface.hpp"
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
#include "strus/base/local_ptr.hpp"
#include "strus/base/shared_ptr.hpp"
#include "strus/base/thread.hpp"
#include "strus/base/math.hpp"
#include "strus/fileCrawlerInterface.hpp"
#include "private/errorUtils.hpp"
#include "private/documentAnalyzer.hpp"
#include "private/internationalization.hpp"
#include <limits>
#include <iostream>
#include <stdarg.h>

using namespace strus;

CheckInsertProcessor::CheckInsertProcessor(
		StorageClientInterface* storage_,
		const TextProcessorInterface* textproc_,
		const strus::DocumentAnalyzer* analyzerMap_,
		const analyzer::DocumentClass& defaultDocumentClass_,
		FileCrawlerInterface* crawler_,
		const std::string& logfile_,
		ErrorBufferInterface* errorhnd_)

	:m_storage(storage_)
	,m_textproc(textproc_)
	,m_analyzerMap(analyzerMap_)
	,m_defaultDocumentClass(defaultDocumentClass_)
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
	m_terminated.set( true);
}

void CheckInsertProcessor::run()
{
	try
	{
		std::vector<std::string> files;
		std::vector<std::string>::const_iterator fitr;

		strus::local_ptr<strus::MetaDataReaderInterface> metadata( m_storage->createMetaDataReader());
		if (!metadata.get()) throw std::runtime_error( _TXT("error creating meta data reader"));
	
		// Evaluate the expected types of the meta data elements to make them comparable
		std::vector<strus::NumericVariant::Type> metadatatype;
		{
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
		}
		unsigned int filesChecked = 0;
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
					const strus::DocumentAnalyzerInstanceInterface* analyzer = m_analyzerMap->get( dclass);
					if (analyzer)
					{
						analyzerContext.reset( analyzer->createContext( dclass));
					}
					else
					{
						std::cerr << string_format( _TXT( "no analyzer defined for document class with MIME type '%s' schema '%s'"), dclass.mimeType().c_str(), dclass.schema().c_str()) << std::endl; 
						continue;
					}
					if (!analyzerContext.get()) throw std::runtime_error( _TXT("error creating analyzer context"));

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
								storagedoc.reset(
									m_storage->createDocumentChecker(
										oi->value(), m_logfile));
								if (!storagedoc.get()) throw std::runtime_error( _TXT("error creating document checker"));
								docid = oi->value().c_str();
								//... use the docid from the analyzer if defined there
							}
							else
							{
								storagedoc.reset(
									m_storage->createDocumentChecker(
										*fitr, m_logfile));
								if (!storagedoc.get()) throw std::runtime_error( _TXT("error creating document checker"));
								storagedoc->setAttribute(
									strus::Constants::attribute_docid(), *fitr);
								docid = fitr->c_str();
								//... define file path as hardcoded docid attribute
							}
			
							// Define hardcoded document attributes:
							storagedoc->setAttribute(
								strus::Constants::attribute_docid(), *fitr);
					
							int maxpos = 0;
		
							// Define all search index term occurrencies:
							std::vector<strus::analyzer::DocumentTerm>::const_iterator
								ti = doc.searchIndexTerms().begin(),
								te = doc.searchIndexTerms().end();
							for (; ti != te; ++ti)
							{
								if (ti->pos() > (int)Constants::storage_max_position_info())
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
							std::vector<strus::analyzer::DocumentTerm>::const_iterator
								fi = doc.forwardIndexTerms().begin(),
								fe = doc.forwardIndexTerms().end();
							for (; fi != fe; ++fi)
							{
								if (fi->pos() > (int)Constants::storage_max_position_info())
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
								double val = mi->value();
								Index midx = metadata->elementHandle( mi->name());
								if (midx < 0)
								{
									std::cerr << string_format( _TXT( "unknown meta data element '%s'"), mi->name().c_str()) << std::endl;
								}
								else switch (metadatatype[midx])
								{
									case strus::NumericVariant::Int:
										if (val - strus::Math::floor( val) < std::numeric_limits<float>::epsilon())
										{
											if (val < 0.0)
											{
												strus::NumericVariant av( (int64_t)(strus::Math::floor( val - std::numeric_limits<float>::epsilon())));
												storagedoc->setMetaData( mi->name(), av);
											}
											else
											{
												strus::NumericVariant av( (int64_t)(strus::Math::floor( val + std::numeric_limits<float>::epsilon())));
												storagedoc->setMetaData( mi->name(), av);
											}
										}
										else
										{
											std::cerr << string_format( _TXT( "meta data assignment is not convertible to the type expected: (%s) %.4f"), "int", val) << std::endl;
										}
										break;
									case strus::NumericVariant::UInt:
										if (val - strus::Math::floor( val) < std::numeric_limits<float>::epsilon()
										|| (val + std::numeric_limits<float>::epsilon()) > 0.0)
										{
											strus::NumericVariant av( (uint64_t)(strus::Math::floor( val + std::numeric_limits<float>::epsilon())));
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
							if (maxpos > (int)Constants::storage_max_position_info())
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
	catch (const std::bad_alloc& err)
	{
		std::cerr << _TXT("failed to complete check inserts: memory allocation error") << std::endl; 
	}
	catch (const std::runtime_error& err)
	{
		std::cerr << string_format( _TXT( "failed to check documents: %s"), err.what()) << std::endl;
	}
	catch (...)
	{
		std::cerr << _TXT("failed to complete check inserts: unexpected exception") << std::endl; 
	}
	m_errorhnd->releaseContext();
}


