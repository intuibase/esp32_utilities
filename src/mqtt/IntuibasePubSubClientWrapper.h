#pragma once

#include <PubSubClient.h>
#include <ESPPubSubClientWrapper.h>

#include <string_view>


namespace ib::mqtt {

using namespace std::string_view_literals;

class IntuibasePubSubClientWrapper : public ESPPubSubClientWrapper {
public:
	IntuibasePubSubClientWrapper(const char *domain, uint16_t port = 1883) : ESPPubSubClientWrapper(domain, port) {
	}

	boolean publish(std::string_view topic, std::string_view payload, boolean retained) {
		if (!connected()) {
			return false;
		}

		try {
			uint32_t sizeOfPacket = 2 + topic.length() + payload.length(); // 2 bytes for topic length
			auto variableHeaderLen = howManyBytesWeNeedToEncodeSize(sizeOfPacket);
			uint8_t header[3 + variableHeaderLen];  // 1 byte for header, 2 bytes topic len

			header[0] = MQTTPUBLISH;
			if (retained) {
				header[0] |= 0x01;
			}

			// if (qos == 0) {
			// 	header[0] |= 0x00;
			// } else if (qos == 1) {
			// 	header[0] |= 0x02;
			// } else if (qos == 2) {
			// 	header[0] |= 0x04;
			// }

			header[variableHeaderLen + 1] = (topic.length() >> 8);
			header[variableHeaderLen + 2] = (topic.length() & 0xFF);

			uint8_t pos = 1;
			uint16_t len = sizeOfPacket;
			do {
				uint8_t digit = len  & 127; //digit = len %128
				len >>= 7; //len = len / 128
				if (len > 0) {
					digit |= 0x80;
				}
				header[pos++] = digit;
			} while(len>0);

			_wiFiClient.write(header, 3 + variableHeaderLen);
			_wiFiClient.write(reinterpret_cast<const uint8_t *>(topic.data()), topic.length());
			_wiFiClient.write(reinterpret_cast<const uint8_t *>(payload.data()), payload.length());

			return true;
		} catch (std::out_of_range const &e) {
            //TODO catch in caller
            // DBGLOGMQTT("Unable to publish: %s\n", e.what());
			return false;
		}
		//     HXXXX00topicpayload  -- 00 topic len H header XXXX variable header len (contains 00 + topic + payload lenghts)
	}

protected:
	uint8_t howManyBytesWeNeedToEncodeSize(uint32_t length) {
		if (length < 128) { return 1; }
		if (length < 16384) { return 2; }
		if (length < 2097152) { return 3; }
		if (length < 268435456) { return 4; }
		throw std::out_of_range("payload too long");
	}
};

}