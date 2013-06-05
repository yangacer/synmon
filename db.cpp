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
#include "error.hpp"

#define JSON_REF_ENT(Var, Obj, Ent, Type) \
  Obj[Ent] = json::##Type##_t(); \
  json::##Type##_t &Var = mbof(Obj[Ent]).Type();

namespace fs = boost::filesystem;
using namespace synmon_error;

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

std::string db::get_remote_name(error_code &ec, std::string const &local_name) const
{
  using std::string;
  std::stringstream stmt;
  string rt_str;

  stmt << "SELECT remote_fullname FROM File WHERE local_fullname = ?;";
  int code = SQLITE_DONE;
  sqlite3_stmt *pstmt = 0;
  if(SQLITE_OK == sqlite3_prepare_v2(
      db_, stmt.str().c_str(), -1, &pstmt, NULL))
  {
    sqlite3_bind_text(pstmt, 1, local_name.c_str(), local_name.size(), SQLITE_STATIC);
    while ( SQLITE_DONE != (code = sqlite3_step(pstmt)) ) {
      if( code == SQLITE_BUSY ) continue;
      if( code != SQLITE_ROW ) break;
      rt_str = (char const*)sqlite3_column_text(pstmt, 0);
    }
    if( SQLITE_DONE != code)
      ec = make_error_code(synmon_error::database_failure);
  }
  sqlite3_finalize(pstmt);
  if(ec) return string();
  return rt_str;
}

std::string db::get_local_name(error_code &ec, std::string const &remote_name) const
{
  using std::string;
  std::stringstream stmt;
  string rt_str;

  stmt << "SELECT local_fullname FROM File WHERE remote_fullname = ?;";
  int code = SQLITE_DONE;
  sqlite3_stmt *pstmt = 0;
  if(SQLITE_OK == sqlite3_prepare_v2(
      db_, stmt.str().c_str(), -1, &pstmt, NULL))
  {
    sqlite3_bind_text(pstmt, 1, remote_name.c_str(), remote_name.size(), SQLITE_STATIC);
    while ( SQLITE_DONE != (code = sqlite3_step(pstmt)) ) {
      if( code == SQLITE_BUSY ) continue;
      if( code != SQLITE_ROW ) break;
      rt_str = (char const*)sqlite3_column_text(pstmt, 0);    
    }
    if( SQLITE_DONE != code)
      ec = make_error_code(synmon_error::database_failure);
  }
  sqlite3_finalize(pstmt);
  if(ec) return string();
  return rt_str;
}

void db::check_changes(error_code &ec, json::object_t &rt_obj)
{
  using std::cout;
  using std::string;
  std::stringstream stmt;
  
  JSON_REF_ENT(data, rt_obj, "file", object);

  stmt << "SELECT local_fullname, remote_fullname, version, mtime, status FROM File;";
  int code = SQLITE_DONE;
  sqlite3_stmt *pstmt = 0;
  if(0 != (code = sqlite3_prepare_v2(
        db_, stmt.str().c_str(), -1, &pstmt, NULL))) 
    return;
  while ( SQLITE_DONE != (code = sqlite3_step(pstmt)) ) {
    if( code == SQLITE_BUSY ) continue;
    if( code != SQLITE_ROW ) break;
    fs::path file((char const*)sqlite3_column_text(pstmt, 0));
    string remote_fullname = (char const*)sqlite3_column_text(pstmt, 1);
    auto old_mtime = sqlite3_column_int64(pstmt, 3);
    file_status status = (file_status)sqlite3_column_int(pstmt, 4);
    // precond assertion
    assert( 
      status != reading 
      && status != writing
      &&  "Precondition of check_changes does not hold" );
    if( fs::exists(file) ) {
      auto cur_mtime = fs::last_write_time(file, ec);
      if(old_mtime != cur_mtime) {
        status = modified;
        stmt.clear(); stmt.str("");
        stmt << "UPDATE File SET mtime = " << cur_mtime << 
          ", status = " << (int)status << 
          " WHERE remote_fullname = '" << remote_fullname << "';"
          ;
        int exe_cnt = 0;
        while(SQLITE_BUSY == (
            code = sqlite3_exec(db_, stmt.str().c_str(), NULL, NULL, NULL))) 
        {
          if(++exe_cnt > 100) break;
        }
        if( code ) break;
      }
    } else if(status != deleted) {
      status = deleted;
      stmt.clear(); stmt.str("");
      stmt << "UPDATE File SET status = " << (int)status << 
        " WHERE remote_fullname = '" << remote_fullname << "';"
        ;
      int exe_cnt = 0;
      while(SQLITE_BUSY == (
          code = sqlite3_exec(db_, stmt.str().c_str(), NULL, NULL, NULL))) 
      {
        if(++exe_cnt > 100) break;
      }
      if( code ) break;
    }
    JSON_REF_ENT(obj, data, remote_fullname.c_str(), object);
    obj["v"] = sqlite3_column_int64(pstmt, 2);
    obj["s"] = (boost::intmax_t)status;
  }
  if(sqlite3_finalize(pstmt))
    ec = make_error_code(synmon_error::database_failure);
  return;
}

bool db::set_status(
  error_code &ec, 
  std::string const &remote_name, 
  file_status new_status,
  std::string const &extra_sql)
{
  using std::cout;
  using std::string;
  std::stringstream stmt;

  stmt << "SELECT local_fullname, mtime, status FROM File WHERE remote_fullname = ? ;";
  int code = SQLITE_DONE;
  sqlite3_stmt *pstmt = 0;
  if(SQLITE_OK == sqlite3_prepare_v2(
      db_, stmt.str().c_str(), -1, &pstmt, NULL))
  {
    sqlite3_bind_text(
      pstmt, 1, 
      remote_name.c_str(), remote_name.size(), 
      SQLITE_STATIC);
    while ( SQLITE_DONE != (code = sqlite3_step(pstmt)) ) {
      if( code == SQLITE_BUSY ) continue;
      if( code != SQLITE_ROW ) break;
      fs::path file((char const*)sqlite3_column_text(pstmt, 0));
      auto old_mtime = sqlite3_column_int64(pstmt, 1);
      auto cur_mtime = fs::last_write_time(file, ec);
      auto old_status = (file_status)sqlite3_column_int(pstmt, 2);
      stmt.clear(); stmt.str("");
      if(fs::exists(file)) {
        // skip upload this file since it has subsequent modification
        if( old_mtime != cur_mtime )
          stmt << "DELETE FROM File ";
        else { 
          stmt << "UPDATE File SET status = " << (int)new_status;
          if( old_status == file_status::reading )
            stmt << " , version = version + 1";
        }
        // apply update
        //  ec = make_error_code(synmon_error::database_failure);
        // if( old_mtime != cur_mtime )
        //  ec = make_error_code(synmon_error::update_file_status_failure);
      } else {
        if( old_status == deleted && new_status == old_status ) {
          stmt << "DELETE FROM File ";
        } else {
          stmt << "UPDATE File SET status = " << (int)deleted ;
        }
      }
      stmt << " WHERE remote_fullname = '" << remote_name << "'";
      int exe_cnt = 0;
      while(SQLITE_BUSY == ( 
          code = sqlite3_exec(db_, stmt.str().c_str(), NULL, NULL, NULL)))
      {
        if(++exe_cnt > 100) break;
      }
      if( code ) break; 
    }
    //if( SQLITE_DONE != code)
    //  ec = make_error_code(synmon_error::database_failure);
  }
  if( sqlite3_finalize(pstmt) )
    ec = make_error_code(synmon_error::database_failure);
  return ec ? false : true;
}

