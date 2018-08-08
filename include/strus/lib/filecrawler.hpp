/*
 * Copyright (c) 2018 Patrick P. Frey
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
/// \brief Library for loading files in chunks for multithreaded processing
/// \file filecrawler.hpp
#ifndef _STRUS_UTILITIES_LIB_FILECRAWLER_HPP_INCLUDED
#define _STRUS_UTILITIES_LIB_FILECRAWLER_HPP_INCLUDED
#include <string>
#include <vector>

/// \brief strus toplevel namespace
namespace strus
{

/// \brief Forward declaration
class ErrorBufferInterface;
/// \brief Forward declaration
class FileCrawlerInterface;


/// \brief Create an interface for loading files in chunks for multithreaded processing
/// \param[in] path path where to load files from
/// \param[in] chunkSize maximum number of files per chunk loaded
/// \param[in] extension extension of the files to load, empty if no restriction on the extension given
/// \param[in] errorhnd error buffer interface for exceptions thrown
/// \return the file crawler interface (with ownership)
FileCrawlerInterface* createFileCrawlerInterface( const std::string& path, int chunkSize, const std::string& extension, ErrorBufferInterface* errorhnd);

}//namespace
#endif

