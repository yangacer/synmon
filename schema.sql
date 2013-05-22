PRAGMA journal_mode = WAL;

CREATE TABLE File ( 
  local_fullname TEXT UNIQUE, 
  remote_fullname TEXT UNIQUE,
  version INTEGER DEFAULT 0,
  mtime INTEGER
  );

