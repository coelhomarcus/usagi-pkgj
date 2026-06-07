#pragma once

#include "sqlite.hpp"

#include <fmt/format.h>
#include <string>

// Symbols shown in the title list next to a flagged item.
// Keep them in the ASCII-compatible range or use UTF-8 sequences already
// known to the Vita font (ltn0.pvf / jpn0.pvf).
enum class UserFlag : int
{
    None        = 0,
    Favorite    = 1,  // [*] star
    VeryGood    = 2,  // [+] thumbs up
    Good        = 3,  // [+]
    Bad         = 4,  // [-]
    VeryBad     = 5,  // [--]
    Broken      = 6,  // [X] broken / doesn't work
    Completed   = 7,  // [/] finished / platinumed
    WantToPlay  = 8,  // [?] wishlist
};

static constexpr int UserFlagCount = 9; // including None

// Short ASCII symbols shown inline in the title list
inline const char* user_flag_symbol(UserFlag f)
{
    switch (f)
    {
    case UserFlag::None:       return "";
    case UserFlag::Favorite:   return "[*]";
    case UserFlag::VeryGood:   return "[++]";
    case UserFlag::Good:       return "[+]";
    case UserFlag::Bad:        return "[-]";
    case UserFlag::VeryBad:    return "[--]";
    case UserFlag::Broken:     return "[X]";
    case UserFlag::Completed:  return "[/]";
    case UserFlag::WantToPlay: return "[?]";
    }
    return "";
}

// Human-readable label used in the GameView flag picker
inline const char* user_flag_label(UserFlag f)
{
    switch (f)
    {
    case UserFlag::None:       return "None";
    case UserFlag::Favorite:   return "[*]  Favorite";
    case UserFlag::VeryGood:   return "[++] Very Good";
    case UserFlag::Good:       return "[+]  Good";
    case UserFlag::Bad:        return "[-]  Bad";
    case UserFlag::VeryBad:    return "[--] Very Bad";
    case UserFlag::Broken:     return "[X]  Broken";
    case UserFlag::Completed:  return "[/]  Completed";
    case UserFlag::WantToPlay: return "[?]  Want to Play";
    }
    return "None";
}

struct UserAnnotation
{
    UserFlag    flag    = UserFlag::None;
    std::string comment;
};

class AnnotationDatabase
{
public:
    explicit AnnotationDatabase(const std::string& dbPath);

    // Returns a default-constructed annotation if titleid is not found
    UserAnnotation get(const std::string& titleid) const;

    // Inserts or replaces the annotation for titleid.
    // If flag==None and comment is empty the row is deleted instead.
    void set(const std::string& titleid, const UserAnnotation& ann);

    void remove(const std::string& titleid);

private:
    SqlitePtr _db;
    std::string _dbPath;

    void open();
};
