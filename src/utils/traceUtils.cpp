/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "private/traceUtils.hpp"
#include "strus/storageObjectBuilderInterface.hpp"
#include "strus/analyzerObjectBuilderInterface.hpp"
#include "strus/traceObjectBuilderInterface.hpp"
#include "strus/errorBufferInterface.hpp"
#include "strus/moduleLoaderInterface.hpp"
#include "private/internationalization.hpp"
#include "private/errorUtils.hpp"
#include <memory>

using namespace strus;

TraceProxy::TraceProxy( ModuleLoaderInterface* moduleLoader, const std::string& config, ErrorBufferInterface* errorhnd_)
	:m_errorhnd(errorhnd_),m_traceObjectBuilder(moduleLoader->createTraceObjectBuilder(config))
{
	if (!m_traceObjectBuilder) throw strus::runtime_error(_TXT("failed to create trace object builder"));
}

TraceProxy::~TraceProxy()
{
	if (m_traceObjectBuilder) delete m_traceObjectBuilder;
}

StorageObjectBuilderInterface* TraceProxy::createProxy( StorageObjectBuilderInterface* sob) const
{
	StorageObjectBuilderInterface* rt = m_traceObjectBuilder->createStorageObjectBuilder( sob);
	if (!rt) throw strus::runtime_error( _TXT( "failed to create storage object builder trace proxy"));
	return rt;
}

AnalyzerObjectBuilderInterface* TraceProxy::createProxy( AnalyzerObjectBuilderInterface* aob) const
{
	AnalyzerObjectBuilderInterface* rt = m_traceObjectBuilder->createAnalyzerObjectBuilder( aob);
	if (!rt) throw strus::runtime_error( _TXT( "failed to create analyzer object builder trace proxy"));
	return rt;
}


