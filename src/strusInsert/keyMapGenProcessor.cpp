/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "keyMapGenProcessor.hpp"
#include "strus/documentAnalyzerInstanceInterface.hpp"
#include "strus/documentAnalyzerContextInterface.hpp"
#include "strus/textProcessorInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/fileCrawlerInterface.hpp"
#include "strus/constants.hpp"
#include "strus/base/fileio.hpp"
#include "strus/base/local_ptr.hpp"
#include "strus/base/string_format.hpp"
#include "strus/base/inputStream.hpp"
#include "strus/base/thread.hpp"
#include "private/internationalization.hpp"
#include <iostream>
#include <algorithm>

using namespace strus;

static bool compareKeyMapOccurrenceFrequency( const KeyOccurrence& aa, const KeyOccurrence& bb)
{
	if (aa.frequency() > bb.frequency()) return true;
	if (aa.frequency() < bb.frequency()) return false;
	return aa.name() < bb.name();
}

void KeyMapGenResultList::push( KeyOccurrenceList& lst)
{
	strus::scoped_lock lock( m_mutex);
	m_keyOccurrenceListBuf.push_back( KeyOccurrenceList());
	m_keyOccurrenceListBuf.back().swap( lst);
}

void KeyMapGenResultList::printKeyOccurrenceList( std::ostream& out, std::size_t maxNofResults) const
{
	// Merge lists:
	std::vector<KeyOccurrenceList>::const_iterator
		ki = m_keyOccurrenceListBuf.begin(),
		ke = m_keyOccurrenceListBuf.end();

	KeyOccurrenceList result = *ki;
	for (++ki; ki != ke; ++ki)
	{
		KeyOccurrenceList prev;
		prev.swap( result);

		KeyOccurrenceList::const_iterator ai = prev.begin(), ae = prev.end();
		KeyOccurrenceList::const_iterator bi = ki->begin(), be = ki->end();

		while (ai != ae && bi != be)
		{
			if (ai->name() <= bi->name())
			{
				if (ai->name() == bi->name())
				{
					result.push_back( KeyOccurrence( ai->name(), ai->frequency() + bi->frequency()));
					++ai;
					++bi;
				}
				else
				{
					result.push_back( KeyOccurrence( ai->name(), ai->frequency()));
					++ai;
				}
			}
			else
			{
				result.push_back( KeyOccurrence( bi->name(), bi->frequency()));
				++bi;
			}
		}
	}
	// Sort result:
	std::sort( result.begin(), result.end(), compareKeyMapOccurrenceFrequency);

	// Print result:
	std::size_t ri = 0, re = maxNofResults<result.size()?maxNofResults:result.size();
	for (; ri != re; ++ri)
	{
		out << result[ ri].name() << std::endl;
	}
}

KeyMapGenProcessor::KeyMapGenProcessor(
		const TextProcessorInterface* textproc_,
		const strus::DocumentAnalyzer* analyzerMap_,
		const analyzer::DocumentClass& defaultDocumentClass_,
		KeyMapGenResultList* que_,
		FileCrawlerInterface* crawler_,
		ErrorBufferInterface* errorhnd_)

	:m_textproc(textproc_)
	,m_analyzerMap(analyzerMap_)
	,m_defaultDocumentClass(defaultDocumentClass_)
	,m_que(que_)
	,m_crawler(crawler_)
	,m_terminated(false)
	,m_errorhnd(errorhnd_)
{}

KeyMapGenProcessor::~KeyMapGenProcessor()
{
}

void KeyMapGenProcessor::sigStop()
{
	m_terminated.set( true);
}


void KeyMapGenProcessor::run()
{
	std::vector<std::string> files;
	std::vector<std::string>::const_iterator fitr;

	while (!(files=m_crawler->fetch()).empty())
	{
		try
		{
			typedef std::map<std::string,int> KeyOccurrenceMap;
			KeyOccurrenceMap keyOccurrenceMap;

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
							break;
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
					if (!analyzer)
					{
						std::cerr << string_format( _TXT( "no analyzer defined for document class with MIME type '%s' scheme '%s'"), dclass.mimeType().c_str(), dclass.scheme().c_str()) << std::endl; 
						continue;
					}
					analyzerContext.reset( analyzer->createContext( dclass));
					if (!analyzerContext.get()) throw std::runtime_error( _TXT("error creating analyzer context"));

					// Analyze the document (with subdocuments) and update the key map:
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
							// Define all search index term occurrencies:
							std::vector<strus::analyzer::DocumentTerm>::const_iterator
								ti = doc.searchIndexTerms().begin(),
								te = doc.searchIndexTerms().end();
							for (; ti != te; ++ti)
							{
								KeyOccurrenceMap::iterator
									ki = keyOccurrenceMap.find( ti->value());
								if (ki == keyOccurrenceMap.end())
								{
									keyOccurrenceMap[ ti->value()] = 1;
								}
								else
								{
									++ki->second;
								}
							}
						}
					}
				}
				catch (const std::bad_alloc& err)
				{
					std::cerr << string_format(_TXT("failed to process document '%s': memory allocation error"), fitr->c_str()) << std::endl;
				}
				catch (const std::runtime_error& err)
				{
					std::cerr << string_format(_TXT("failed to process document '%s': %s"), fitr->c_str(), err.what()) << std::endl;
				}
			}
			if (!m_terminated.test())
			{
				KeyOccurrenceList keyOccurrenceList;
				KeyOccurrenceMap::const_iterator
					ki = keyOccurrenceMap.begin(), ke = keyOccurrenceMap.end();
				for (; ki != ke; ++ki)
				{
					keyOccurrenceList.push_back( KeyOccurrence( ki->first, ki->second));
				}
				m_que->push( keyOccurrenceList);
				std::cerr << ".";
			}
		}
		catch (const std::bad_alloc&)
		{
			std::cerr << string_format(_TXT("out of memory when processing chunk of %u"), (unsigned int)files.size()) << std::endl;
		}
		catch (const std::runtime_error& err)
		{
			const char* errmsg = m_errorhnd->fetchError();
			if (errmsg)
			{
				std::cerr << string_format(_TXT("failed to process chunk of %u: %s; %s"), (unsigned int)files.size(), err.what(), errmsg) << std::endl;
			}
			else
			{
				std::cerr << string_format(_TXT("failed to process chunk of %u: %s"), (unsigned int)files.size(), err.what()) << std::endl;
			}
		}
	}
	m_errorhnd->releaseContext();
}

