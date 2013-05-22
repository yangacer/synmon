#include "db.hpp"
#include <boost/filesystem.hpp>
#include "sqlite3.h"

namespace fs = boost::filesystem;

db::db(std::string const &prefix)
{
  fs::path p = fs::system_complete(fs::path(prefix));
  p += "synmon.db";
  if(SQLITE_OK != sqlite3_open(p.string().c_str(), &db_))
    throw std::runtime_error("Open db failed");
}

db::~db()
{
  sqlite3_close(db_);
}

void db::increment(error_code &ec, std::string const *name, int number)
{
  
}

void db::remove(error_code &ec, std::string const &prefix)
{}

int db::version_count(error_code &ec, std::string const &name)
{}
