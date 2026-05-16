#pragma once

#include <ostream>
#include <string_view>
#include <vector>

#include <logger/LoggerInterface.h>
#include "TimeHelpers.h"
#include "config.h"
#include "mqtt/IntuibasePubSubClientWrapper.h"
#include "mqtt/MQTTReporterInterface.h"
#include "mqtt/MQTTPublishInterface.h"
#include "mqtt/MqttConfig.h"
#include "viewable_stringbuf.h"

using namespace std::string_view_literals;
using namespace std::string_literals;
namespace ib::mqtt {

class MQTT : public MQTTPublishInterface {
public:
	using getCounter_t = std::function<void(std::ostream &)>;

	struct HomeAssistantDeviceInfo {
		std::string_view name;
		std::string_view model;
		std::string_view manufacturer;
		std::string_view swVersion;
	};

	MQTT(std::shared_ptr<logger::LoggerInterface> log, ib::mqtt::MqttConfig config, HomeAssistantDeviceInfo deviceInfo, std::vector<std::shared_ptr<MQTTReporterInterface>> reporters) : log_(std::move(log)), config_(std::move(config)), deviceInfo_(std::move(deviceInfo)), client_{config_.brokerAddress.c_str(), config_.brokerPort}, reporters_(std::move(reporters)) {
		mqttFeature_ = log_->addFeature("MQTT"s);

		DBGLOGFI(log_, mqttFeature_, "Enabled: %d\n", config_.enabled);
		DBGLOGFI(log_, mqttFeature_, "%s:%d\n", config_.brokerAddress.c_str(), config_.brokerPort);
		DBGLOGFI(log_, mqttFeature_, "publish interval %d, keep alive inteval: %d\n", config_.interval, config_.keepAlive);
		DBGLOGFI(log_, mqttFeature_, "clientId '%s', base: '%s'\n", config_.clientId.c_str(), config_.base.c_str());

		if (!config_.enabled) {
			return;
		}

		client_.on("homeassistant/status", [this](char *topic, uint8_t *payload, unsigned int payloadLen) { DBGLOGI(log_, "HomeAssistant '%s' payload: '%s'\n", topic, payload); });

		client_.onConnect([this](uint16_t connCount) {
			DBGLOGFI(log_, mqttFeature_, "Connected to broker %d\n", connCount);
			publishHADiscovery();
		});

		client_.connect(config_.clientId.c_str(), config_.username.empty() ? nullptr : config_.username.c_str(), config_.password.empty() ? nullptr : config_.password.c_str(), (config_.base + "/status").c_str(), 0, false, "off", true);
	}

	void operate() {
		if (!config_.enabled) {
			return;
		}

		if (client_.connected()) {
			publishStatus();
			// publishCounterMetrics();

			for (auto const &reporter : reporters_) {
				reporter->publishStateTopic(*this, config_.interval);
			}
		}
	}

	void loop() {
		if (!config_.enabled) {
			return;
		}

		if (millisDurationPassed(lastOperate_, 1000)) {
			lastOperate_ = millis();
			operate();
		}

		client_.loop();
	}

	void subscribe(std::string_view topic, std::function<void(char *topic, uint8_t *payload, unsigned int payloadLen)> cb) override {
		std::string fullTopic = config_.base;
		fullTopic.append("/"sv).append(topic);

		client_.on(fullTopic.c_str(), cb);
	}

	void publishStateTopic(std::string_view topic, std::string_view payload, bool retained) override {
		std::string fullStateTopic = config_.base;
		fullStateTopic.append("/"sv).append(topic);

		DBGLOGFI(log_, mqttFeature_, "Publishing to topic '%s' payload '%s' (retained: %d)\n", fullStateTopic.c_str(), std::string(payload).c_str(), retained);

		client_.publish(fullStateTopic, payload, retained);
	}

	void publishAutoDiscoveryBinarySensor(std::string_view stateTopic, std::string_view sensorUniqueId, std::string_view sensorFriendlyName, std::string_view jsonValueName, std::string_view deviceClass, std::string_view entityCategory) override {
		viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);
		ss << "{";
		ss << "\"name\": \"" << sensorFriendlyName << "\",";
		ss << "\"uniq_id\": \"" << config_.base << "_" << sensorUniqueId << "\",";
		ss << "\"obj_id\": \"" << config_.base << "_" << sensorUniqueId << "\",";
		ss << "\"stat_t\": \"" << config_.base << "/" << stateTopic << "\",";
		if (!deviceClass.empty()) {
			ss << "\"dev_cla\": \"" << deviceClass << "\",";
		}
		if (!entityCategory.empty()) {
			ss << "\"entity_category\": \"" << entityCategory << "\",";
		}
		ss << "\"val_tpl\": \"{{ \\\"on\\\" if value_json." << jsonValueName << " == \\\"on\\\" else \\\"off\\\" }}\",";
		ss << "\"pl_on\": \"on\",";
		ss << "\"pl_off\": \"off\",";
		ss << "\"dev\": { \"ids\": [ \"" << config_.base << "\" ] }"; // dev
		ss << ", \"avty\": [";
		ss << "{ \"t\": \"" << config_.base << "/" << stateTopic << "\", \"val_tpl\": \"{{ \\\"online\\\" if value_json." << jsonValueName << " is defined else \\\"offline\\\" }}\" },";
		ss << "{ \"t\": \"" << config_.base << "/status\", \"val_tpl\": \"{{ \\\"online\\\" if value == \\\"on\\\" else \\\"offline\\\" }}\" }";
		ss << "], \"avty_mode\": \"all\"";
		ss << "}";

		viewable_stringbuf topicBuf;
		std::ostream topic(&topicBuf);
		topic << "homeassistant/binary_sensor/" << config_.base << "/" << sensorUniqueId << "/config";

		client_.publish(topicBuf.view(), payloadBuf.view(), true);
	}

	void publishAutoDiscoverySensor(std::string_view stateTopic, std::string_view sensorUniqueId, std::string_view sensorFriendlyName, std::string_view jsonValueName, std::string_view valueOperation, std::string_view unit, std::string_view stateClass, std::string_view devClass = {}, std::string_view entityCategory = {}) override {
		viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);
		ss << "{";
		ss << "\"name\": \"" << sensorFriendlyName << "\",";
		ss << "\"uniq_id\": \"" << config_.base << "_" << sensorUniqueId << "\",";
		ss << "\"obj_id\": \"" << config_.base << "_" << sensorUniqueId << "\",";
		ss << "\"stat_t\": \"" << config_.base << "/" << stateTopic << "\",";

		if (!entityCategory.empty()) {
			ss << "\"entity_category\": \"" << entityCategory << "\",";
		}

		if (!unit.empty()) {
			ss << "\"unit_of_meas\": \"" << unit << "\",";
		}
		if (!stateClass.empty()) {
			ss << "\"stat_cla\": \"" << stateClass << "\",";
		}
		if (!devClass.empty()) {
			ss << "\"dev_cla\": \"" << devClass << "\",";
		}
		ss << "\"val_tpl\": \"{{(value_json." << jsonValueName << valueOperation << ") if value_json." << jsonValueName << " is defined else '0'}}\",";
		ss << "\"dev\": { \"ids\": [ \"" << config_.base << "\" ] },"; // dev
		ss << "\"avty\": [";
		ss << "{ \"t\": \"" << config_.base << "/" << stateTopic << "\", \"val_tpl\": \"{{ \\\"online\\\" if value_json." << jsonValueName << " is defined else \\\"offline\\\" }}\" },";
		ss << "{ \"t\": \"" << config_.base << "/status\", \"val_tpl\": \"{{ \\\"online\\\" if value == \\\"on\\\" else \\\"offline\\\" }}\" }";
		ss << "], \"avty_mode\": \"all\"";
		ss << "}";

		viewable_stringbuf topicBuf;
		std::ostream topic(&topicBuf);
		topic << "homeassistant/sensor/" << config_.base << "/" << sensorUniqueId << "/config";

		client_.publish(topicBuf.view(), payloadBuf.view(), true);
	}

	void publishAutoDiscoveryButton(std::string_view commandTopic, std::string_view buttonUniqueId, std::string_view buttonFriendlyName, std::string_view deviceClass, std::string_view entityCategory) override {
		viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);
		ss << "{";
		ss << "\"name\": \"" << buttonFriendlyName << "\",";
		ss << "\"uniq_id\": \"" << config_.base << "_" << buttonUniqueId << "\",";
		ss << "\"obj_id\": \"" << config_.base << "_" << buttonUniqueId << "\",";

		// topic to which Home Assistant will send the command when the button is pressed
		ss << "\"cmd_t\": \"" << config_.base << "/" << commandTopic << "\",";

		// payload that Home Assistant will send (you listen for it on ESP)
		ss << "\"pl_prs\": \"PRESS\",";

		// device_class: "restart", "update", "identify", ...
		if (!deviceClass.empty()) {
			ss << "\"dev_cla\": \"" << deviceClass << "\",";
		}

		if (!entityCategory.empty()) {
			ss << "\"entity_category\": \"" << entityCategory << "\",";
		}

		ss << "\"dev\": { \"ids\": [ \"" << config_.base << "\" ] },";
		ss << "\"avty\": [";
		ss << "{ \"t\": \"" << config_.base << "/status\", \"val_tpl\": \"{{ \\\"online\\\" if value == \\\"on\\\" else \\\"offline\\\" }}\" }";
		ss << "], \"avty_mode\": \"all\"";

		ss << "}";

		viewable_stringbuf topicBuf;
		std::ostream topic(&topicBuf);
		topic << "homeassistant/button/" << config_.base << "/" << buttonUniqueId << "/config";

		client_.publish(topicBuf.view(), payloadBuf.view(), true);
	}

private:
	void publishHADiscovery() {
		DBGLOGI(log_, "publishHADiscovery\n");
		viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);

		ss << "{\"name\": \"" << deviceInfo_.name << "\", \"uniq_id\": \"" << config_.base << "\", \"entity_category\": \"diagnostic\", \"object_id\": \"" << config_.base << "_status\", \"state_topic\": \"" << config_.base << "/status\",\
\"device_class\": \"power\", \"payload_on\": \"on\", \"payload_off\": \"off\", \"dev\": {\"name\": \""
		   << deviceInfo_.name << "\", \"sw\": \"" << deviceInfo_.swVersion << "\", \"mf\": \"" << deviceInfo_.manufacturer << "\", \"mdl\": \"" << deviceInfo_.model << "\", \"ids\": [ \"" << config_.base << "\" ] } }";

		DBGLOGI(log_, "publishHADiscovery payload '%s'\n", std::string(payloadBuf.view()).c_str());

		auto payload = payloadBuf.view();
		std::string topic = "homeassistant/binary_sensor/"s + config_.base + "/status/config"s;

		client_.publish(topic, payload, true);

		for (auto const &reporter : reporters_) {
			reporter->publishHADiscovery(*this);
		}
	}

	void publishStatus() {
		if (lastPublishStatus_ != 0 && !millisDurationPassed(lastPublishStatus_, 1000ul * config_.keepAlive)) {
			auto now = millis();
			unsigned long timeToWait = static_cast<decltype(lastPublishStatus_)>(config_.keepAlive) * 1000ul;
			timeToWait = timeToWait - (now - lastPublishStatus_);
			DBGLOGFD(log_, mqttFeature_, "publishStatus: waiting for keepalive interval (%ds) last: %ld, now: %ld, still need to wait for: %ld ms \n", config_.keepAlive, lastPublishStatus_, now, timeToWait);
			return;
		}

		lastPublishStatus_ = millis();
		DBGLOGFI(log_, mqttFeature_, "publishStatus\n");
		client_.publish(config_.base + "/status"s, "on"sv, true);
	}

private:
	std::shared_ptr<logger::LoggerInterface> log_;
	logger::LoggerInterface::LogFeatureType mqttFeature_;
	ib::mqtt::MqttConfig config_;
	HomeAssistantDeviceInfo deviceInfo_;
	ib::mqtt::IntuibasePubSubClientWrapper client_;
	unsigned long lastPublishRoomData_ = 0;
	unsigned long lastPublishDeviceStatus_ = 0;
	unsigned long lastPublishStatus_ = 0;
	unsigned long lastPublishCounterMetrics_ = 0;

	unsigned long lastOperate_ = 0;

	std::vector<std::shared_ptr<MQTTReporterInterface>> reporters_;
};
}