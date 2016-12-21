#include "scope.h"

#include <unordered_map>
#include <ostream>
#include <regex>

using namespace CppPrinting;
using namespace std;

static unordered_map<const ostream*, Scope::size_type> _levels;

Scope::Scope(ostream& s, string opener, string closer, bool appendEndl)
    : _s(s), _closer(closer), _appendEndl(appendEndl), _depth(1)
{
    _s << offset << opener;
    ++_levels[&_s];
    if (_appendEndl)
        _s << '\n';
    _s.flush();
}

Scope::Scope(ostream& s, const string& scope,
             const string& splitAt, const string& header,
             const string& opener, const string& closer)
    : _s(s), _closer(closer), _appendEndl(true), _depth(0)
{
    regex reSplit { splitAt };
    for(sregex_token_iterator it(scope.begin(), scope.end(), reSplit, -1), end;
        it != end; ++it, ++_depth, ++_levels[&s])
    {
        s << offset << header << *it << '\n'
          << offset << opener << '\n';
    }
    s.flush();
}

Scope::~Scope()
{
    if (_closer.empty())
        _levels[&_s] -= _depth;
    else
    {
        for (;_depth > 0; --_depth)
        {
            --_levels[&_s];
            if (_appendEndl)
                _s << offset << _closer << '\n';
            else
                _s << _closer;
        }
    }
    _s.flush();
}

Scope::size_type Scope::getOffset(const ostream& s)
{
    return _levels[&s];
}

void Scope::setOffset(const ostream& s, Scope::size_type offset)
{
    _levels[&s] = offset;
}
