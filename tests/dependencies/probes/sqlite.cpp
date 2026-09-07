#include "probe_common.hpp"
#include <sqlite3.h>
#include <windows.h>
#include <cstring>
#include <stdexcept>
#include <string>
static std::string scalar(sqlite3* db, const char* sql) {
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
  std::string value;
  if (sqlite3_step(stmt) == SQLITE_ROW) value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
  sqlite3_finalize(stmt); return value;
}
int main() {
  PROBE_CHECK(sqlite3_libversion_number() == 3051003 && sqlite3_threadsafe() == 1);
  PROBE_CHECK(std::strstr(sqlite3_sourceid(), "737ae4a34738ffa0c3ff7f9bb18df914dd1cad163f28fd6b6e114a344fe6d618") != nullptr);
  std::string file = "sqlite-probe-" + std::to_string(GetCurrentProcessId()) + ".db";
  sqlite3* db = nullptr; PROBE_CHECK(sqlite3_open(file.c_str(), &db) == SQLITE_OK);
  PROBE_CHECK(scalar(db, "PRAGMA journal_mode=WAL") == "wal");
  PROBE_CHECK(sqlite3_exec(db, "PRAGMA synchronous=FULL;PRAGMA foreign_keys=ON;CREATE TABLE IF NOT EXISTS probe(n INTEGER PRIMARY KEY);DELETE FROM probe;BEGIN;INSERT INTO probe VALUES(7);COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK);
  PROBE_CHECK(scalar(db, "PRAGMA synchronous") == "2" && scalar(db, "PRAGMA foreign_keys") == "1");
  PROBE_CHECK(scalar(db, "SELECT n FROM probe") == "7");
  PROBE_CHECK(sqlite3_exec(db, "BEGIN;INSERT INTO probe VALUES(8);ROLLBACK;", nullptr, nullptr, nullptr) == SQLITE_OK);
  PROBE_CHECK(scalar(db, "SELECT count(*) FROM probe") == "1");
  PROBE_CHECK(sqlite3_wal_checkpoint_v2(db, nullptr, SQLITE_CHECKPOINT_TRUNCATE, nullptr, nullptr) == SQLITE_OK);
  PROBE_CHECK(sqlite3_close(db) == SQLITE_OK);
  std::cout << "SQLite " << sqlite3_libversion() << " source=" << sqlite3_sourceid() << " WAL/FULL/foreign_keys readback smoke; file=" << file << "\n";
}
