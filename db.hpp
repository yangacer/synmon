#ifndef SYNMON_DB_HPP_
#define SYNMON_DB_HPP_

#include <string>
#include <boost/system/error_code.hpp>

using boost::system::error_code;

struct sqlite3;

class db 
{
public:
  db(std::string const &prefix);
  ~db();
  void increment(error_code &ec, std::string const *name, int number);
  void remove(error_code &ec, std::string const &prefix);
  int version_count(error_code &ec, std::string const &name);
private:
  sqlite3 *db_;
};

#endif
