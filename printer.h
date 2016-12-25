#pragma once

#include <string>
#include <vector>
#include <fstream>

#include "model.h"

class Printer
{
    public:
        Printer(const std::string& basePath,
                const std::string& filenameBase);

        void print(const Model& model);
        void printDataDef(const Type& dm);
        void printCall(const std::string& ns, const CallClass& cm);
        void printConstructors(const CallClass& cm, const std::string& ns = "");
        void printInitializer(const std::string& callName,
                       const Call& callOverload);
        void printParamInitializer(const Call::params_type& params,
                                   const std::string& containerName);

    private:
        std::ofstream hS;
        std::ofstream cppS;
};
