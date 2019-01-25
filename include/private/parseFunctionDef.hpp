/*
 * Copyright (c) 2018 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Some utility functions for parsing function definitions in program arguments
/// \file parseFunctionDef.hpp
#ifndef _STRUS_UTILITIES_PARSE_FUNCTION_DEF_HPP_INCLUDED
#define _STRUS_UTILITIES_PARSE_FUNCTION_DEF_HPP_INCLUDED
#include "strus/base/programLexer.hpp"
#include <string>

/// \brief strus toplevel namespace
namespace strus {

/// \brief Forward declaration
class ErrorBufferInterface;

namespace utils {

typedef std::vector<std::string> FunctionArgs;
typedef std::pair<std::string,FunctionArgs> FunctionDef;

std::vector<FunctionDef> parseFunctionDefs( const std::string& parameter, ErrorBufferInterface* errorhnd);

}}//namespace
#endif


