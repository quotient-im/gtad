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

#include "model.h"
#include "printer.h"

#include <QtCore/QString>

class Translator
{
    public:
        Translator(const QString& configFilePath, QString outputDirPath);

        Model processFile(std::string filePath, std::string baseDirPath) const;
        TypeUsage mapType(const std::string& swaggerType,
                          const std::string& swaggerFormat, bool constRef) const;
        TypeUsage mapArrayType(const TypeUsage& innerType, bool constRef) const;

    private:
        QString _outputDirPath;
        Printer _printer;
};
