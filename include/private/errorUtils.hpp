/*
 * Copyright (c) 2014 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Macros, classes and functions supporting error handling
/// \file errorUtils.hpp
#ifndef _STRUS_UTILITIES_ERROR_UTILITIES_HPP_INCLUDED
#define _STRUS_UTILITIES_ERROR_UTILITIES_HPP_INCLUDED
#include <stdexcept>
#include "private/internationalization.hpp"

#define THIS_COMPONENT_NAME	"strus utilities"
#define THIS_COMPONENT_ID	strus::StrusComponentUtilities

/// \brief strus toplevel namespace
namespace strus
{

#define CATCH_ERROR_MAP( contextExplainText, errorBuffer)\
	catch (const std::bad_alloc&)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseOutOfMem), _TXT("memory allocation error in %s"), THIS_COMPONENT_NAME);\
	}\
	catch (const std::runtime_error& err)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseRuntimeError), contextExplainText, err.what());\
	}\
	catch (const std::logic_error& err)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseLogicError), _TXT("logic error in %s: %s"), THIS_COMPONENT_NAME, err.what());\
	}\
	catch (const std::exception& err)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseUncaughtException), _TXT("uncaught exception in %s: %s"), THIS_COMPONENT_NAME, err.what());\
	}

#define CATCH_ERROR_ARG1_MAP( contextExplainText, ARG, errorBuffer)\
	catch (const std::bad_alloc&)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseOutOfMem), _TXT("memory allocation error in %s"), THIS_COMPONENT_NAME);\
	}\
	catch (const std::runtime_error& err)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseRuntimeError), contextExplainText, ARG, err.what());\
	}\
	catch (const std::logic_error& err)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseLogicError), _TXT("logic error in %s: %s"), THIS_COMPONENT_NAME, err.what());\
	}\
	catch (const std::exception& err)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseUncaughtException), _TXT("uncaught exception in %s: %s"), THIS_COMPONENT_NAME, err.what());\
	}

#define CATCH_ERROR_MAP_RETURN( contextExplainText, errorBuffer, errorReturnValue)\
	catch (const std::bad_alloc&)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseOutOfMem), _TXT("memory allocation error in %s"), THIS_COMPONENT_NAME);\
		return errorReturnValue;\
	}\
	catch (const std::runtime_error& err)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseRuntimeError), contextExplainText, err.what());\
		return errorReturnValue;\
	}\
	catch (const std::logic_error& err)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseLogicError), _TXT("logic error in %s: %s"), THIS_COMPONENT_NAME, err.what());\
		return errorReturnValue;\
	}\
	catch (const std::exception& err)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseUncaughtException), _TXT("uncaught exception in %s: %s"), THIS_COMPONENT_NAME, err.what());\
		return errorReturnValue;\
	}

#define CATCH_ERROR_ARG1_MAP_RETURN( contextExplainText, ARG, errorBuffer, errorReturnValue)\
	catch (const std::bad_alloc&)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseOutOfMem), _TXT("memory allocation error in %s"), THIS_COMPONENT_NAME);\
		return errorReturnValue;\
	}\
	catch (const std::runtime_error& err)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseRuntimeError), contextExplainText, ARG, err.what());\
		return errorReturnValue;\
	}\
	catch (const std::logic_error& err)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseLogicError), _TXT("logic error in %s: %s"), THIS_COMPONENT_NAME, err.what());\
		return errorReturnValue;\
	}\
	catch (const std::exception& err)\
	{\
		(errorBuffer).report( *strus::ErrorCode(THIS_COMPONENT_ID,strus::ErrorOperationUnknown,strus::ErrorCauseUncaughtException), _TXT("uncaught exception in %s: %s"), THIS_COMPONENT_NAME, err.what());\
		return errorReturnValue;\
	}

}//namespace
#endif
