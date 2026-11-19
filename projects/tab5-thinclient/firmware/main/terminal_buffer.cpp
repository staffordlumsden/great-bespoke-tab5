#include "terminal_buffer.hpp"

#include <cctype>

namespace {
bool isUtf8Continuation(uint8_t c)
{
    return (c & 0xC0) == 0x80;
}

size_t previousUtf8CharStart(const String& text)
{
    size_t pos = text.length();
    while (pos > 0) {
        --pos;
        if (!isUtf8Continuation(static_cast<uint8_t>(text[pos]))) {
            return pos;
        }
    }
    return 0;
}

size_t skipAnsiSgr(const String& text, size_t i)
{
    const size_t n = text.length();
    if (i + 1 >= n || static_cast<uint8_t>(text[i]) != 0x1B || text[i + 1] != '[') {
        return i;
    }
    size_t j = i + 2;
    while (j < n) {
        uint8_t uc = static_cast<uint8_t>(text[j]);
        ++j;
        if (uc >= 0x40 && uc <= 0x7E) {
            return j;
        }
    }
    return n;
}

size_t displayCells(const String& text)
{
    size_t cells = 0;
    const size_t n = text.length();
    for (size_t i = 0; i < n;) {
        uint8_t c = static_cast<uint8_t>(text[i]);
        if (c == 0x1B && i + 1 < n && text[i + 1] == '[') {
            i = skipAnsiSgr(text, i);
            continue;
        }
        if (c < 0x80) {
            ++cells;
        } else if (!isUtf8Continuation(c)) {
            cells += 2;
        }
        ++i;
    }
    return cells;
}

size_t wrapByteIndex(const String& text, size_t columns)
{
    size_t cells = 0;
    size_t lastGood = 0;
    const size_t n = text.length();
    for (size_t i = 0; i < n;) {
        uint8_t c = static_cast<uint8_t>(text[i]);
        if (c == 0x1B && i + 1 < n && text[i + 1] == '[') {
            i = skipAnsiSgr(text, i);
            lastGood = i;
            continue;
        }
        size_t nextCells = cells;
        if (c < 0x80) {
            ++nextCells;
        } else if (!isUtf8Continuation(c)) {
            nextCells += 2;
        }
        if (nextCells > columns && lastGood > 0) {
            return lastGood;
        }
        cells = nextCells;
        ++i;
        lastGood = i;
    }
    return n;
}

String trailingActiveSgr(const String& text)
{
    String active;
    const size_t n = text.length();
    for (size_t i = 0; i < n;) {
        if (static_cast<uint8_t>(text[i]) == 0x1B && i + 1 < n && text[i + 1] == '[') {
            size_t next = skipAnsiSgr(text, i);
            if (next > i + 2 && text[next - 1] == 'm') {
                String seq = text.substring(i, next);
                if (seq == "\x1B[0m" || seq == "\x1B[m") {
                    active = "";
                } else {
                    active = seq;
                }
            }
            i = next;
        } else {
            ++i;
        }
    }
    return active;
}
}

TerminalBuffer::TerminalBuffer(size_t maxLines) : _maxLines(maxLines)
{
    _lines.reserve(128);
}

void TerminalBuffer::setViewport(size_t columns, size_t rows)
{
    _columns = max<size_t>(1, columns);
    _rows = max<size_t>(1, rows);
    trim();
}

void TerminalBuffer::clear()
{
    _lines.clear();
    _current = "";
    _escSeq = "";
    _scrollOffset = 0;
    _escapeState = 0;
}

void TerminalBuffer::append(const char* data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        appendChar(data[i]);
    }
}

void TerminalBuffer::append(const String& text)
{
    append(text.c_str(), text.length());
}

void TerminalBuffer::inputEcho(char c)
{
    appendChar(c);
}

void TerminalBuffer::backspace()
{
    if (_current.length()) {
        _current.remove(previousUtf8CharStart(_current));
    }
}

void TerminalBuffer::scroll(int delta)
{
    const size_t total = _lines.size() + (_current.length() ? 1 : 0);
    const size_t maxOffset = total > _rows ? total - _rows : 0;
    int next = static_cast<int>(_scrollOffset) + delta;
    if (next < 0) {
        next = 0;
    }
    if (next > static_cast<int>(maxOffset)) {
        next = static_cast<int>(maxOffset);
    }
    _scrollOffset = static_cast<size_t>(next);
}

void TerminalBuffer::scrollToBottom()
{
    _scrollOffset = 0;
}

String TerminalBuffer::lineAt(size_t viewportRow) const
{
    const size_t total = _lines.size() + 1;
    if (viewportRow >= _rows) {
        return "";
    }

    const size_t maxOffset = total > _rows ? total - _rows : 0;
    const size_t offset = min(_scrollOffset, maxOffset);
    const size_t firstIndex = total > _rows ? total - _rows - offset : 0;
    const size_t index = firstIndex + viewportRow;
    if (index < _lines.size()) {
        return _lines[index];
    }
    if (index == _lines.size()) {
        return _current;
    }
    return "";
}

size_t TerminalBuffer::inputViewportRow() const
{
    const size_t total = _lines.size() + 1;
    if (_rows == 0) {
        return 0;
    }
    return total <= _rows ? total - 1 : _rows - 1;
}

bool TerminalBuffer::atBottom() const
{
    return _scrollOffset == 0;
}

void TerminalBuffer::pushLine(String line)
{
    _lines.push_back(line);
    trim();
}

void TerminalBuffer::appendChar(char c)
{
    uint8_t uc = static_cast<uint8_t>(c);
    if (_escapeState == 1) {
        if (c == '[') {
            _escapeState = 2;
            _escSeq = "";
        } else if (c == ']') {
            _escapeState = 3;
        } else {
            _escapeState = 0;
        }
        return;
    }
    if (_escapeState == 2) {
        if (uc >= 0x40 && uc <= 0x7E) {
            if (c == 'm' && _escSeq.length() < 48) {
                _current += "\x1B[";
                _current += _escSeq;
                _current += 'm';
            }
            _escSeq = "";
            _escapeState = 0;
        } else if (_escSeq.length() < 48) {
            _escSeq += c;
        }
        return;
    }
    if (_escapeState == 3) {
        if (c == '\a') {
            _escapeState = 0;
        } else if (c == 0x1B) {
            _escapeState = 1;
        }
        return;
    }
    if (c == 0x1B) {
        _escapeState = 1;
        return;
    }
    if (c == '\r') {
        return;
    }
    if (c == '\n') {
        pushLine(_current);
        _current = trailingActiveSgr(_current);
        return;
    }
    if (c == '\b' || c == 0x7F) {
        backspace();
        return;
    }
    if (c == '\t') {
        _current += "    ";
    } else if (std::isprint(static_cast<unsigned char>(c)) || uc >= 0x80) {
        _current += c;
    }
    while (displayCells(_current) > _columns) {
        size_t index = wrapByteIndex(_current, _columns);
        String slice = _current.substring(0, index);
        String carry = trailingActiveSgr(slice);
        if (carry.length()) {
            slice += "\x1B[0m";
        }
        pushLine(slice);
        _current = carry + _current.substring(index);
    }
}

void TerminalBuffer::trim()
{
    while (_lines.size() > _maxLines) {
        _lines.erase(_lines.begin());
    }
}
