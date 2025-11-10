
#pragma once
#include <ostream>

namespace ib {

class StatusReportingInterface {
public:
	virtual ~StatusReportingInterface() = default;
	virtual void getStatus(std::ostream &ss) const = 0;
};

}