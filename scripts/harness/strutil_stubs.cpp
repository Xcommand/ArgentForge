// Copies of the handful of strutil functions the actor parser calls, so the
// check links without dragging in everything StringUtils.cpp depends on.
#include "Main.h"
#include "Utility/StringUtils.h"
#include <algorithm>

using namespace slade;

namespace
{
const string whitespace = " \t\n\r\f\v";
}

bool strutil::equalCI(string_view left, string_view right)
{
	const auto sz = left.size();
	if (right.size() != sz)
		return false;

	for (auto a = 0u; a < sz; ++a)
		if (tolower(left[a]) != tolower(right[a]))
			return false;

	return true;
}

bool strutil::startsWith(string_view str, string_view check)
{
	return check.size() <= str.size() && str.compare(0, check.size(), check) == 0;
}

bool strutil::startsWith(string_view str, char check)
{
	return !str.empty() && str[0] == check;
}

bool strutil::endsWith(string_view str, string_view check)
{
	return check.size() <= str.size() && str.compare(str.size() - check.size(), check.size(), check) == 0;
}

bool strutil::endsWith(string_view str, char check)
{
	return !str.empty() && str.back() == check;
}

bool strutil::contains(string_view str, char check)
{
	return str.find(check) != string::npos;
}

bool strutil::contains(string_view str, string_view check)
{
	return str.find(check) != string::npos;
}

string& strutil::replaceIP(string& str, string_view from, string_view to)
{
	size_t start_pos = 0;
	while ((start_pos = str.find(from.data(), start_pos)) != string::npos)
	{
		str.replace(start_pos, from.length(), to.data(), to.size());
		start_pos += to.length();
	}
	return str;
}

string strutil::replace(string_view str, string_view from, string_view to)
{
	auto s = string{ str };
	replaceIP(s, from, to);
	return s;
}

string& strutil::upperIP(string& str)
{
	transform(str.begin(), str.end(), str.begin(), ::toupper);
	return str;
}

string& strutil::lowerIP(string& str)
{
	transform(str.begin(), str.end(), str.begin(), ::tolower);
	return str;
}

string strutil::upper(string_view str)
{
	auto s = string{ str };
	transform(s.begin(), s.end(), s.begin(), ::toupper);
	return s;
}

string strutil::lower(string_view str)
{
	auto s = string{ str };
	transform(s.begin(), s.end(), s.begin(), ::tolower);
	return s;
}

string& strutil::trimIP(string& str)
{
	str.erase(0, str.find_first_not_of(whitespace));
	str.erase(str.find_last_not_of(whitespace) + 1);
	return str;
}

string strutil::trim(string_view str)
{
	auto s = string{ str };
	s.erase(0, s.find_first_not_of(whitespace));
	s.erase(s.find_last_not_of(whitespace) + 1);
	return s;
}

string strutil::rtrim(string_view str)
{
	auto s = string{ str };
	s.erase(s.find_last_not_of(whitespace) + 1);
	return s;
}

vector<string> strutil::split(string_view str, char separator, bool skip_duplicates)
{
	unsigned       start = 0;
	const auto     size  = str.size();
	vector<string> parts;
	for (unsigned c = 0; c < size; ++c)
	{
		if (str[c] == separator)
		{
			if (!skip_duplicates || c > start)
				parts.emplace_back(str.substr(start, c - start));
			start = c + 1;
		}
	}

	parts.emplace_back(str.substr(start, size - start));
	return parts;
}

bool strutil::startsWithCI(string_view str, string_view check)
{
	if (check.size() > str.size())
		return false;

	for (size_t c = 0; c < check.size(); ++c)
		if (tolower(str[c]) != tolower(check[c]))
			return false;

	return true;
}

bool strutil::containsCI(string_view str, string_view check)
{
	return lower(str).find(lower(check)) != string::npos;
}

string strutil::ltrim(string_view str)
{
	auto s = string{ str };
	s.erase(0, s.find_first_not_of(whitespace));
	return s;
}

int strutil::asInt(string_view str, int base)
{
	int value = 0, sign = 1;
	size_t a  = 0;

	if (a < str.size() && (str[a] == '-' || str[a] == '+'))
		sign = str[a++] == '-' ? -1 : 1;

	for (; a < str.size(); ++a)
	{
		int digit = isdigit((unsigned char)str[a]) ? str[a] - '0' : tolower(str[a]) - 'a' + 10;
		if (digit < 0 || digit >= base)
			break;

		value = value * base + digit;
	}

	return value * sign;
}
