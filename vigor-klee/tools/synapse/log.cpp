#include "log.h"

namespace synapse {

Log Log::log() { return Log(Log::LOG); }
Log Log::dbg() { return Log(Log::DEBUG) << "DEBUG:   "; }
Log Log::wrn() { return Log(Log::WARNING) << "WARNING: "; }
Log Log::err() { return Log(Log::ERROR) << "ERROR:   "; }

Log::Level Log::MINIMUM_LOG_LEVEL = Log::WARNING;

} // namespace synapse
