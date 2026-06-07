#include "annotationdb.hpp"

#include "log.hpp"

#include <fmt/format.h>
#include <stdexcept>

AnnotationDatabase::AnnotationDatabase(const std::string& dbPath)
    : _dbPath(dbPath)
{
    open();
}

void AnnotationDatabase::open()
{
    LOGF("Opening annotation database: {}", _dbPath);

    sqlite3* raw = nullptr;
    if (sqlite3_open(_dbPath.c_str(), &raw) != SQLITE_OK)
        throw std::runtime_error(
                fmt::format("can't open annotation db: {}", _dbPath));
    _db.reset(raw);

    // Create table if it doesn't exist yet
    const char* create_sql =
        "CREATE TABLE IF NOT EXISTS annotations ("
        "    titleid TEXT PRIMARY KEY NOT NULL,"
        "    flag    INTEGER NOT NULL DEFAULT 0,"
        "    comment TEXT    NOT NULL DEFAULT ''"
        ");";

    char* errmsg = nullptr;
    if (sqlite3_exec(_db.get(), create_sql, nullptr, nullptr, &errmsg) !=
        SQLITE_OK)
    {
        std::string msg = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error(
                fmt::format("can't create annotations table: {}", msg));
    }
}

UserAnnotation AnnotationDatabase::get(const std::string& titleid) const
{
    const char* sql =
        "SELECT flag, comment FROM annotations WHERE titleid = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db.get(), sql, -1, &stmt, nullptr) != SQLITE_OK)
        return {};

    sqlite3_bind_text(stmt, 1, titleid.c_str(), -1, SQLITE_TRANSIENT);

    UserAnnotation ann;
    if (sqlite3_step(stmt) == SQLITE_ROW)
    {
        ann.flag    = static_cast<UserFlag>(sqlite3_column_int(stmt, 0));
        const char* txt = reinterpret_cast<const char*>(
                sqlite3_column_text(stmt, 1));
        ann.comment = txt ? txt : "";
    }
    sqlite3_finalize(stmt);
    return ann;
}

void AnnotationDatabase::set(const std::string& titleid,
                             const UserAnnotation& ann)
{
    // If nothing meaningful is stored, just delete the row
    if (ann.flag == UserFlag::None && ann.comment.empty())
    {
        remove(titleid);
        return;
    }

    const char* sql =
        "INSERT OR REPLACE INTO annotations (titleid, flag, comment)"
        " VALUES (?, ?, ?);";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db.get(), sql, -1, &stmt, nullptr) != SQLITE_OK)
        throw std::runtime_error(
                fmt::format("annotation insert prepare failed: {}",
                            sqlite3_errmsg(_db.get())));

    sqlite3_bind_text(stmt, 1, titleid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int (stmt, 2, static_cast<int>(ann.flag));
    sqlite3_bind_text(stmt, 3, ann.comment.c_str(), -1, SQLITE_TRANSIENT);

    const int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE)
        throw std::runtime_error(
                fmt::format("annotation insert failed: {}",
                            sqlite3_errmsg(_db.get())));
}

void AnnotationDatabase::remove(const std::string& titleid)
{
    const char* sql = "DELETE FROM annotations WHERE titleid = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(_db.get(), sql, -1, &stmt, nullptr) != SQLITE_OK)
        return;

    sqlite3_bind_text(stmt, 1, titleid.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}
