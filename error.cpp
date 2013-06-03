#include "error.hpp"
#include <string>

namespace synmon_error {

class error_category : public boost::system::error_category
{
public:
  virtual char const * name() const 
  {
    return "synmon_error";
  }
  virtual std::string message(int ev) const;
};

std::string error_category::message(int ev) const 
{
  switch (ev) {
  case update_file_status_failure:
    return "Update File Status Failure";
  default:
    return "Unknown basis error";
  }
}

boost::system::error_code make_error_code(error e)
{
  return boost::system::error_code((int)e, synmon_error_category());
}

boost::system::error_condition make_error_condition(error e)
{
  return boost::system::error_condition((int)e, synmon_error_category());
}

boost::system::error_category const &synmon_error_category()
{
  static error_category inst_;
  return inst_;
}


} // namespace synmon_error
