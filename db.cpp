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

namespace fs = boost::filesystem;
using namespace synmon_error;

void sql_trace(void* db, char const *msg)
{
  int code = sqlite3_errcode((sqlite3*)db);
  //if(code) {
    std::cerr << "[^sql-err] " << 
      sqlite3_errstr(code) << " -> " <<
      msg << "\n"
      ;
  //}
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
  // FIXME may block eternally
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
  std::string const &local_name,
  std::string const &remote_name, 
  file_status new_status)
{
  using std::cout;
  using std::string;
  std::stringstream stmt;

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
        file = fs::path(local_name);
      }
      bool file_exists = fs::exists(file);
      old_mtime = sqlite3_column_int64(pstmt, 1);
      cur_mtime = file_exists ? fs::last_write_time(file, ec) : 0;
      old_status = (file_status)sqlite3_column_int(pstmt, 2);

      ec.clear(); // dont care ec of last_write_time
      stmt.clear(); stmt.str("");
      { // ---- determine state transition ----
        switch(new_status) {
        case ok:
          assert(is_in_db && "ok case");
          if(old_status == reading) {
            stmt << "UPDATE File SET mtime = " << cur_mtime <<
              ", version = version + 1"
              ;
            if(true == file_exists) {
              if( old_mtime != cur_mtime ) {
                stmt << ", status = " << (int)modified;
                ec = make_error_code(synmon_error::broken_version);
              } else {
                stmt << ", status = " << (int)ok;
              }
            } else {
              stmt << ", status = " << (int)deleted;
              ec = make_error_code(synmon_error::broken_version);
            }
          } else if(old_status == writing) {
            // FIXME Version should be sync with server's ver_num
            stmt << "UPDATE File SET mtime = " << std::time(NULL) <<
              ", version = version + 1"
              ;
            if( old_mtime != cur_mtime ) {
              stmt << ", status = " << (int)modified;
              ec = make_error_code(synmon_error::broken_version);
            } else {
              stmt << ", status = " << (int)ok;
            }
          } else {
            assert(false && "ok can only transitted from reading and writing");
          }
          break; // eof ok case
        case modified:
          assert(is_in_db && "modified case");
          assert(false && "modified state should not be used externally");
          break;
        case reading:
          assert(is_in_db && "reading case");
          if(old_status == modified) {
            if( true == file_exists ) {
              if( old_mtime != cur_mtime ) {
                stmt << "UPDATE File SET mtime = " << cur_mtime ;
                ec = make_error_code(synmon_error::set_status_failure);
              } else {
                stmt << "UPDATE File SET status = " << (int)reading ;
              }
            } else {
              stmt << "UPDATE File SET status = " << (int)deleted ;
              ec = make_error_code(synmon_error::set_status_failure);
            }
          } else {
            assert(false && "reading state can only be transited from modified");
          }
          break; // eof reading case
        case writing:
          if(old_status == ok) {
            if(file_exists == false) {
              if( is_in_db ) {
                stmt << "UPDATE File SET status = " << (int)deleted ;
                ec = make_error_code(synmon_error::set_status_failure);
              } else {
                file_info fi;
                fi.local_fullname = &local_name;
                fi.remote_fullname = &remote_name;
                add(ec, fi);
                stmt << "UPDATE File SET status = " << (int)writing ;
              }
            } else { // file does not exist
              if( is_in_db ) {
                if( old_mtime == cur_mtime ) {
                  stmt << "UPDATE File SET status = " << (int)writing ;
                } else {
                  stmt << "UPDATE File SET status = " << (int)modified ;
                  ec = make_error_code(synmon_error::set_status_failure);
                }
              } else {
                file_info fi;
                fi.local_fullname = &local_name;
                fi.remote_fullname = &remote_name;
                add(ec, fi);
              }
            }
          } else {
            assert(false && "writing state can only be transited from ok");
          }
          break; // eof writing case
        case conflicted:
          if(old_status == modified) {
            
          } else {
            assert(false && "conflicted state can only be transited from modified");
          }
          break; // eof conflicted case
        case deleted:
          assert(is_in_db && "deleted case");
          if(old_status == deleted) {
            if(false == file_exists) {
              stmt << "DELETE FROM File ";
            } else {
              stmt << "UPDATE File SET status = " << (int)modified;
            }
          } else {
            assert(false && "deleted state can only be transited from deleted");
          }
          break; // eof deleted case
        }
        stmt << " WHERE remote_fullname = '" << remote_name << "';";
      } // ---- eof determine state transition ----

      int exe_cnt = 0;
      int inner_code = 0;
      while(SQLITE_BUSY == ( 
          inner_code = sqlite3_exec(db_, stmt.str().c_str(), NULL, NULL, NULL)))
      {
        if(++exe_cnt > 100) break;
      }
      if( inner_code ) {
        ec = make_error_code(synmon_error::database_failure);
        break;
      }
      //if( code == SQLITE_DONE ) break; 
    }
    //if( SQLITE_DONE != code)
    //  ec = make_error_code(synmon_error::database_failure);
  }
  if( sqlite3_finalize(pstmt) )
    ec = make_error_code(synmon_error::database_failure);
  return ec ? false : true;
}

