#pragma once

#include <string>

#include "model.h"

class Printer
{
    public:
        Printer(const std::string& baseDirPath,
                const std::string& filenameBase);

        void print(const Model& model) const;

    private:
        std::string baseDir;
        std::string filenameBase;
};
