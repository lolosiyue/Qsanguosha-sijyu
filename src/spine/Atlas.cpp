/******************************************************************************
 * spine-cpp Atlas implementation
 * Parses Spine .atlas files (texture atlas format).
 *****************************************************************************/

#include <spine/Atlas.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstring>

namespace spine {

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

static std::string getDir(const std::string &path) {
    size_t pos = path.find_last_of("/\\");
    return pos != std::string::npos ? path.substr(0, pos + 1) : "";
}

static AtlasFilter toFilter(const std::string &s) {
    if (s == "Nearest") return AtlasFilter_Nearest;
    if (s == "Linear") return AtlasFilter_Linear;
    if (s == "MipMap") return AtlasFilter_MipMap;
    if (s == "MipMapNearestNearest") return AtlasFilter_MipMapNearestNearest;
    if (s == "MipMapLinearNearest") return AtlasFilter_MipMapLinearNearest;
    if (s == "MipMapNearestLinear") return AtlasFilter_MipMapNearestLinear;
    if (s == "MipMapLinearLinear") return AtlasFilter_MipMapLinearLinear;
    return AtlasFilter_Linear;
}

static AtlasFormat toFormat(const std::string &s) {
    if (s == "Alpha") return AtlasFormat_Alpha;
    if (s == "Intensity") return AtlasFormat_Intensity;
    if (s == "LuminanceAlpha") return AtlasFormat_LuminanceAlpha;
    if (s == "RGB565") return AtlasFormat_RGB565;
    if (s == "RGBA4444") return AtlasFormat_RGBA4444;
    if (s == "RGB888") return AtlasFormat_RGB888;
    if (s == "RGBA8888") return AtlasFormat_RGBA8888;
    return AtlasFormat_RGBA8888;
}

static std::pair<int, int> parseIntPair(const std::string &value) {
    size_t comma = value.find(',');
    if (comma == std::string::npos) return {0, 0};
    return {std::stoi(trim(value.substr(0, comma))),
            std::stoi(trim(value.substr(comma + 1)))};
}

// ─── Atlas ──────────────────────────────────────────────────────────────────

Atlas::Atlas(const String &path, TextureLoader *textureLoader, bool createTexture)
    : _textureLoader(textureLoader)
{
    // Read entire file
    std::ifstream file(path.buffer(), std::ios::binary);
    if (!file.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    file.close();

    std::string dir = getDir(path.buffer());
    parse(content.c_str(), static_cast<int>(content.size()), String(dir.c_str()), createTexture);
}

Atlas::Atlas(const char *data, int length, const String &dir,
             TextureLoader *textureLoader, bool createTexture)
    : _textureLoader(textureLoader)
{
    parse(data, length, dir, createTexture);
}

Atlas::~Atlas() {
    for (size_t i = 0; i < _pages.size(); ++i) {
        if (_textureLoader && _pages[i]->texHandle)
            _textureLoader->unload(_pages[i]->texHandle);
        delete _pages[i];
    }
    for (size_t i = 0; i < _regions.size(); ++i) {
        delete _regions[i];
    }
}

AtlasRegion *Atlas::findRegion(const String &name) {
    for (size_t i = 0; i < _regions.size(); ++i) {
        if (_regions[i]->name == name)
            return _regions[i];
    }
    return nullptr;
}

void Atlas::parse(const char *data, int length, const String &dir, bool createTexture) {
    std::istringstream stream(std::string(data, length));
    std::string line;
    AtlasPage *page = nullptr;

    // Skip initial empty lines
    while (std::getline(stream, line)) {
        line = trim(line);
        if (line.empty()) continue;

        // If the line has no colon, it's a page name or region name.
        // A page name comes after an empty line (or at start).
        // A region name comes after a page has been established.

        // Check if this is a page header (no colon, first non-blank after blank/start)
        if (line.find(':') == std::string::npos) {
            // This is a page texture filename
            page = new AtlasPage();
            page->name = String(line.c_str());
            _pages.add(page);

            // Read page properties
            while (std::getline(stream, line)) {
                line = trim(line);
                if (line.empty()) break;

                size_t colon = line.find(':');
                if (colon == std::string::npos) {
                    // This is a region name — load the page texture NOW
                    // (the code after the while-loop would be skipped by goto)
                    if (createTexture && _textureLoader && page) {
                        String texPath = dir + page->name;
                        _textureLoader->load(page->texHandle, texPath);
                    }
                    goto parse_region;
                }

                std::string key = trim(line.substr(0, colon));
                std::string value = trim(line.substr(colon + 1));

                if (key == "size") {
                    auto p = parseIntPair(value);
                    page->width = p.first;
                    page->height = p.second;
                } else if (key == "format") {
                    page->format = toFormat(value);
                } else if (key == "filter") {
                    size_t comma = value.find(',');
                    if (comma != std::string::npos) {
                        page->minFilter = toFilter(trim(value.substr(0, comma)));
                        page->magFilter = toFilter(trim(value.substr(comma + 1)));
                    }
                } else if (key == "repeat") {
                    // handle repeat modes
                }
            }

            // Load texture
            if (createTexture && _textureLoader && page) {
                String texPath = dir + page->name;
                _textureLoader->load(page->texHandle, texPath);
            }

            continue;
        }

        // If we're here, the line has a colon but no page context – skip
        continue;

parse_region:
        // `line` contains the region name (no colon)
        if (!page) continue;

        while (true) {
            AtlasRegion *region = new AtlasRegion();
            region->page = page;
            region->name = String(line.c_str());
            _regions.add(region);

            // Read region properties
            while (std::getline(stream, line)) {
                line = trim(line);
                if (line.empty()) {
                    // Empty line = back to page parsing
                    goto done_region;
                }

                size_t colon = line.find(':');
                if (colon == std::string::npos) {
                    // Next region name
                    break;
                }

                std::string key = trim(line.substr(0, colon));
                std::string value = trim(line.substr(colon + 1));

                if (key == "rotate") {
                    region->rotate = (value == "true" || value == "90");
                    region->degrees = region->rotate ? 90 : 0;
                } else if (key == "xy") {
                    auto p = parseIntPair(value);
                    region->x = p.first;
                    region->y = p.second;
                } else if (key == "size") {
                    auto p = parseIntPair(value);
                    region->width = p.first;
                    region->height = p.second;
                } else if (key == "orig") {
                    auto p = parseIntPair(value);
                    region->originalWidth = p.first;
                    region->originalHeight = p.second;
                } else if (key == "offset") {
                    auto p = parseIntPair(value);
                    region->offsetX = p.first;
                    region->offsetY = p.second;
                } else if (key == "index") {
                    region->index = std::stoi(value);
                }
            }

            // Compute UVs
            if (page->width > 0 && page->height > 0) {
                float invW = 1.0f / page->width;
                float invH = 1.0f / page->height;
                region->u = region->x * invW;
                region->v = region->y * invH;
                if (region->rotate) {
                    region->u2 = (region->x + region->height) * invW;
                    region->v2 = (region->y + region->width) * invH;
                } else {
                    region->u2 = (region->x + region->width) * invW;
                    region->v2 = (region->y + region->height) * invH;
                }
            }

            if (line.empty() || stream.eof()) break;
            // `line` now has the next region name (no colon)
            continue;
        }

done_region:
        continue;
    }
}

} // namespace spine
