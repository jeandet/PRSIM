#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <mutex>

namespace prism::core {

using ErrorHandler = std::function<void(std::exception_ptr)>;

namespace error_hub_detail {
inline std::mutex error_handler_mutex;
inline ErrorHandler error_handler;

inline void default_error_handler(std::exception_ptr e) {
    try {
        std::rethrow_exception(e);
    } catch (const std::exception& ex) {
        std::cerr << "[prism] unhandled exception in posted callback: " << ex.what() << '\n';
    } catch (...) {
        std::cerr << "[prism] unhandled exception in posted callback: <non-std exception>\n";
    }
}
} // namespace error_hub_detail

// Any thread. Pass nullptr to restore the default (print-to-stderr) handler.
inline void set_unhandled_error_handler(ErrorHandler h) {
    std::lock_guard<std::mutex> lock(error_hub_detail::error_handler_mutex);
    error_hub_detail::error_handler = std::move(h);
}

inline void report_unhandled_error(std::exception_ptr e) {
    ErrorHandler h;
    {
        std::lock_guard<std::mutex> lock(error_hub_detail::error_handler_mutex);
        h = error_hub_detail::error_handler;
    }
    if (!h) {
        error_hub_detail::default_error_handler(e);
        return;
    }
    try {
        h(e);
    } catch (...) {
        error_hub_detail::default_error_handler(e);
    }
}

} // namespace prism::core
