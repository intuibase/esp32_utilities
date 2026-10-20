#pragma once
#include <cstddef>

namespace ib::logger::detail {
struct RecordWriteResult {
	size_t sent = 0;
	unsigned partialWrites = 0;
	bool timedOut = false, error = false;
};

// send returns bytes, -1 for retry, -2 for a terminal error. The deadline is
// owned by the caller and is never renewed after partial progress.
template <class Send, class Wait, class Interrupted, class Expired>
RecordWriteResult writeRecord(const char *data, size_t length, Send send, Wait wait, Interrupted interrupted, Expired expired) {
	RecordWriteResult result;
	while (result.sent < length && !interrupted() && !expired()) {
		const auto count = send(data + result.sent, length - result.sent);
		if (count > 0) {
			if (static_cast<size_t>(count) < length - result.sent)
				++result.partialWrites;
			result.sent += count;
		} else if (count == -1) {
			if (!wait())
				break;
		} else {
			result.error = true;
			break;
		}
	}
	if (result.sent < length && !interrupted()) {
		result.timedOut = expired();
		result.error = !result.timedOut;
	}
	return result;
}
} // namespace ib::logger::detail
