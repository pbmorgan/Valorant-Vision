/*
* Web scraper for VLR.gg to extract professional Valorant player names and their teams for Valorant-Vision project
* unfortunately VLR.gg does not have a public API, so we have to scrape the data directly from their website.
* Thus, the scraper relies heavily on the structure of VLR's HTML, so if they change their website this code might break and need to be updated.
*/

#include "../headers/webScraper.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <regex>
#include <filesystem>
#include <system_error>

#include <curl/curl.h>

#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

// Static member definitions
std::vector<PlayerRecord>       WebScraper::proPlayers;  // Stores all loaded player records
std::unordered_set<std::string> WebScraper::teamLinks;   // Stores team page URLs found during scraping
std::unordered_set<std::string> WebScraper::playerSet;   // Stores player names

// Stores players grouped by team abbreviation for fast lookup.
// Example: "SEN" -> [player1, player2, player3, ...]
std::unordered_map<std::string, std::vector<PlayerRecord>> WebScraper::playersByTeam;

// Internal helper functions
// These functions are used internally for file handling, network requests, and HTML parsing.
namespace {

    // Callback for libcurl to write downloaded data into a string
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
    {
        size_t totalSize = size * nmemb;
        std::string* buffer = static_cast<std::string*>(userp);
        buffer->append((char*)contents, totalSize);
        return totalSize;
    }

    // Resolves to "<exe-dir>/data" so the path is identical on every machine,
    // regardless of where the project was built or what the current working
    // directory is when the app launches.
    std::filesystem::path dataDir()
    {
        return std::filesystem::path(
            QCoreApplication::applicationDirPath().toStdString()) / "data";
    }

    // Full path to players.txt
    std::filesystem::path playersFilePath()
    {
        return dataDir() / "players.txt";
    }

    // Trim whitespace from both ends of a string
    void trim(std::string& s)
    {
        auto start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) { s.clear(); return; }
        auto end = s.find_last_not_of(" \t\r\n");
        s = s.substr(start, end - start + 1);
    }

}

// public getter for main.cpp to access loaded roster
const std::vector<PlayerRecord>& WebScraper::getPlayers()
{
    return proPlayers;
}

const std::vector<PlayerRecord>& WebScraper::getPlayersForTeam(const std::string& team)
{
    static const std::vector<PlayerRecord> empty;

    // Convert the requested team abbreviation to lowercase so OCR
    // capitalization differences do not affect the lookup.
    std::string normalizedTeam = team;
    for (auto& c : normalizedTeam)
    {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    // Search through the stored team keys and compare them case-insensitively.
    for (const auto& entry : playersByTeam)
    {
        std::string storedTeam = entry.first;

        for (auto& c : storedTeam)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (storedTeam == normalizedTeam)
        {
            return entry.second;
        }
    }

    // Return an empty vector if the team was not found.
    return empty;
}

// Returns every unique team abbreviation currently stored
// in the local player database.
std::vector<std::string> WebScraper::getTeamNames()
{
    // If the database has not been loaded yet, load players.txt first.
    // loadPlayersFromFile() also builds playersByTeam.
    if (playersByTeam.empty())
    {
        loadPlayersFromFile();
    }

    std::vector<std::string> teams;

    // Reserve enough space so the vector does not need to repeatedly
    // resize while team names are being added.
    teams.reserve(playersByTeam.size());

    // Each key in playersByTeam is already a unique team abbreviation.
    for (const auto& entry : playersByTeam)
    {
        if (!entry.first.empty())
        {
            teams.push_back(entry.first);
        }
    }

    // Sort the names so the order is predictable every time the app runs.
    std::sort(teams.begin(), teams.end());

    return teams;
}

// fileExists checks if players.txt exists and is non-empty
// Very basic check so might need to be expanded in the future
bool WebScraper::fileExists()
{
    std::error_code ec;

    // Check if file exists
    if (!std::filesystem::exists(playersFilePath(), ec) || ec)
        return false;

    // Check if file is non-empty by looking for at least one non-empty line
    std::ifstream file(playersFilePath());
    if (!file.is_open())
        return false;

    std::string line;
    while (std::getline(file, line))
        if (!line.empty()) return true;

    return false;
}

// Reads from players.txt and populates proPlayers and playerSet
void WebScraper::loadPlayersFromFile()
{
    // Clear the current in-memory database before reloading it.
    proPlayers.clear();
    playerSet.clear();
    playersByTeam.clear();

    std::ifstream file(playersFilePath());

    if (!file.is_open())
    {
        std::cerr << "Could not open players.txt\n";
        return;
    }

    std::string line;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        PlayerRecord rec;

        // New format:
        // REGION|TEAM|PLAYER
        size_t firstPipe = line.find('|');
        size_t secondPipe =
            (firstPipe == std::string::npos)
            ? std::string::npos
            : line.find('|', firstPipe + 1);

        if (firstPipe != std::string::npos &&
            secondPipe != std::string::npos)
        {
            rec.region = line.substr(0, firstPipe);

            rec.team = line.substr(
                firstPipe + 1,
                secondPipe - firstPipe - 1);

            rec.name = line.substr(secondPipe + 1);
        }
        else
        {
            // Backward compatibility with the old database:
            //
            // TEAM PLAYER
            //
            // We cannot know the region from an old line,
            // so mark it as Unknown.
            size_t space = line.find(' ');

            if (space == std::string::npos)
                continue;

            rec.region = "Unknown";
            rec.team = line.substr(0, space);
            rec.name = line.substr(space + 1);
        }

        proPlayers.push_back(rec);
        playerSet.insert(rec.name);

        // Build the normal team lookup used by OCR.
        playersByTeam[rec.team].push_back(rec);
    }

    std::cout << "Loaded "
        << proPlayers.size()
        << " players from file.\n";
}

// Writes current proPlayers data to players.txt in "TEAM PLAYERNAME" format
void WebScraper::savePlayersToFile()
{
    namespace fs = std::filesystem;

    // Ensure data directory exists
    fs::path dir = dataDir();
    std::error_code ec;
    if (!fs::exists(dir, ec))
        fs::create_directories(dir, ec);

    // Open file for writing (overwrites existing file)
    fs::path filePath = playersFilePath();
    std::ofstream file(filePath);

    // Check if file opened successfully
    if (!file.is_open())
    {
        std::cerr << "Failed to open " << filePath << "\n";
        return;
    }

    // New database format:
// REGION|TEAM|PLAYER
    for (const auto& p : proPlayers)
    {
        file << p.region
            << "|"
            << p.team
            << "|"
            << p.name
            << "\n";
    }

    std::cout << "Saved " << proPlayers.size() << " players to: " << filePath << "\n";
}

// Uses libcurl to download the HTML content of a page with retries and error handling 
// Amount of retries set in header file (default 3) but can also be adjusted when calling the function
std::string WebScraper::downloadPage(const std::string& url, int maxRetries)
{
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    // Set common options for all requests
    std::string html;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    // Attempt the request with retries
    CURLcode res = CURLE_FAILED_INIT;
    for (int attempt = 0; attempt < maxRetries; ++attempt)
    {
        html.clear();
        res = curl_easy_perform(curl);
        if (res == CURLE_OK) break;

        std::cerr << "Attempt " << (attempt + 1) << " failed for "
            << url << ": " << curl_easy_strerror(res) << "\n";
        std::cout.flush();
    }

    // Clean up curl resources
    curl_easy_cleanup(curl);

    // If all attempts failed, return empty string
    if (res != CURLE_OK)
    {
        std::cerr << "All retries failed for: " << url << "\n";
        return "";
    }
    return html;
}

// Scrapes all team abbreviations from the ranking pages without visiting individual team pages
std::vector<std::string> WebScraper::scrapeAllTeamAbbreviations()
{
    static const std::vector<std::string> regions = {
        "https://www.vlr.gg/rankings/europe",
        "https://www.vlr.gg/rankings/north-america",
        "https://www.vlr.gg/rankings/brazil",
        "https://www.vlr.gg/rankings/asia-pacific",

        // Korea has its own VLR ranking page.
        "https://www.vlr.gg/rankings/korea",

        "https://www.vlr.gg/rankings/china",
        "https://www.vlr.gg/rankings/japan",
        "https://www.vlr.gg/rankings/la-s",
        "https://www.vlr.gg/rankings/la-n",
        "https://www.vlr.gg/rankings/oceania",
        "https://www.vlr.gg/rankings/gc",
        "https://www.vlr.gg/rankings/mena",
        "https://www.vlr.gg/rankings/collegiate"
    };

    // Team abbreviations appear inline on ranking pages in this span —
    // no need to visit individual team pages.
    std::regex tagRegex(R"(<span class="m-item-team-tag">\s*([^<]+?)\s*</span>)");

    std::unordered_set<std::string> seen;
    std::vector<std::string> result;

    for (const auto& url : regions)
    {
        std::string html = downloadPage(url);
        if (html.empty()) continue;

        std::smatch match;
        std::string remaining = html;
        while (std::regex_search(remaining, match, tagRegex))
        {
            std::string tag = match[1].str();
            auto s = tag.find_first_not_of(" \t\r\n");
            auto e = tag.find_last_not_of(" \t\r\n");
            if (s != std::string::npos)
                tag = tag.substr(s, e - s + 1);

            if (!tag.empty() && seen.insert(tag).second)
                result.push_back(tag);

            remaining = match.suffix().str();
        }
    }

    std::cout << "Scraped " << result.size() << " team abbreviations.\n";
    return result;
}

// Extracts team page URLs from any VLR HTML (search results OR ranking pages)
// and normalizes them to canonical https://www.vlr.gg/team/{ID} form.
// VLR redirects bare /team/{ID} to the slug version, so the canonical form works.
void WebScraper::extractTeams(const std::string& html)
{
    // Match the team ID from either /team/{ID}/slug (ranking pages) or
    // /search/r/team/{ID}/idx (search results pages).
    std::regex teamRegex(R"(/(?:search/r/)?team/([0-9]+)(?:/[A-Za-z0-9\-]*)?)");
    auto begin = std::sregex_iterator(html.begin(), html.end(), teamRegex);
    auto end   = std::sregex_iterator();

    for (auto it = begin; it != end; ++it)
    {
        // Build the canonical VLR team URL from the captured team ID.
        std::string teamUrl =
            "https://www.vlr.gg/team/" + (*it)[1].str();

        

        // Store the discovered team URL in the set.
        teamLinks.insert(teamUrl);
    }
}

// Parses VLR search results into (canonical-team-url, displayed-title) pairs.
// Used to pre-filter candidates before paying for a full team-page download.
namespace {
    struct SearchHit { std::string url; std::string title; bool inactive; };

    std::vector<SearchHit> parseSearchHits(const std::string& html)
    {
        std::vector<SearchHit> hits;
        std::regex re(
            R"(<a href="/search/r/team/([0-9]+)/idx"[^>]*>[\s\S]*?<div class="search-item-title">([\s\S]*?)</div>)"
        );
        auto begin = std::sregex_iterator(html.begin(), html.end(), re);
        auto end   = std::sregex_iterator();
        for (auto it = begin; it != end; ++it) {
            std::string id    = (*it)[1].str();
            std::string title = (*it)[2].str();

            bool inactive = title.find("inactive") != std::string::npos;

            // Strip nested tags and trim
            title = std::regex_replace(title, std::regex(R"(<[^>]+>)"), "");
            auto s = title.find_first_not_of(" \t\r\n");
            auto e = title.find_last_not_of(" \t\r\n");
            if (s == std::string::npos) continue;
            title = title.substr(s, e - s + 1);

            hits.push_back({ "https://www.vlr.gg/team/" + id, title, inactive });
        }
        return hits;
    }

    std::string toLower(std::string s)
    {
        for (auto& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }
}

// Extracts the team abbreviation (tag) from a team page's HTML
std::string WebScraper::extractTeamTag(const std::string& html)
{
    std::smatch match;

    // Primary regex looks for the main team header tag
    std::regex primaryRegex(R"(<h2 class="wf-title team-header-tag">([^<]+)</h2>)");    // uses html, so if they change the structure of the page this might break
    if (std::regex_search(html, match, primaryRegex))
    {
        std::string tag = match[1].str();
        trim(tag);
        return tag;
    }

    std::regex fallbackRegex(R"(<span class="m-item-team-tag">\s*([^<]+?)\s*</span>)"); // uses html, so if they change the structure of the page this might break
    if (std::regex_search(html, match, fallbackRegex))
    {
        std::string tag = match[1].str();
        trim(tag);
        return tag;
    }

    return "";
}

// Extracts player names from a team page's HTML by finding the "players" section and parsing player entries
std::string WebScraper::extractPlayers(const std::string& html,const std::string& region)
{
    std::unordered_set<std::string> teamPlayerSet;
    std::string teamName = extractTeamTag(html);

    // If we can't find the team name, return early
    if (teamName.empty())
    {
        std::cerr << "Could not find team abbreviation (tag) in page HTML\n";
        return "";
    }

    // team page has separate sections for players and staff so declare labels and search for them to isolate the players section
    const std::string playersLabel = "wf-module-label";
    const std::string playersText = "players";
    const std::string staffText = "staff";

    size_t playersPos = std::string::npos;
    size_t searchPos = 0;

    // Searches through the HTML to find the position of the "players" section by looking for the label and matching its text content
    // Once again relies on HTML structure, so if they change the page this might break
    while (searchPos < html.size())
    {
        // Find the next label element
        size_t labelPos = html.find(playersLabel, searchPos);
        if (labelPos == std::string::npos) break;

        // Find the closing '>' of the label tag
        size_t closeTag = html.find('>', labelPos);
        if (closeTag == std::string::npos) break;

        // Find the next '<' which indicates the end of the label content
        size_t nextClose = html.find('<', closeTag + 1);
        if (nextClose == std::string::npos) break;

        // Extract the text content of the label
        std::string labelContent = html.substr(closeTag + 1, nextClose - closeTag - 1);

        // Trim whitespace from label content before comparing to "players"
        auto s = labelContent.find_first_not_of(" \t\r\n");
        auto e = labelContent.find_last_not_of(" \t\r\n");
        std::string trimmed = (s == std::string::npos) ? "" : labelContent.substr(s, e - s + 1);

        if (trimmed == playersText)
        {
            playersPos = nextClose;
            break;
        }

        searchPos = labelPos + 1;
    }

    if (playersPos == std::string::npos)
        return "";

    // Now find the position of the "staff" section which comes after players to determine the end of the players section
    size_t staffPos = html.size();
    size_t searchPos2 = playersPos;

    // Similar search loop to find the "staff" label and its position in the HTML
    while (searchPos2 < html.size())
    {
        // Same loop as before but looking for "staff" instead of "players"
        size_t labelPos = html.find(playersLabel, searchPos2);
        if (labelPos == std::string::npos) break;

        size_t closeTag = html.find('>', labelPos);
        if (closeTag == std::string::npos) break;

        size_t nextClose = html.find('<', closeTag + 1);
        if (nextClose == std::string::npos) break;

        std::string labelContent = html.substr(closeTag + 1, nextClose - closeTag - 1);

        auto s = labelContent.find_first_not_of(" \t\r\n");
        auto e = labelContent.find_last_not_of(" \t\r\n");
        std::string trimmed = (s == std::string::npos) ? "" : labelContent.substr(s, e - s + 1);

        if (trimmed == staffText)
        {
            staffPos = labelPos;
            break;
        }

        searchPos2 = labelPos + 1;
    }

    // Extract the HTML section that contains the players by taking the substring between the "players" and "staff" positions
    std::string playersSection = html.substr(playersPos, staffPos - playersPos);

    // Now we have the section of the HTML that contains player entries, we can use a regex to extract each player's name
    std::regex blockRegex(
        R"(<div class="team-roster-item-name-alias">\s*([\s\S]*?)\s*</div>)"
    );

    std::regex tagRegex(R"(<[^>]+>)");

    std::string remaining = playersSection;
    std::smatch match;

    // Loop through each player entry found by the regex, extract the player's name, trim whitespace, and add to the roster if it's not a duplicate 
    while (std::regex_search(remaining, match, blockRegex))
    {
        std::string inner = match[1].str();
        std::string player = std::regex_replace(inner, tagRegex, "");

        auto s = player.find_first_not_of(" \t\r\n");
        auto e = player.find_last_not_of(" \t\r\n");

        if (s != std::string::npos)
            player = player.substr(s, e - s + 1);
        else
            player.clear();

        if (!player.empty() && teamPlayerSet.insert(player).second)
        {
            PlayerRecord rec;

            // Store where this roster came from.
            rec.region = region;
            rec.name = player;
            rec.team = teamName;

            proPlayers.push_back(rec);
            playerSet.insert(player);
        }

        remaining = match.suffix().str();
    }

    return teamName;
}

// Attempts to find and scrape the two teams by their abbreviations (tags) using VLR.gg's search functionality
bool WebScraper::scrapeTeamsByAbbreviation(const std::string& teamA, const std::string& teamB)
{
    proPlayers.clear();
    playerSet.clear();

    // Flags to track if we successfully found and scraped each team
    bool foundA = false, foundB = false;

    // For each team abbreviation, perform a search on VLR.gg and look through the results for a matching team page
    for (const auto& abbr : { teamA, teamB })
    {
        std::cout << "Searching VLR.gg for: " << abbr << "\n";
        std::cout.flush();

        // Download the search results page for the team abbreviation
        std::string searchHTML = downloadPage("https://www.vlr.gg/search/?q=" + abbr + "&type=teams");

        if (searchHTML.empty()) continue;

        // Extract team page URLs from the search results HTML and store them in teamLinks
        teamLinks.clear();
        extractTeams(searchHTML);

        // Loop through the team page URLs found in the search results and check if any of them match the desired team abbreviation
        for (const auto& url : teamLinks)
        {
            std::cout << "Checking: " << url << "\n";
            std::cout.flush();

            std::string html = downloadPage(url);
            if (html.empty()) continue;

            std::string tag = extractTeamTag(html);
            if (tag != abbr) continue;

            std::cout << "Found and scraping: " << tag << "\n";
            std::cout.flush();
            extractPlayers(html);

            if (tag == teamA) foundA = true;
            if (tag == teamB) foundB = true;
            break;
        }
    }

    // Failure warnings for debugging/validation if we couldn't find one or both teams by their abbreviations
    if (!foundA) std::cerr << "Could not find team " << teamA << "\n";
    if (!foundB) std::cerr << "Could not find team " << teamB << "\n";

    bool anyTeamFound = foundA || foundB;

    if (!proPlayers.empty())
        savePlayersToFile();

    return anyTeamFound;
}

// Searches VLR for one team that is missing from the local database.
// If the team is found, its players are added to the existing local
// players.txt database instead of replacing the entire database.
bool WebScraper::scrapeMissingTeam(const std::string& teamAbbreviation)
{
    std::cout << "Searching VLR for missing team: "
        << teamAbbreviation << "\n";
    std::cout.flush();

    // Load the existing local database first.
    // This is important because we want to ADD the missing team
    // without deleting the thousands of players already stored.
    loadPlayersFromFile();

    // Search VLR for the requested team abbreviation.
    std::string searchHTML =
        downloadPage(
            "https://www.vlr.gg/search/?q=" +
            teamAbbreviation +
            "&type=teams"
        );

    // Stop if the search page could not be downloaded.
    if (searchHTML.empty())
    {
        std::cerr << "Could not search VLR for team: "
            << teamAbbreviation << "\n";
        return false;
    }

    // Clear old temporary team links before processing
    // the current search results.
    teamLinks.clear();

    // Extract all team-page URLs returned by the VLR search.
    extractTeams(searchHTML);

    // Convert the requested abbreviation to lowercase so the
    // comparison is not affected by capitalization.
    std::string requestedTeam = toLower(teamAbbreviation);

    // Check each team returned by the VLR search.
    for (const auto& url : teamLinks)
    {
        std::cout << "Checking: " << url << "\n";
        std::cout.flush();

        // Download this candidate team's VLR page.
        std::string html = downloadPage(url);

        if (html.empty())
            continue;

        // Read the team's actual abbreviation from its VLR page.
        std::string foundTag = extractTeamTag(html);

        // Compare team abbreviations case-insensitively.
        if (toLower(foundTag) != requestedTeam)
            continue;

        std::cout << "Found missing team: "
            << foundTag << "\n";

        // Add this team's players to proPlayers.
        // Because loadPlayersFromFile() ran earlier, proPlayers
        // already contains the existing local database.
        extractPlayers(html);

        // Save the existing database plus the newly found team.
        savePlayersToFile();

        // Reload the file so playersByTeam contains the new team
        // immediately for future local lookups.
        loadPlayersFromFile();

        std::cout << "Added " << foundTag
            << " to the local player database.\n";

        return true;
    }

    // No exact matching team abbreviation was found.
    std::cerr << "Could not find exact VLR team: "
        << teamAbbreviation << "\n";

    return false;
}

// If we couldn't find the teams by their abbreviations and there's no existing players.txt file, show a dialog to select regions to scrape all teams from
// Could use more styling, is very basic pop up right now 
