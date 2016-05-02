/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#ifndef _STRUS_UTILITIES_TRACE_UTILS_HPP_INCLUDED
#define _STRUS_UTILITIES_TRACE_UTILS_HPP_INCLUDED
#include <string>

namespace strus
{
/// \brief Forward declaration
class TraceObjectBuilderInterface;
/// \brief Forward declaration
class StorageObjectBuilderInterface;
/// \brief Forward declaration
class AnalyzerObjectBuilderInterface;
/// \brief Forward declaration
class ModuleLoaderInterface;
/// \brief Forward declaration
class ErrorBufferInterface;

class TraceProxy
{
public:
	/// \brief Constructor
	TraceProxy( ModuleLoaderInterface* moduleLoader, const std::string& config, ErrorBufferInterface* errorhnd_);
	/// \brief Destructor
	~TraceProxy();

	/// \brief Create a storage object builder that wrappes sob with a proxy doing trace logging
	/// \param[in] sob storage object builder (ownership passed)
	/// \return analyzer object builder with trace logging
	StorageObjectBuilderInterface* createProxy( StorageObjectBuilderInterface* sob) const;

	/// \brief Create an analyzer object builder that wrappes sob with a proxy doing trace logging
	/// \param[in] aob analyzer object builder (ownership passed)
	/// \return analyzer object builder with trace logging
	AnalyzerObjectBuilderInterface* createProxy( AnalyzerObjectBuilderInterface* aob) const;

private:
	ErrorBufferInterface* m_errorhnd;
	TraceObjectBuilderInterface* m_traceObjectBuilder;
};

}//namespace
#endif
