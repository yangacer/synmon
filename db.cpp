#include "db.hpp"
#include <sstream>
#include <iostream>
#include <boost/filesystem.hpp>

extern "C" {
#include "sqlite3.h"
}
#include "error.hpp"

#define JSON_REF_ENT(Var, Obj, Ent, Type) \
  Obj[Ent] = json::##Type##_t(); \
  json::##Type##_t &Var = mbof(Obj[Ent]).Type();

#define BUSY_WAIT_CNT_ 100


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

inline void sql_mod(error_code &ec, sqlite3 *db, std::string const& stmt)
{
  int code = 0;
  int exe_cnt = 0;

  while(SQLITE_BUSY == (code = sqlite3_exec(db, stmt.c_str(), NULL, NULL, NULL))
        && ++exe_cnt < BUSY_WAIT_CNT_ );

  if(code != SQLITE_OK)
    ec = make_error_code(synmon_error::database_failure);
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
  stmt << "INSERT OR IGNORE INTO File "
    "(local_fullname, remote_fullname, mtime, status) VALUES "
    "(?, ?, " << finfo.mtime << ", "<< (int)finfo.status << ");"
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
  int exe_cnt = 0;
  while ( SQLITE_BUSY == (code = sqlite3_step(pstmt)) && ++exe_cnt < BUSY_WAIT_CNT_ );
  sqlite3_finalize(pstmt);
}

void db::remove(error_code &ec, std::string const &prefix)
{}
/*
int db::version_count(error_code &ec, std::string const &name)
{
  return 0;
}

void db::increment(error_code &ec, std::string const &name)
{
   
}
*/

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
  stmt.str(""); stmt.clear();
  while ( SQLITE_DONE != (code = sqlite3_step(pstmt)) ) {
    if( code == SQLITE_BUSY ) continue;
    if( code != SQLITE_ROW ) break;
    fs::path file((char const*)sqlite3_column_text(pstmt, 0));
    string remote_fullname = (char const*)sqlite3_column_text(pstmt, 1);
    auto old_mtime = sqlite3_column_int64(pstmt, 3);
    file_status status = (file_status)sqlite3_column_int(pstmt, 4),
                new_status = status;
    
    if( fs::exists(file) ) {
      auto cur_mtime = fs::last_write_time(file, ec);
      if(old_mtime != cur_mtime) {
        if( status != conflicted ) {
          new_status = modified;
          stmt << "UPDATE File SET mtime = " << cur_mtime << 
            " WHERE remote_fullname = '" << remote_fullname << "';";
        } else {
          // no change 
        }
      } else { // file is not modified
        switch( status ) {
        case reading:
          new_status = modified;
          break;
        case writing:
          new_status = ok;
          break;
        default:
          // as is
          break;
        } 
      }
    } else {
      new_status = deleted;
    }
    if(status != new_status)
      stmt << 
        "UPDATE File SET status = " << (int)new_status << 
        " WHERE remote_fullname = '" << remote_fullname << "';"
        ;
    JSON_REF_ENT(obj, data, remote_fullname.c_str(), object);
    obj["v"] = sqlite3_column_int64(pstmt, 2);
    obj["s"] = (boost::intmax_t)new_status;
  }
  if(sqlite3_finalize(pstmt))
    ec = make_error_code(synmon_error::database_failure);
  sql_mod(ec, db_, stmt.str());
  return;
}

file_status db::fsm(bool not_exist, bool not_modified, 
                    file_status old_status, file_status new_status)
{
  file_status rt;

  switch(old_status) {
  case ok:
    switch(new_status) {
    case writing:
      if(!not_exist && !not_modified) {
        rt = modified;      
      } else if(not_exist ^ not_modified) {
        rt = writing;
      } else {
        assert(false && "not handled status");
      }
    break;
    case deleted:
      if(!not_exist) {
        if(not_modified) {
          rt = deleted;
        } else {
          rt = ok;
        }
      } else {
        assert(false && "not handled status");
      }
    break;
    default:
      assert(false && "not handled status");
    }
  break;
  case modified:
    if(new_status == reading) {
      if(not_exist && !not_modified) {
        rt = deleted;
      } else if(!not_exist) {
        if(not_modified) {
          rt = reading;
        } else {
          rt = modified;
        }
      } else {
        assert(false && "not handled status");
      }
    } else {
      assert(false && "not handled status");
    }
  break;
  case reading:
    switch(new_status) {
    case ok:
      if(not_exist) {
        rt = deleted;
      } else {
        if(not_modified) {
          rt = ok;
        } else {
          rt = modified;
        }
      }
      break;
    case conflicted:
      if(not_exist)
        rt = deleted;
      else
        rt = conflicted;
      break;
    default:
      assert(false && "not handled status");
    }
  break;
  case writing:
    switch(new_status) {
    case ok: 
      if(!not_exist && !not_modified) {
        rt = modified;
      } else if(not_exist ^ not_modified) {
        rt = ok;
      } else {
        assert(false && "not handled status");
      }
      break;
    case deleted:
      if(not_exist && !not_modified) {
        rt = deleted;
      } else if(!not_exist && not_modified ) {
        rt = ok;
      } else if(!not_exist && !not_modified) {
        rt = modified;
      } else {
        assert(false && "not handled status");
      }
      break;
    default:
      assert(false && "not handled status");
    } 
  break;
  case deleted:
    if(new_status == deleted) {
      if(not_exist) {
        rt = deleted;
      } else {
        rt = modified;
      }
    } else { 
      assert(false && "not handled status");
    }
  break;
  default:
    assert(false && "not handled status");
  }
  return rt;
}

bool db::set_status(
  error_code &ec,
  file_info fi,
  file_status new_status)
{
  using std::cout;
  using std::string;
  std::stringstream stmt;

  assert(0 != fi.remote_fullname && "remote name is missing");
  string const &remote_name = *fi.remote_fullname;

  stmt << "SELECT local_fullname, mtime, status, count(*) AS cnt FROM File WHERE remote_fullname = ? ;";
  int code = SQLITE_DONE;
  sqlite3_stmt *pstmt = 0;
  if(SQLITE_OK == sqlite3_prepare_v2(
      db_, stmt.str().c_str(), -1, &pstmt, NULL))
  {
    sqlite3_bind_text(
      pstmt, 1, 
      remote_name.c_str(), remote_name.size(), 
      SQLITE_STATIC);
    while ( 0 != (code = sqlite3_step(pstmt)) ) {
      if( code == SQLITE_BUSY ) continue;
      if( code != SQLITE_ROW) break;
      
      int cnt = sqlite3_column_int(pstmt, 3);
      bool is_in_db = cnt > 0;
      fs::path file;
      time_t old_mtime = 0;
      time_t cur_mtime = 0;
      file_status old_status = ok;
      if( is_in_db ) {
        file = fs::path((char const*)sqlite3_column_text(pstmt, 0));
      } else {
        assert(0 != fi.local_fullname && "local name is missing");
        file = fs::path(*fi.local_fullname);
      }
      bool file_exists = fs::exists(file);
      old_mtime = sqlite3_column_int64(pstmt, 1);
      cur_mtime = fs::last_write_time(file, ec) ;
      old_status = (file_status)sqlite3_column_int(pstmt, 2);

      assert(is_in_db && "not found record");
      ec.clear(); // dont care ec of last_write_time
      stmt.clear(); stmt.str("");

      file_status result_status = fsm(
        !file_exists, 
        old_mtime == cur_mtime, 
        old_status, 
        new_status);

      if( deleted == result_status ) {
        if(file_exists)
          fs::remove(*fi.local_fullname, ec);
        ec.clear();
        stmt << "DELETE FROM File";
      } else if( ok == result_status || conflicted == result_status) {
        stmt << "UPDATE File SET status = " << (int)result_status << 
          ", version = " << fi.version <<
          ", mtime = " << cur_mtime
          ;
      } else {
        stmt << "UPDATE File SET status = " << (int)result_status ;
        if(cur_mtime != -1) 
          stmt << ", mtime = " << cur_mtime ;
      }
      stmt << " WHERE remote_fullname = '" << remote_name << "';";
      if( result_status != new_status )
        ec = make_error_code(synmon_error::set_status_failure);
      sql_mod(ec, db_, stmt.str());
      //if( code == SQLITE_DONE ) break; 
    }
    //if( SQLITE_DONE != code)
    //  ec = make_error_code(synmon_error::database_failure);
  }
  if( sqlite3_finalize(pstmt) )
    ec = make_error_code(synmon_error::database_failure);
  return ec ? false : true;
}


void db::resync_mtime(error_code &ec, file_info const &fi)
{
  std::stringstream stmt;
  auto cur_mtime = fs::last_write_time(*fi.local_fullname, ec);
  if(ec) 
    return;
  stmt << "UPDATE File SET mtime = " << cur_mtime << 
    " WHERE remote_fullname = '" << *fi.remote_fullname  << "';"
    ;
  sql_mod(ec, db_, stmt.str());
}
