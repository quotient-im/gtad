#include "formatting.h"

#include <unordered_map>
#include <algorithm>
#include <regex>

using namespace SrcFormatting;
using namespace std;

static const osrcfstream::src_off_type TABSIZE = 4;

osrcfstream::src_off_type osrcfstream::offsetWidth() const
{
    return _offsetLevel * TABSIZE;
}

osrcfstream& SrcFormatting::offset(osrcfstream& s)
{
    fill_n(ostreambuf_iterator<char>(s), s.offsetWidth(), ' ');
    return s;
}

Offset::Offset(WrappedLine& lw, Offset::off_type depth)
        : Offset(lw.baseStream(), depth)
{ }

WrappedLine::WrappedLine(WrappedLine::stream_type& s, WrappedLine::size_type margin)
    : _s(s << offset), _curColumn(size_type(s.offsetWidth())), _margin(margin), _softWrapOffset(_s, 0)
{ }

void WrappedLine::maybeEndline()
{
    // Check if the segment from the last CR will overflow _margin in _s if appended
    const size_type newLength =
            _curColumn + _lastFiller.size() + size_type(tellp());
    if (newLength <= _margin)
    {
        _s << _lastFiller;
        _curColumn = newLength;
    }
    else
    {
        _s << '\n' << offset;
        _curColumn = size_type(_s.offsetWidth() + tellp());
    }
    _s << str();
    str({}); // Clear the segment from the last CR
    _softWrapOffset.set(1);
}

Scope::Scope(stream_type& s, string leader, string trailer, bool appendNewLines)
    : Offset(s, 0), _trailer(trailer), _appendEndl(appendNewLines)
{
    _s << offset << leader;
    if (_appendEndl)
        _s << '\n';
    shift();
}

Scope::Scope(stream_type& s, const string& header, const string& scopeName,
             const string& leader, const string& trailer)
    : Offset(s, 0), _trailer(trailer), _appendEndl(true)
{
    _s << offset << header << scopeName << '\n'
       << offset << leader << '\n';
    shift();
}

Scope::Scope(stream_type& s, const string& header, const string& scope,
             const string& leader, const string& trailer, const string& splitAt)
    : Offset(s, 0), _trailer(trailer), _appendEndl(true)
{
    regex reSplit { splitAt };
    for(sregex_token_iterator it(scope.begin(), scope.end(), reSplit, -1), end;
        it != end; ++it, shift())
    {
        _s << offset << header << *it << '\n'
           << offset << leader << '\n';
    }
}

Scope::~Scope()
{
    if (!_trailer.empty())
    {
        while (_nStops > 0)
        {
            shift(-1);
            if (_appendEndl)
                _s << offset << _trailer << '\n';
            else
                _s << _trailer;
        }
    }
}

