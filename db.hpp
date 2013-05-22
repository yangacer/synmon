#ifndef SYNMON_DB_HPP_
#define SYNMON_DB_HPP_

#include <string>
#include <boost/system/error_code.hpp>

using boost::system::error_code;

struct sqlite3;

struct file_info
{
  std::string const *local_fullname;
  std::string const *remote_fullname;
  int version;
  time_t mtime;
};

class db 
{
public:
  db(std::string const &prefix);
  ~db();
  void add(error_code &ec, file_info const &finfo);
  void remove(error_code &ec, std::string const &prefix);
  int version_count(error_code &ec, std::string const &name);
  void increment(error_code &ec, std::string const &name);
private:
  sqlite3 *db_;
};

#endif
