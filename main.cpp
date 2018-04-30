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

#include "translator.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>
#include <QtCore/QDir>

#include <iostream>

int main( int argc, char* argv[] )
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QMatrixClient");
    QCoreApplication::setApplicationName("GTAD");
    QCoreApplication::setApplicationVersion("0.5");

    QCommandLineParser parser;
    parser.setApplicationDescription(QCoreApplication::translate("main",
        "Client-server API source files generator"));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption configPathOption("config",
        QCoreApplication::translate("main", "API generator configuration in YAML format"),
        "configfile");
    parser.addOption(configPathOption);

    QCommandLineOption outputDirOption("out",
        QCoreApplication::translate("main", "Write generated files to <outputdir>."),
        "outputdir");
    parser.addOption(outputDirOption);

    parser.addPositionalArgument("files",
        QCoreApplication::translate("main", "Files or directories with API definition in Swagger format. Append a hyphen to exclude a file/directory."),
        "files...");

    parser.process(app);

    try
    {
        Translator t(parser.value(configPathOption), parser.value(outputDirOption));

        QStringList paths, exclusions;
        for (const auto& path: parser.positionalArguments())
        {
            if (path.endsWith('-'))
                exclusions.append(path.left(path.size() - 1));
            else
                paths.append(path);
        }
        for(auto& path: paths)
        {
            if (!QFileInfo(path).isDir())
            {
                t.processFile(path.toStdString(), "");
                continue;
            }

            if (!path.isEmpty() && !path.endsWith('/'))
                path.push_back('/');
            QStringList filesList = QDir(path).entryList(QDir::Readable|QDir::Files);
            for (const auto& fn: filesList)
                if (!exclusions.contains(fn))
                    t.processFile(fn.toStdString(), path.toStdString());
        }
    }
    catch (Exception& e)
    {
        std::cerr << e.message << std::endl;
        return 3;
    }

    return 0;
}
