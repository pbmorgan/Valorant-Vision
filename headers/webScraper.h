#ifndef WEBSCRAPER_H
#define WEBSCRAPER_H

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map> 


// / Simple struct to hold player information: team and name
struct PlayerRecord {
    std::string region;
    std::string team;
    std::string name;
};

// Main class for text detection and web scraping of player data from VLR.gg
class WebScraper {
public:
    // Main entry point function
    static bool prepareForMatch(const std::string& teamA, const std::string& teamB);
       
    // Maps a team abbreviation to a list of players on that team.
    // Example: "SEN" -> [player1, player2, player3, ...]
    // unordered_map gives fast average O(1) lookup by team name.
    static std::unordered_map<std::string, std::vector<PlayerRecord>> playersByTeam;

    /// Getter function for all currently loaded players
    static const std::vector<PlayerRecord>& getPlayers();

    // Returns all players for a specific team abbreviation.
    // Uses playersByTeam for fast average O(1) lookup.
    static const std::vector<PlayerRecord>& getPlayersForTeam(const std::string& team);

    // Returns every unique team abbreviation currently loaded
    // in the local player database.
    static std::vector<std::string> getTeamNames();

    // Cascade steps functions
    static std::vector<std::string> scrapeAllTeamAbbreviations();
    static bool scrapeTeamsByAbbreviation(const std::string& teamA, const std::string& teamB);

    // Searches VLR for one missing team, adds its roster to the
    // existing local database, and saves the updated database.
    static bool scrapeMissingTeam(const std::string& teamAbbreviation);
    static void scrapePlayers();

    // Refresh only the selected VLR regions while keeping
    // players from all other regions in the database.
    static bool refreshRegions(
        const std::vector<std::string>& regionUrls);

    // File I/O functions
    static void loadPlayersFromFile();
    static void savePlayersToFile();
    static bool fileExists();

    // Network functions
    static std::string downloadPage(const std::string& url, int maxRetries = 3);

    // HTML parsing functions
    static void extractTeams(const std::string& html);
    static std::string extractTeamTag(const std::string& html);
    // Extract players and remember which VLR region they came from.
    // "Unknown" is used for teams found through the individual-team
    // search instead of a regional ranking page.
    static std::string extractPlayers(
        const std::string& html,
        const std::string& region = "Unknown");

    // Qt region selection dialog function
    static std::vector<std::string> promptRegionSelection();

    // Uncomment to debug two teams
    // static void scrapeDebugTeams();

// private helper functions for string manipulation, file paths, etc. are defined in the cpp file
private:
    static std::vector<PlayerRecord>       proPlayers;
    static std::unordered_set<std::string> teamLinks;
    static std::unordered_set<std::string> playerSet;
};

#endif // WEBSCRAPER_H
