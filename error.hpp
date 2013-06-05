#ifndef SYNMON_ERROR_HPP_
#define SYNMON_ERROR_HPP_

#include <boost/type_traits.hpp>
#include <boost/system/error_code.hpp>

namespace synmon_error {

enum error 
{
  update_file_status_failure = 1,
  filesystem_error = 2,
  database_failure = 3
};

boost::system::error_category const &synmon_error_category();
boost::system::error_code make_error_code(error e);
boost::system::error_condition make_error_condition(error e);


} // namespace synmon_error

namespace boost {
namespace system  {
  template<>
  struct is_error_code_enum<synmon_error::error> 
  : public true_type {};
}
}

#endif
