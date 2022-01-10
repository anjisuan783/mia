#ifndef SDPTRANSFORM_HPP
#define SDPTRANSFORM_HPP

#include "json.hpp"
#include <string>
#include <vector>

using json = nlohmann::json;

namespace sdptransform
{
	json parse(const std::string& sdp);

	json parseParams(const std::string& str);

	std::vector<int> parsePayloads(const std::string& str);

	json parseImageAttributes(const std::string& str);

	json parseSimulcastStreamList(const std::string& str);

	std::string write(json& session);
}

#endif
