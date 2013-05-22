CREATE TABLE File ( 
  local_fullname TEXT UNIQUE, 
  remote_partial_name TEXT UNIQUE,
  version INTEGER DEFAULT 0,
  mtime INTEGER 
  );

