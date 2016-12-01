#include "scope.h"

using namespace CppPrinting;

static QHash<const QTextStream*, size_type> _levels;

Scope::Scope(QTextStream& s, QString leader, QString trailer, bool appendEndl)
    : _s(s), _trailer(trailer)
{
    s << offset << leader;
    if (appendEndl)
    {
        s << "\n";
        if (!_trailer.isEmpty())
            _trailer = offsetString(s) + _trailer;
    }
    ++_levels[&_s];
}

Scope::~Scope()
{
    --_levels[&_s];
    if (!_trailer.isEmpty())
        _s << _trailer << "\n";
}

size_type Scope::getOffset(const QTextStream& s)
{
    return _levels[&s];
}

void Scope::setOffset(const QTextStream& s, size_type offset)
{
    _levels[&s] = offset;
}

