#pragma once

#include <string>

namespace ib::mqtt
{

struct MqttConfig
{
	bool enabled = false;
	std::string brokerAddress;
	uint16_t brokerPort;
	std::string username;
	std::string password;
	std::string clientId;
	std::string base;
	uint16_t keepAlive = 60;
	uint16_t interval = 10;
};

}
