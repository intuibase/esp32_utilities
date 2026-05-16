// Compile Logger.cpp for native tests (avoids Arduino-dependent sinks)
#include <mutex>
#include <shared_mutex>
#include <sys/time.h>
#include "logger/Logger.cpp"
