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


namespace {
    std::filesystem::path playersFilePath() {
        return std::filesystem::path(QCoreApplication::applicationDirPath().toStdString())
            / "data" / "players.txt";
    }
    std::string toLower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
}

std::vector<std::string> WebScraper::promptRegionSelection()
{
    // List of regions and their corresponding VLR.gg ranking page URLs
    static const std::vector<std::pair<std::string, std::string>> regionList = {
        { "Europe",        "https://www.vlr.gg/rankings/europe"        },
        { "North America", "https://www.vlr.gg/rankings/north-america" },
        { "Brazil",        "https://www.vlr.gg/rankings/brazil"        },
        { "Asia-Pacific",  "https://www.vlr.gg/rankings/asia-pacific"  },

        // Korea has its own VLR ranking page and is not included
        // in the general Asia-Pacific rankings.
        { "Korea",         "https://www.vlr.gg/rankings/korea"         },

        { "China",         "https://www.vlr.gg/rankings/china"         },
        { "Japan",         "https://www.vlr.gg/rankings/japan"         },
        { "LA South",      "https://www.vlr.gg/rankings/la-s"          },
        { "LA North",      "https://www.vlr.gg/rankings/la-n"          },
        { "Oceania",       "https://www.vlr.gg/rankings/oceania"       },
        { "GC",            "https://www.vlr.gg/rankings/gc"            },
        { "MENA",          "https://www.vlr.gg/rankings/mena"          },
        { "Collegiate",    "https://www.vlr.gg/rankings/collegiate"    },
    };

    std::vector<std::string> selected;

    // Create a dialog with a list of regions and OK/Cancel buttons
    QDialog dialog;
    dialog.setWindowTitle("Refresh Player Database");
    dialog.setMinimumWidth(300);

    // Vertical layout for the dialog
    QVBoxLayout* layout = new QVBoxLayout(&dialog);

    // Instruction label at the top of the dialog
    QLabel* label = new QLabel(
        "Select the region(s) you want to refresh:"
    );
    layout->addWidget(label);

    // List widget to display regions with multi-selection enabled
    QListWidget* list = new QListWidget();
    list->setSelectionMode(QAbstractItemView::MultiSelection);
    for (const auto& region : regionList)
        list->addItem(QString::fromStdString(region.first));
    layout->addWidget(list);

    // Horizontal layout for OK and Cancel buttons
    QHBoxLayout* buttons = new QHBoxLayout();
    // Allow the user to easily migrate/rebuild every region.
    QPushButton* selectAll =
        new QPushButton("Select All");

    QPushButton* ok =
        new QPushButton("Refresh Selected");

    QPushButton* cancel =
        new QPushButton("Cancel");



    buttons->addWidget(selectAll);
    buttons->addWidget(ok);
    buttons->addWidget(cancel);
    layout->addLayout(buttons);

    // Connect button signals to dialog slots for accepting or rejecting the selection
    QObject::connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);// Highlight every region in the list.
    QObject::connect(selectAll,&QPushButton::clicked,list,&QListWidget::selectAll);
    // Execute the dialog and if the user accepted, gather the selected regions' URLs based on their selection in the list widget
    if (dialog.exec() == QDialog::Accepted)
    {
        for (auto* item : list->selectedItems())
        {
            int row = list->row(item);
            selected.push_back(regionList[row].second);
        }
    }

    return selected;
}

bool WebScraper::refreshRegions(
    const std::vector<std::string>& regionUrls)
{
    // Nothing was selected.
    if (regionUrls.empty())
    {
        return false;
    }

    // These names must match the names used by the GUI.
    static const std::vector<std::pair<std::string, std::string>> regions = {
        { "Europe",        "https://www.vlr.gg/rankings/europe" },
        { "North America", "https://www.vlr.gg/rankings/north-america" },
        { "Brazil",        "https://www.vlr.gg/rankings/brazil" },
        { "Asia-Pacific",  "https://www.vlr.gg/rankings/asia-pacific" },

        // Korea is a separate VLR ranking region.
        { "Korea",         "https://www.vlr.gg/rankings/korea" },

        { "China",         "https://www.vlr.gg/rankings/china" },
        { "Japan",         "https://www.vlr.gg/rankings/japan" },
        { "LA South",      "https://www.vlr.gg/rankings/la-s" },
        { "LA North",      "https://www.vlr.gg/rankings/la-n" },
        { "Oceania",       "https://www.vlr.gg/rankings/oceania" },
        { "GC",            "https://www.vlr.gg/rankings/gc" },
        { "MENA",          "https://www.vlr.gg/rankings/mena" },
        { "Collegiate",    "https://www.vlr.gg/rankings/collegiate" }
    };

    // Load the entire existing database first.
    loadPlayersFromFile();

    // Save a copy in case something goes wrong while downloading.
    std::vector<PlayerRecord> originalPlayers = proPlayers;

    // Determine the names of the regions selected by the user.
    std::unordered_set<std::string> selectedRegionNames;

    for (const auto& selectedUrl : regionUrls)
    {
        for (const auto& region : regions)
        {
            if (region.second == selectedUrl)
            {
                selectedRegionNames.insert(region.first);
                break;
            }
        }
    }

    if (selectedRegionNames.empty())
    {
        return false;
    }



    // Determine whether the database still needs its first
// full regional migration.
    bool hasLegacyRecords = false;

    // Check whether at least one player already has
    // an actual region assigned.
    bool hasKnownRegion = false;

    for (const auto& player : originalPlayers)
    {
        if (player.region != "Unknown")
        {
            hasKnownRegion = true;
            break;
        }
    }

    // Check players.txt for the OLD format:
    //
    // TEAM PLAYER
    //
    // The new format is:
    //
    // REGION|TEAM|PLAYER
    {
        std::ifstream file(playersFilePath());
        std::string line;

        while (std::getline(file, line))
        {
            if (line.empty())
            {
                continue;
            }

            size_t firstPipe = line.find('|');

            size_t secondPipe =
                (firstPipe == std::string::npos)
                ? std::string::npos
                : line.find('|', firstPipe + 1);

            // If the line does not have two pipe characters,
            // it is still using the old format.
            if (firstPipe == std::string::npos ||
                secondPipe == std::string::npos)
            {
                hasLegacyRecords = true;
                break;
            }
        }
    }

    // There is also a case where the old database may have
    // already been rewritten as:
    //
    // Unknown|TEAM|PLAYER
    //
    // If every player is still Unknown, treat the database
    // as needing the initial full migration.
    if (!originalPlayers.empty() && !hasKnownRegion)
    {
        hasLegacyRecords = true;
    }


    

    // Because the old players.txt did not contain region information,
    // we cannot safely refresh only one region the first time.
    //
    // The user must select every region once to migrate the database
    // into the new REGION|TEAM|PLAYER format.
    if (hasLegacyRecords &&
        selectedRegionNames.size() != regions.size())
    {
        std::cerr
            << "Old player database format detected.\n"
            << "Select all regions once to migrate the database.\n";

        return false;
    }

    // We temporarily append newly scraped players after the
    // existing database. We do not delete anything yet.
    size_t oldPlayerCount = proPlayers.size();

    std::unordered_set<std::string> scrapedTeamUrls;

    // Scrape only the regions selected by the user.
    for (const auto& selectedUrl : regionUrls)
    {
        std::string regionName;

        for (const auto& region : regions)
        {
            if (region.second == selectedUrl)
            {
                regionName = region.first;
                break;
            }
        }

        if (regionName.empty())
            continue;

        std::cout << "Refreshing region: "
            << regionName << "\n";

        // Download this region's ranking page.
        std::string rankingHTML =
            downloadPage(selectedUrl);

        // TEMP DEBUG:
        // Show how much HTML libcurl received from this VLR ranking page.
        std::cout << "Ranking HTML size: "
            << rankingHTML.size()
            << " bytes\n";

        // TEMP DEBUG:
        // Check whether the downloaded HTML actually contains VLR team links.
        std::cout << "Contains /team/: "
            << (rankingHTML.find("/team/") != std::string::npos ? "YES" : "NO")
            << "\n";

        // Do not modify players.txt if a selected ranking page
        // cannot even be downloaded.
        if (rankingHTML.empty())
        {
            std::cerr
                << "Could not download region: "
                << regionName << "\n";

            proPlayers = originalPlayers;
            return false;
        }

        // Get only the teams belonging to this ranking page.
        teamLinks.clear();
        extractTeams(rankingHTML);

        std::vector<std::string> currentTeamLinks(
            teamLinks.begin(),
            teamLinks.end());

        for (const auto& teamUrl : currentTeamLinks)
        {
            // Prevent the same team page from being scraped twice
            // if VLR happens to list it in multiple selected regions.
            if (!scrapedTeamUrls.insert(teamUrl).second)
            {
                continue;
            }

            std::cout << "Scraping: "
                << teamUrl << "\n";

            std::string teamHTML =
                downloadPage(teamUrl);

            if (teamHTML.empty())
            {
                std::cerr
                    << "Could not download team page: "
                    << teamUrl << "\n";

                // Restore the original in-memory database.
                // Do not partially replace a region.
                proPlayers = originalPlayers;

                return false;
            }

            // First make sure this is a valid team page.
            std::string teamTag = extractTeamTag(teamHTML);

            if (teamTag.empty())
            {
                std::cerr
                    << "Could not identify team page: "
                    << teamUrl << "\n";

                // Something is wrong with the page/parser,
                // so abort instead of risking the database.
                proPlayers = originalPlayers;

                return false;
            }

            // Remember how many players existed before parsing this team.
            size_t playersBefore = proPlayers.size();

            // Add these players with their region attached.
            extractPlayers(teamHTML, regionName);

            // Some ranked VLR teams may currently have no active roster.
            // That is valid, so skip them instead of failing the entire refresh.
            if (proPlayers.size() == playersBefore)
            {
                std::cout
                    << "No active roster found for "
                    << teamTag
                    << ". Skipping team.\n";

                continue;
            }
        }
    }

    // Safety check before using oldPlayerCount as a vector offset.
      //
    // oldPlayerCount should never be greater than the current
    // proPlayers size. If it is, something reset proPlayers while
    // the regional refresh was running.
    if (oldPlayerCount > proPlayers.size())
    {
        std::cerr
            << "ERROR: Player vector became smaller during refresh.\n"
            << "Old player count: " << oldPlayerCount << "\n"
            << "Current player count: " << proPlayers.size() << "\n";

        // Restore the database from before the refresh.
        proPlayers = originalPlayers;

        return false;
    }

    // Copy only the players that were added during this refresh.
    std::vector<PlayerRecord> refreshedPlayers(
        proPlayers.begin() + oldPlayerCount,
        proPlayers.end());

    if (refreshedPlayers.empty())
    {
        std::cerr
            << "No players were found for the selected regions.\n";

        proPlayers = originalPlayers;
        return false;
    }

    // Collect all teams we just refreshed.
    // This also lets us replace an old "Unknown" copy of a team
    // if that team was previously added through scrapeMissingTeam().
    std::unordered_set<std::string> refreshedTeams;

    for (const auto& player : refreshedPlayers)
    {
        refreshedTeams.insert(
            toLower(player.team));
    }

    // Go back to the original database before replacing records.
    proPlayers = originalPlayers;

    // First migration:
    // all old records have no reliable region information,
    // so after successfully scraping ALL regions we discard them.
    if (hasLegacyRecords)
    {
        proPlayers.clear();
    }
    else
    {
        // Remove:
        // 1. old records belonging to a selected region
        // 2. old/Unknown copies of teams that were just refreshed
        proPlayers.erase(
            std::remove_if(
                proPlayers.begin(),
                proPlayers.end(),
                [&](const PlayerRecord& player)
                {
                    bool selectedRegion =
                        selectedRegionNames.count(
                            player.region) > 0;

                    bool refreshedTeam =
                        refreshedTeams.count(
                            toLower(player.team)) > 0;

                    // Remove players from the region being refreshed.
                    //
                    // Also remove an "Unknown" copy of a team if that team
                    // has now been found through a regional refresh.
                    //
                    // Do NOT remove records from other named regions.
                    bool unknownCopyOfRefreshedTeam =
                        player.region == "Unknown" &&
                        refreshedTeam;

                    return selectedRegion ||
                        unknownCopyOfRefreshedTeam;
                }),
            proPlayers.end());
    }

    // Add the fresh copies from VLR.
    proPlayers.insert(
        proPlayers.end(),
        refreshedPlayers.begin(),
        refreshedPlayers.end());

    // Rebuild the helper set.
    playerSet.clear();

    for (const auto& player : proPlayers)
    {
        playerSet.insert(player.name);
    }

    // Save the combined database.
    savePlayersToFile();

    // Rebuild playersByTeam so OCR can immediately use it.
    loadPlayersFromFile();

    std::cout
        << "Selected regions refreshed successfully.\n";

    return true;
}

// Final resort if we couldn't find the teams by their abbreviations and there's no existing players.txt file,
// and the user either skipped the region selection or didn't select any regions
void WebScraper::scrapePlayers()
{
    // A full database refresh is simply a refresh of every
    // supported VLR region.
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

    // Reuse the regional-refresh system instead of maintaining
    // a second full-scrape implementation.
    bool success = refreshRegions(regions);

    if (!success)
    {
        std::cerr
            << "Full player database refresh failed.\n";
    }
}

// Main initialization function called by main.cpp after OCR attempts to detect teams
bool WebScraper::prepareForMatch(
    const std::string& teamA,
    const std::string& teamB)
{
    std::cout << "Looking for teams: "
        << teamA << " / " << teamB << "\n";
    std::cout.flush();

    // Load the local database if it has not been loaded yet.
    if (playersByTeam.empty())
    {
        loadPlayersFromFile();
    }

    // Check whether each team already exists locally.
    bool foundA = !getPlayersForTeam(teamA).empty();
    bool foundB = !getPlayersForTeam(teamB).empty();

    std::cout << "Team A: "
        << (foundA ? "GOOD" : "MISSING") << "\n";

    std::cout << "Team B: "
        << (foundB ? "GOOD" : "MISSING") << "\n";

    // If Team A is missing, search VLR and add it to players.txt.
    if (!foundA)
    {
        std::cout << "Team A missing locally. Searching VLR...\n";

        bool scrapedA = scrapeMissingTeam(teamA);

        // Verify that the team is now actually available locally.
        foundA =
            scrapedA &&
            !getPlayersForTeam(teamA).empty();
    }

    // If Team B is missing, search VLR and add it to players.txt.
    if (!foundB)
    {
        std::cout << "Team B missing locally. Searching VLR...\n";

        bool scrapedB = scrapeMissingTeam(teamB);

        // Verify that the team is now actually available locally.
        foundB =
            scrapedB &&
            !getPlayersForTeam(teamB).empty();
    }

    // IMPORTANT:
    // Get the rosters AFTER any web scraping has finished.
    // scrapeMissingTeam() reloads playersByTeam.
    std::vector<PlayerRecord> teamAPlayers =
        getPlayersForTeam(teamA);

    std::vector<PlayerRecord> teamBPlayers =
        getPlayersForTeam(teamB);

    // proPlayers should now contain ONLY the players
    // needed for this particular match.
    proPlayers.clear();
    playerSet.clear();
    teamLinks.clear();

    // Add Team A's roster.
    for (const auto& player : teamAPlayers)
    {
        proPlayers.push_back(player);
        playerSet.insert(player.name);
    }

    // Add Team B's roster.
    for (const auto& player : teamBPlayers)
    {
        proPlayers.push_back(player);
        playerSet.insert(player.name);
    }
    
    return foundA && foundB;
}
