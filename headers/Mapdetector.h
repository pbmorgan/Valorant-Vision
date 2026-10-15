#ifndef MAPDETECTOR_H
#define MAPDETECTOR_H

#include <QString>
#include <string>

// Detects the map from a video file and returns the layout image path.
// Returns empty string if the broadcast bar is not visible.
QString detectMapFromVideo(const std::string& videoPath);

// Call once per map to save its name crop as a reference image.
// refPath example: "map_templates/text/haven_text.png"
void saveMapNameReference(const std::string& videoPath, const std::string& mapName);

#endif