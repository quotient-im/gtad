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

#include <fstream>
#include <sstream>

namespace SrcFormatting
{
    class osrcfstream : public std::ofstream
    {
        public:
            using src_off_type = std::string::difference_type;

            using std::ofstream::ofstream;

            osrcfstream& operator<<(osrcfstream& (*f)(osrcfstream&))
            {
                f(*this);
                return *this;
            }

            osrcfstream& operator<<(std::ostream& (*f)(std::ostream&))
            {
                f(*this);
                return *this;
            }

            void promote(src_off_type nStops = 1)
            {
                _offsetLevel += nStops;
            }

            void demote(src_off_type nStops = 1)
            {
                _offsetLevel -= nStops;
            }

            src_off_type offsetWidth() const;

        private:
            src_off_type _offsetLevel = 0;
    };

    osrcfstream& offset(osrcfstream& s);

    template <typename T>
    inline osrcfstream& operator<<(osrcfstream& osfs, const T& val)
    {
        static_cast<std::ofstream&>(osfs) << val;
        return osfs;
    }

    /**
     * @brief Marks a place in the @class LineWrapper stream where wrapping can occur
     */
    struct soft_endl
    {
        explicit soft_endl(const std::string& filler = "") : _filler(filler) { }

        std::string _filler;
    };

    class WrappedLine;

    class Offset
    {
        public:
            using off_type = std::string::difference_type;
            using stream_type = osrcfstream;

            explicit Offset(stream_type& s, off_type depth = 1)
                    : _s(s)
            {
                shift(depth);
            }
            explicit Offset(WrappedLine& lw, off_type depth = 1);
            Offset(stream_type& s, const std::string& leader)
                    : Offset(s << offset << leader << '\n')
            { }
            ~Offset()
            {
                shift(-_nStops);
            }

            void shift(off_type nStops = 1)
            {
                _s.promote(nStops); _nStops += nStops;
            }

            void set(off_type nStops)
            {
                shift(nStops - _nStops);
            }

        protected:
            stream_type& _s;
            off_type _nStops = 0;
    };

    /**
     * @brief Wraps output to the stream @param s at column @param margin
     *
     * Pre-requisite: before creating an object of this class, @param s should be
     * at the first (0'th) column.
     */
    class WrappedLine : public std::ostringstream
    {
        public:
            using size_type = std::string::size_type;
            using stream_type = osrcfstream;

            WrappedLine(stream_type& s, size_type margin = 80);
            ~WrappedLine()
            {
                endLine();
            }

            WrappedLine& operator<<(const soft_endl& se)
            {
                maybeEndline();
                _lastFiller = se._filler;
                return *this;
            }

            void maybeEndline();
            void endLine()
            {
                maybeEndline();
                _s << '\n';
                _curColumn = 0;
            }

            stream_type& baseStream() const
            {
                return _s;
            }

        private:
            stream_type& _s;
            size_type _curColumn;
            const size_type _margin;
            std::string _lastFiller;
            Offset _softWrapOffset;
    };

    template <typename T>
    inline WrappedLine& operator<<(WrappedLine& lw, const T& val)
    {
        static_cast<std::ostringstream&>(lw) << val;
        return lw;
    }

    class Scope : public Offset
    {
        public:
            static const bool NoNewLines = false;

            Scope(stream_type& s, std::string leader, std::string trailer,
                  bool appendNewLines = true);
            Scope(stream_type& s, const std::string& header,
                  const std::string& scopeName, const std::string& leader,
                  const std::string& trailer);
            Scope(stream_type& s, const std::string& header,
                  const std::string& scope, const std::string& leader,
                  const std::string& trailer, const std::string& splitAt);
            ~Scope();

        private:
            std::string _trailer;
            bool _appendEndl;
    };
}
