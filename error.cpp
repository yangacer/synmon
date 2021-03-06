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
  case set_status_failure:
    return "Set status failure";
  case database_failure:
    return "Database failure";
  case broken_version:
    return "Broken version";
  default:
    return "Unknown error";
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
