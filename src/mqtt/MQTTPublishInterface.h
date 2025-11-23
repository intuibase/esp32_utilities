#pragma once

#include <ostream>
#include <string_view>
#include <functional>

namespace ib::mqtt {

class MQTTPublishInterface {
public:
	virtual ~MQTTPublishInterface() {}
	virtual void publishAutoDiscoverySensor(std::string_view stateTopic, std::string_view sensorUniqueId, std::string_view sensorFriendlyName, std::string_view jsonValueName, std::string_view valueOperation, std::string_view unit, std::string_view stateClass, std::string_view devClass, std::string_view entityCategory) = 0;
	virtual void publishAutoDiscoveryBinarySensor(std::string_view stateTopic, std::string_view sensorUniqueId, std::string_view sensorFriendlyName, std::string_view jsonValueName, std::string_view deviceClass, std::string_view entityCategory) = 0;
	virtual void publishAutoDiscoveryButton(std::string_view commandTopic, std::string_view buttonUniqueId, std::string_view buttonFriendlyName, std::string_view deviceClass, std::string_view entityCategory) = 0;

	virtual void publishStateTopic(std::string_view topic, std::string_view payload, bool retained) = 0;

	virtual void subscribe(std::string_view topic, std::function<void(char *topic, uint8_t *payload, unsigned int payloadLen)> cb) = 0;

};

} // namespace ib::mqtt