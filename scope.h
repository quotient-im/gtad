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

#include <QtCore/QTextStream>
#include <QtCore/QHash>

namespace ApiGenerator {

    static const bool NoNewLines = false;
    using size_type = int;

    class Scope
    {
        public:
            Scope(QTextStream& s, QString leader, QString trailer = "",
                  bool appendEndl = true);
            ~Scope();

            static size_type getOffset(const QTextStream& s);

            static QString offsetString(const QTextStream& s)
            {
                return QString(getOffset(s), ' ');
            }
        private:
            QTextStream& _s;
            QString _trailer;
    };

    inline QTextStream& offset(QTextStream& s)
    {
        return s << Scope::offsetString(s);
    }
}
