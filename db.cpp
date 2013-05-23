#include "db.hpp"
#include <sstream>
#include <iostream>
#include <boost/filesystem.hpp>
#ifdef _WIN32
#ifdef _DEBUG
#pragma comment(linker, "/nodefaultlib:libcmtd.lib")
#else
#pragma comment(linker, "/nodefaultlib:libcmt.lib")
#endif
#endif
extern "C" {
#include "sqlite3.h"
}

namespace fs = boost::filesystem;

void sql_trace(void* db, char const *msg)
{
  int code = sqlite3_errcode((sqlite3*)db);
  if(code) {
    std::cerr << "[^sql-err] " << 
      sqlite3_errstr(code) << " -> " <<
      msg << "\n"
      ;
  }
}

db::db(std::string const &prefix)
{
  fs::path p = fs::system_complete(fs::path(prefix));
  p /= "synmon.db";
  if(sqlite3_open(p.string().c_str(), &db_))
    throw std::runtime_error("Open db failed");
  sqlite3_trace(db_, &sql_trace, (void*)db_);
}

db::~db()
{
  sqlite3_close(db_);
}

void db::add(error_code &ec, file_info const &finfo)
{
  std::stringstream stmt;
  stmt << "INSERT INTO File "
    "(local_fullname, remote_fullname) VALUES "
    "(?, ?);"
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
  sqlite3_finalize(pstmt);
}

void db::remove(error_code &ec, std::string const &prefix)
{}

int db::version_count(error_code &ec, std::string const &name)
{
  return 0;
}

void db::increment(error_code &ec, std::string const &name)
{
   
}

void db::check_changes(error_code &ec)
{
  using std::cout;

  std::stringstream stmt;
  stmt << "SELECT local_fullname, remote_fullname, version, mtime  FROM File;";
  int code = SQLITE_DONE;
  sqlite3_stmt *pstmt = 0;
  if(0 != (code = sqlite3_prepare_v2(
        db_, stmt.str().c_str(), -1, &pstmt, NULL))) 
    return;
  while ( SQLITE_DONE != (code = sqlite3_step(pstmt)) ) {
    if( code == SQLITE_BUSY ) continue;
    if( code != SQLITE_ROW ) break;
    fs::path file((char const*)sqlite3_column_text(pstmt, 0));
    auto old_mtime = sqlite3_column_int64(pstmt, 3);
    auto cur_mtime = fs::last_write_time(file, ec);
    if( old_mtime != cur_mtime )
      cout << "(c) ";
    else
      cout << "(u) ";
    cout << sqlite3_column_text(pstmt, 1) << "\n";
  }
  sqlite3_finalize(pstmt);
}
