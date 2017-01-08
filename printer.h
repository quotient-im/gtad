#pragma once

#include <string>

#include "model.h"
#include "formatting.h"

class Printer
{
    public:
        using stream_type = SrcFormatting::osrcfstream;

        Printer(const std::string& basePath,
                const std::string& filenameBase);

        void print(const Model& model);

    private:
        stream_type hS;
        stream_type cppS;

        void printDataDef(const StructDef& dm);
        void printCall(const std::string& ns, const CallClass& cm);
        void printConstructors(const CallClass& cm, const std::string& ns = "");
        void printInitializer(SrcFormatting::WrappedLine& lw,
                              const std::string& callName,
                              const Call& callOverload);
        void printParamInitializers(const Call& call, bool withSetters);
};
