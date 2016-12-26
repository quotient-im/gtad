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
    class Offset
    {
        public:
            using size_type = std::string::size_type;

            explicit Offset(std::ostream& s, size_type depth = 0)
                : _s(s), _depth(0)
            {
                promote(depth);
            }
            Offset(std::ostream& s, const std::ostream& otherS);
            Offset(std::ostream& s, const std::string& leader);
            ~Offset();

            void promote(size_type depth = 1);
            void demote(size_type depth = 1);

        protected:
            std::ostream& _s;
            size_type _depth;
    };
    class Scope : public Offset
    {
        public:
            static const bool NoNewLines = false;

            Scope(std::ostream& s, std::string leader, std::string trailer,
                  bool appendNewLines = true);
            Scope(std::ostream& s, const std::string& header,
                  const std::string& scopeName, const std::string& leader,
                  const std::string& trailer);
            Scope(std::ostream& s, const std::string& header,
                  const std::string& scope, const std::string& leader,
                  const std::string& trailer, const std::string& splitAt);
            ~Scope();

        private:
            std::string _trailer;
            bool _appendEndl;
    };

    std::ostream& offset(std::ostream& s);
}
