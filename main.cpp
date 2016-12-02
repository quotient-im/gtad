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

#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>

#include "translator.h"
#include "exception.h"

int main( int argc, char* argv[] )
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("Quaternion");
    QCoreApplication::setApplicationName("qmatrixclient-api-generator");
    QCoreApplication::setApplicationVersion("0.0");

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main",
        "Client-server API source files generator for libqmatrixclient"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption outputDirOption("out",
        QCoreApplication::translate("main", "Write generated files to <outputdir>."),
        "outputdir");
    parser.addOption(outputDirOption);

    parser.addPositionalArgument("files",
        QCoreApplication::translate("main", "Files with API definition in Swagger format."),
        "files...");

    parser.process(app);

    try
    {
        auto filePaths = parser.positionalArguments();
        std::for_each(filePaths.begin(), filePaths.end(),
                      Translator(parser.value(outputDirOption)));
    }
    catch (Exception& e)
    {
        return e.code;
    }

    return 0;
}
