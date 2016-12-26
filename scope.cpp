#include "scope.h"

#include <unordered_map>
#include <ostream>
#include <regex>

using namespace CppPrinting;
using namespace std;

static unordered_map<const ostream*, Offset::size_type> _levels;
static const Offset::size_type TABSIZE = 4;

Offset::Offset(ostream& s, const ostream& otherS)
    : _s(s), _depth(_levels[&otherS] - _levels[&_s])
{
    _levels[&_s] = _levels[&otherS];
}

Offset::Offset(ostream& s, const string& leader)
    : _s(s), _depth(1)
{
    _s << offset << leader << endl;
    promote();
}

Offset::~Offset()
{
    _levels[&_s] -= _depth;
}

void Offset::promote(Offset::size_type depth)
{
    _levels[&_s] += depth;
    _depth += depth;
}

void Offset::demote(Offset::size_type depth)
{
    _levels[&_s] -= depth;
    _depth -= depth;
}

Scope::Scope(ostream& s, string leader, string trailer, bool appendNewLines)
    : Offset(s), _trailer(trailer), _appendEndl(appendNewLines)
{
    _s << offset << leader;
    if (_appendEndl)
        _s << '\n';
    _s.flush();
    promote();
}

Scope::Scope(ostream& s, const string& header, const string& scopeName,
             const string& leader, const string& trailer)
    : Offset(s), _trailer(trailer), _appendEndl(true)
{
    _s << offset << header << scopeName << '\n'
       << offset << leader << endl;
    promote();
}

Scope::Scope(ostream& s, const string& header, const string& scope,
             const string& leader, const string& trailer, const string& splitAt)
    : Offset(s), _trailer(trailer), _appendEndl(true)
{
    regex reSplit { splitAt };
    for(sregex_token_iterator it(scope.begin(), scope.end(), reSplit, -1), end;
        it != end; ++it, promote())
    {
        _s << offset << header << *it << '\n'
           << offset << leader << '\n';
    }
    _s.flush();
}

Scope::~Scope()
{
    if (!_trailer.empty())
    {
        while (_depth > 0)
        {
            demote();
            if (_appendEndl)
                _s << offset << _trailer << '\n';
            else
                _s << _trailer;
        }
        _s.flush();
    }
}

ostream& CppPrinting::offset(ostream& s)
{
    fill_n(ostreambuf_iterator<char>(s), _levels[&s] * TABSIZE, ' ');
    return s;
}
