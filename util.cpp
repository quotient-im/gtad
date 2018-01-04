/******************************************************************************
 * Copyright (C) 2017 Kitsune Ral <kitsune-ral@users.sf.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "util.h"

#include <fstream>
#include <iostream>

std::string readFile(const std::string& fileName)
{
    std::ifstream ifs { fileName };
    if (!ifs.good())
    {
        // FIXME: Figure a better error reporting mechanism.
        // Once we fix this, we can use this function from Printer as well.
        std::cerr << "Failed to open file: " << fileName << std::endl;
        return "";
    }
    std::string result;
    getline(ifs, result, '\0'); // Won't work on files with NULs
    return result;
}

Exception::~Exception() noexcept = default;
