/******************************************************************************
 * Copyright (C) 2016 Kitsune Ral <kitsune-ral@users.sf.net>
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

#pragma once

#include <string>
#include <regex>
#include <algorithm>
#include <ostream>

namespace CppPrinting
{
    class Scope
    {
        public:
            using size_type = std::string::size_type;

            static const size_type TABSIZE = 4;
            static const bool NoNewLines = false;

            Scope(std::ostream& s, std::string opener, std::string closer = "",
                  bool appendNewLines = true);
            Scope(std::ostream& s, const std::string& scope,
                  const std::string& splitAt, const std::string& header,
                  const std::string& opener, const std::string& closer = "");
            ~Scope();

            static size_type getOffset(const std::ostream& s);
            static void setOffset(const std::ostream& s, size_type offset);

            static std::string offsetString(const std::ostream& s)
            {
                return std::string(getOffset(s) * TABSIZE, ' ');
            }
            std::string offsetString() const
            {
                return offsetString(_s);
            }

        private:
            std::ostream& _s;
            std::string _closer;
            bool _appendEndl;
            size_type _depth;
    };
    inline std::ostream& offset(std::ostream& s)
    {
        return s << Scope::offsetString(s);
    }
}
