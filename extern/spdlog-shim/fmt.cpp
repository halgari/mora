// fmt compiled-library implementation.
//
// format-inl.h provides the template definitions for locale-aware helpers;
// we just need explicit instantiations for char so the linker can find them.

#include "fmt/format-inl.h"

namespace fmt {
inline namespace v12 {
namespace detail {

template thousands_sep_result<char>    thousands_sep_impl<char>(locale_ref);
template char                          decimal_point_impl<char>(locale_ref);

} // namespace detail
} // namespace v12
} // namespace fmt
