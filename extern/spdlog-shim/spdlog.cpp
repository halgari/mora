// spdlog compiled-library implementation.
//
// SPDLOG_COMPILED_LIB makes the -inl.h implementations opt-in; this TU
// opts in once and also explicitly instantiates the template classes for
// std::mutex (the mutex flavor SKSE's logger actually uses).

#include "spdlog/spdlog.h"
#include "spdlog/async.h"

// Non-template implementations.
#include "spdlog/async_logger-inl.h"
#include "spdlog/common-inl.h"
#include "spdlog/details/backtracer-inl.h"
#include "spdlog/details/file_helper-inl.h"
#include "spdlog/details/log_msg-inl.h"
#include "spdlog/details/log_msg_buffer-inl.h"
#include "spdlog/details/os-inl.h"
#include "spdlog/details/periodic_worker-inl.h"
#include "spdlog/details/registry-inl.h"
#include "spdlog/details/thread_pool-inl.h"
#include "spdlog/logger-inl.h"
#include "spdlog/pattern_formatter-inl.h"
#include "spdlog/sinks/ansicolor_sink-inl.h"
#include "spdlog/sinks/base_sink-inl.h"
#include "spdlog/sinks/basic_file_sink-inl.h"
#include "spdlog/sinks/rotating_file_sink-inl.h"
#include "spdlog/sinks/sink-inl.h"
#include "spdlog/sinks/stdout_color_sinks-inl.h"
#include "spdlog/sinks/stdout_sinks-inl.h"
#include "spdlog/sinks/wincolor_sink-inl.h"
#include "spdlog/spdlog-inl.h"

#include <mutex>
#include "spdlog/details/null_mutex.h"

// Explicit template instantiations. SKSE's logger constructs these with the
// std::mutex flavor; null_mutex is provided for symmetry with upstream
// spdlog's source file.
template class spdlog::sinks::base_sink<std::mutex>;
template class spdlog::sinks::base_sink<spdlog::details::null_mutex>;
template class spdlog::sinks::basic_file_sink<std::mutex>;
template class spdlog::sinks::basic_file_sink<spdlog::details::null_mutex>;
