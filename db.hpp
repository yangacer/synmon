#ifndef SYNMON_DB_HPP_
#define SYNMON_DB_HPP_

#include <string>
#include <vector>
#include <boost/system/error_code.hpp>
#include "json/json.hpp"

using boost::system::error_code;
namespace json = yangacer::json;

struct sqlite3;

struct file_info
{
  std::string const *local_fullname;
  std::string const *remote_fullname;
  int version;
  time_t mtime;
};

enum file_status {
  ok = 0,
  modified = 1, 
  reading = 2, 
  writing = 3,
  conflicted = 4,
  deleted = 5
};

class db 
{
public:
  db(std::string const &prefix);
  ~db();
  void add(error_code &ec, file_info const &finfo);
  void remove(error_code &ec, std::string const &prefix);
  bool set_status(
    error_code &ec, 
    std::string const &remote_name,
    file_status status, 
    std::string const& etrax_sql = "");

  std::string get_remote_name(error_code &ec, std::string const &local_name) const;
  std::string get_local_name(error_code &ec, std::string const &remote_name) const;

  int version_count(error_code &ec, std::string const &name);
  void increment(error_code &ec, std::string const &name);
  /** @precond No intermediate file, i.e. every files' status are either 
   * ok or conflicted.
   */
  void check_changes(error_code &ec, json::object_t &out);
private:
  sqlite3 *db_;
};

#endif
