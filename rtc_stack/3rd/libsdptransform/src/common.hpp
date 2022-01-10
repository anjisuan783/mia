#ifndef SDPTRANSFORM_COMMON_HPP
#define SDPTRANSFORM_COMMON_HPP

#include <map>
#include <functional>

#ifdef __USE_BOOST_REGEX__
#include <boost/regex.hpp>
#else
#include <regex>
#endif

#ifdef __USE_BOOST_REGEX__
  #define Regular boost
#else
  #define Regular std
#endif

namespace sdptransform
{
	namespace grammar
	{
		struct Rule
		{
			std::string name;
			std::string push;
			Regular::regex reg;
			std::vector<std::string> names;
			std::vector<char> types;
			std::string format;
			std::function<const std::string(const json&)> formatFunc;
		};

		extern const std::map<char, std::vector<Rule>> rulesMap;
	}
}

#endif
