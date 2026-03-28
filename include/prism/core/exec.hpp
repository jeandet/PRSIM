#pragma once

// stdexec's constexpr-exceptions path has a GCC 16 incompatibility
// (incomplete return type completion_signatures<>). Treating this
// header as a system header suppresses the warnings. Remove when
// stdexec is fixed upstream.
#pragma GCC system_header

#pragma push_macro("__cpp_constexpr_exceptions")
#undef __cpp_constexpr_exceptions

#include <stdexec/execution.hpp>
#include <exec/start_detached.hpp>

#pragma pop_macro("__cpp_constexpr_exceptions")
