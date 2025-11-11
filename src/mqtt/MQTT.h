#pragma once

#include <ostream>
#include <string_view>
#include <vector>

#include <logger/LoggerInterface.h>
#include "TimeHelpers.h"
#include "config.h"
#include "mqtt/IntuibasePubSubClientWrapper.h"
#include "mqtt/MQTTReporterInterface.h"
#include "mqtt/MqttConfig.h"
#include "viewable_stringbuf.h"

using namespace std::string_view_literals;
using namespace std::string_literals;
namespace ib::mqtt {

class MQTT {
public:
	using getCounter_t = std::function<void(std::ostream &)>;

	struct HomeAssistantDeviceInfo {
		std::string_view name;
		std::string_view model;
		std::string_view manufacturer;
		std::string_view swVersion;
	};

	MQTT(std::shared_ptr<logger::LoggerInterface> log, ib::mqtt::MqttConfig config, HomeAssistantDeviceInfo deviceInfo, getCounter_t getCounter, std::vector<std::shared_ptr<MQTTReporterInterface>> reporters) : log_(std::move(log)), config_(std::move(config)), deviceInfo_(std::move(deviceInfo)), client_{config_.brokerAddress.c_str(), config_.brokerPort}, getCounter_{std::move(getCounter)}, reporters_(std::move(reporters)) {
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

			auto publish = [this] (std::string_view topic, std::string_view payload, bool retained) {
				client_.publish(topic, payload, retained);
			};

			for (auto const &reporter : reporters_) {
				reporter->publishStateTopic(publish, config_.interval);
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

private:
	void publishHADiscovery() {
		DBGLOGI(log_, "publishHADiscovery\n");
		viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);

		ss << "{\"name\": \"" << deviceInfo_.name << "\", \"uniq_id\": \"" << config_.base << "\", \"object_id\": \"" << config_.base << "_status\", \"state_topic\": \"" << config_.base << "/status\",\
\"device_class\": \"power\", \"payload_on\": \"on\", \"payload_off\": \"off\", \"dev\": {\"name\": \""
		   << deviceInfo_.name << "\", \"sw\": \"" << deviceInfo_.swVersion << "\", \"mf\": \"" << deviceInfo_.manufacturer << "\", \"mdl\": \"" << deviceInfo_.model << "\", \"ids\": [ \"" << config_.base << "\" ] } }";

		DBGLOGI(log_, "publishHADiscovery payload '%s'\n", std::string(payloadBuf.view()).c_str());

		auto payload = payloadBuf.view();
		std::string topic = "homeassistant/binary_sensor/"s + config_.base + "/status/config"s;

		client_.publish(topic, payload, true);

		auto publish = [this](std::string_view stateTopic, std::string_view sensorUniqueId, std::string_view sensorFriendlyName, std::string_view jsonValueName, std::string_view valueOperation, std::string_view unit, std::string_view stateClass, std::string_view devClass) {
			publishSensor(stateTopic, sensorUniqueId, sensorFriendlyName, jsonValueName, valueOperation, unit, stateClass, devClass);
		};

		for (auto const &reporter : reporters_) {
			reporter->publishHADiscovery(publish);
		}
	}

	// void publishCounterMetrics() {
	// 	auto now = millis();
	// 	if (lastPublishCounterMetrics_ != 0 && !millisDurationPassed(lastPublishCounterMetrics_, 1000ul * config_.interval)) {
	// 		decltype(now) timeToWait = static_cast<decltype(lastPublishCounterMetrics_)>(config_.interval) * 1000ul;
	// 		timeToWait = timeToWait - (now - lastPublishCounterMetrics_);
	// 		DBGLOGI(log_, "publishCounterMetrics: waiting for publish interval (%ds) last: %ld, now: %ld, still need to wait for: %ld ms \n", config_.interval, lastPublishCounterMetrics_, now, timeToWait);
	// 		return;
	// 	}

	// 	lastPublishCounterMetrics_ = now;

	// 	viewable_stringbuf payloadBuf;
	// 	std::ostream ss(&payloadBuf);

	// 	getCounter_(ss);

	// 	DBGLOGI(log_, "publishCounterMetrics %zu\n", payloadBuf.view().length());

	// 	client_.publish("ib_water_meter/water_usage"sv, payloadBuf.view(), false);
	// }

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

	void publishSensor(std::string_view stateTopic, std::string_view sensorUniqueId, std::string_view sensorFriendlyName, std::string_view jsonValueName, std::string_view valueOperation, std::string_view unit, std::string_view stateClass, std::string_view devClass = {}) {
		viewable_stringbuf payloadBuf;
		std::ostream ss(&payloadBuf);
		ss << "{";
		ss << "\"name\": \"" << sensorFriendlyName << "\",";
		ss << "\"uniq_id\": \"" << sensorUniqueId << "\",";
		ss << "\"obj_id\": \"" << sensorUniqueId << "\",";
		ss << "\"stat_t\": \"" << config_.base << "/" << stateTopic << "\",";
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

	getCounter_t getCounter_;

	std::vector<std::shared_ptr<MQTTReporterInterface>> reporters_;
};

}