#include "db.hpp"
#include <sstream>
#include <iostream>
#include <boost/filesystem.hpp>
#include "sqlite3.h"

namespace fs = boost::filesystem;

void sql_trace(void*, char const *msg)
{
  //std::cerr << "err: " << msg << "\n";
}

db::db(std::string const &prefix)
{
  fs::path p = fs::system_complete(fs::path(prefix));
  p /= "synmon.db";
  std::cout << p.string() << "\n";
  if(sqlite3_open(p.string().c_str(), &db_))
    throw std::runtime_error("Open db failed");
  //sqlite3_trace(db_, &sql_trace, NULL);
}

db::~db()
{
  sqlite3_close(db_);
}

void db::add(error_code &ec, file_info const &finfo)
{
  std::stringstream stmt;
  stmt << "INSERT INTO File "
    "(local_fullname, remote_fullname, mtime) VALUES "
    "(?, ?, " << finfo.mtime << ");"
    ;
  // TODO error handling/reporting
  int code = SQLITE_DONE;
  sqlite3_stmt *pstmt = 0;
  if(0 != (code = sqlite3_prepare_v2(
        db_, stmt.str().c_str(), -1, &pstmt, NULL))) 
    return;
  sqlite3_bind_text(
    pstmt, 1, 
    finfo.local_fullname->c_str(), 
    finfo.local_fullname->size(), SQLITE_STATIC); 
  sqlite3_bind_text(
    pstmt, 2, 
    finfo.remote_fullname->c_str(), 
    finfo.remote_fullname->size(), SQLITE_STATIC);
  while ( SQLITE_BUSY == (code = sqlite3_step(pstmt)) );
  if( SQLITE_DONE != code ) {
    std::cerr << sqlite3_errstr(code) << "\n";
  }
  sqlite3_finalize(pstmt);
}

void db::remove(error_code &ec, std::string const &prefix)
{}

int db::version_count(error_code &ec, std::string const &name)
{}

void db::increment(error_code &ec, std::string const &name)
{
   
}

