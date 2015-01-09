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
#include "keyMapGenProcessor.hpp"
#include "strus/constants.hpp"
#include "strus/utils/fileio.hpp"
#include <boost/scoped_ptr.hpp>
#include <boost/interprocess/smart_ptr/unique_ptr.hpp>

using namespace strus;

static bool compareKeyMapOccurrenceFrequency( const KeyOccurrence& aa, const KeyOccurrence& bb)
{
	if (aa.frequency() < bb.frequency()) return true;
	if (aa.frequency() > bb.frequency()) return false;
	return aa.name() < bb.name();
}

void KeyMapQueue::push( KeyOccurrenceList& lst)
{
	boost::mutex::scoped_lock( m_mutex);
	m_keyOccurrenceListBuf.push_back( KeyOccurrenceList());
	m_keyOccurrenceListBuf.back().swap( lst);
}

void KeyMapQueue::printKeyOccurrenceList( std::ostream& out, std::size_t maxNofResults)
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
		AnalyzerInterface* analyzer_,
		KeyMapQueue* que_,
		FileCrawlerInterface* crawler_)

	:m_analyzer(analyzer_)
	,m_que(que_)
	,m_crawler(crawler_)
	,m_terminated(false)
{}

KeyMapGenProcessor::~KeyMapGenProcessor()
{
}

void KeyMapGenProcessor::sigStop()
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

void KeyMapGenProcessor::run()
{
	Index docno;
	std::vector<std::string> files;
	std::vector<std::string>::const_iterator fitr;

	while (m_crawler->fetch( docno, files))
	{
		try
		{
			KeyOccurrenceList keyOccurrenceList;

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
			
					// Define all search index term occurrencies:
					std::vector<strus::analyzer::Term>::const_iterator
						ti = doc.searchIndexTerms().begin(),
						te = doc.searchIndexTerms().end();
					for (; ti != te; ++ti)
					{
						keyOccurrenceList.push_back( KeyOccurrence( ti->value()));
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
				m_que->push( keyOccurrenceList);
			}
		}
		catch (const std::bad_alloc& err)
		{
			std::cerr << "out of memory when processing chunk of " << files.size() << " documents: " << err.what() << std::endl;
		}
		catch (const std::runtime_error& err)
		{
			std::cerr << "failed to process chunk of " << files.size() << " documents: " << err.what() << std::endl;
		}
	}
}

