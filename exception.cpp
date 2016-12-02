#include "exception.h"

#include <iostream>

void fail(int code, const std::string& msg)
{
    std::cerr << msg << std::endl;
    throw Exception { code };
}
