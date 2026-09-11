#pragma once

#include <nlohmann/json.hpp>

namespace Tga
{
	/// <summary>
	/// Used to pass around json data. This struct is possible to easily foward declare and reduces the need to include the nlohmann json headers 
	/// </summary>
	struct JsonData
	{
		nlohmann::json json;
	};

} // namespace Tga