// clog.h - lightweight logging for U++ applications with timer support
#pragma once

#include <chrono>
#include <cstdio>
#include <sstream>
#include <string>

#ifndef CLOG_ENABLED
#ifndef NDEBUG
#define CLOG_ENABLED 1
#else
#define CLOG_ENABLED 0
#endif
#endif

namespace Clog {

// Simple enable flag - no complex statics
inline bool& IsEnabled()
{
	static bool enabled = true;
	return enabled;
}

inline void DisableLogging() { IsEnabled() = false; }

// RAII sink - simplified for U++ compatibility
struct Sink {
#if CLOG_ENABLED
	std::ostringstream* buffer_;

	Sink()
		: buffer_(IsEnabled() ? new std::ostringstream() : nullptr)
	{
	}

	~Sink()
	{
		if(!buffer_)
			return;

		if(IsEnabled()) {
			std::string msg = buffer_->str();
			if(!msg.empty()) {
				std::fprintf(stderr, "%s\n", msg.c_str());
				std::fflush(stderr);
			}
		}
		delete buffer_;
	}

	template <class T>
	Sink& operator<<(const T& v)
	{
		if(buffer_ && IsEnabled()) {
			(*buffer_) << v;
		}
		return *this;
	}
#else
	template <class T>
	Sink& operator<<(const T&)
	{
		return *this;
	}
#endif
};

// Timer sink for performance measurement
struct TimerSink {
#if CLOG_ENABLED
	std::ostringstream* buffer_;
	std::chrono::high_resolution_clock::time_point start_;

	TimerSink()
		: buffer_(nullptr)
		, start_(std::chrono::high_resolution_clock::now())
	{
		if(IsEnabled()) {
			buffer_ = new std::ostringstream();
		}
	}

	~TimerSink()
	{
		if(!buffer_)
			return;

		if(IsEnabled()) {
			auto end = std::chrono::high_resolution_clock::now();
			auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start_);

			std::string msg = buffer_->str();
			if(!msg.empty()) {
				std::fprintf(stderr, "[%lld us] %s\n", static_cast<long long>(duration.count()),
				             msg.c_str());
				std::fflush(stderr);
			}
		}
		delete buffer_;
	}

	template <class T>
	TimerSink& operator<<(const T& v)
	{
		if(buffer_ && IsEnabled()) {
			(*buffer_) << v;
		}
		return *this;
	}
#else
	template <class T>
	TimerSink& operator<<(const T&)
	{
		return *this;
	}
#endif
};

} // namespace Clog

#if CLOG_ENABLED
#define CLOG ::Clog::Sink()
#define CLOG_TR ::Clog::TimerSink()
#else
#define CLOG ::Clog::Sink()
#define CLOG_TR ::Clog::TimerSink()
#endif