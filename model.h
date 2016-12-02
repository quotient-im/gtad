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

#include <vector>

#include <QtCore/QString>
#include <QtCore/QStringBuilder>

struct VariableDefinition
{
    QString type;
    QString name;

    QString toString() const { return type % " " % name; }
};

struct CustomResponseType
{
    QString name;

    std::vector<VariableDefinition> fields;
};

class QTextStream;

struct CallConfigModel
{
    QString className;

    struct CallOverload
    {
        std::vector<VariableDefinition> params;
        bool needsToken;

        void addParam(const QString& type, const std::string& name);
    };
    std::vector<CallOverload> callOverloads;
    VariableDefinition replyFormatVar;
    CustomResponseType responseType;

    CallConfigModel(const QString& n) : className(n) { }
    void printTo(QTextStream& hText, QTextStream& cppText);
};


