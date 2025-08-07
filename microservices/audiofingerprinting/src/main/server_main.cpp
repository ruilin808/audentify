#include <httplib.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <fftw3.h>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <regex>
#include <algorithm>
#include <cctype>

#include "../recognition/Recognition.h"

using json = nlohmann::json;

// Base64 encoding implementation
static const std::string base64_chars = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/";

std::string base64_encode(std::string const& str) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    const char* bytes_to_encode = str.c_str();
    int in_len = str.length();

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i <4) ; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while((i++ < 3))
            ret += '=';
    }

    return ret;
}

// .env file parser
std::map<std::string, std::string> parseEnvFile(const std::string& filePath) {
    std::map<std::string, std::string> env;
    
    std::ifstream file(filePath);
    if (!file.is_open()) {
        std::cout << "Warning: .env file not found at " << filePath << std::endl;
        return env;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Find the = delimiter
        size_t delimPos = line.find('=');
        if (delimPos != std::string::npos) {
            std::string key = line.substr(0, delimPos);
            std::string value = line.substr(delimPos + 1);
            
            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // Remove quotes if present
            if (value.length() >= 2 && 
                ((value.front() == '"' && value.back() == '"') ||
                 (value.front() == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.length() - 2);
            }
            
            env[key] = value;
        }
    }
    
    return env;
}

// Enhanced similarity calculation for song matching
double calculateSongSimilarity(const std::string& str1, const std::string& str2) {
    std::string s1 = str1, s2 = str2;
    
    // Convert to lowercase
    std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
    std::transform(s2.begin(), s2.end(), s2.begin(), ::tolower);
    
    // Remove common variations and punctuation
    std::regex featRegex(R"(\s*(feat\.|featuring|ft\.)\s*.*)", std::regex_constants::icase);
    std::regex punctRegex(R"([^\w\s])");
    
    s1 = std::regex_replace(s1, featRegex, "");
    s2 = std::regex_replace(s2, featRegex, "");
    s1 = std::regex_replace(s1, punctRegex, " ");
    s2 = std::regex_replace(s2, punctRegex, " ");
    
    // Normalize whitespace
    std::regex wsRegex(R"(\s+)");
    s1 = std::regex_replace(s1, wsRegex, " ");
    s2 = std::regex_replace(s2, wsRegex, " ");
    
    // Trim whitespace
    s1.erase(0, s1.find_first_not_of(" \t"));
    s1.erase(s1.find_last_not_of(" \t") + 1);
    s2.erase(0, s2.find_first_not_of(" \t"));
    s2.erase(s2.find_last_not_of(" \t") + 1);
    
    // Exact match gets highest score
    if (s1 == s2) return 1.0;
    
    // Check if one is contained in the other
    if (s1.find(s2) != std::string::npos || s2.find(s1) != std::string::npos) {
        return 0.9;
    }
    
    // Word-based similarity
    std::istringstream iss1(s1), iss2(s2);
    std::set<std::string> words1, words2;
    std::string word;
    
    while (iss1 >> word) words1.insert(word);
    while (iss2 >> word) words2.insert(word);
    
    if (words1.empty() || words2.empty()) return 0.0;
    
    std::set<std::string> intersection;
    std::set_intersection(words1.begin(), words1.end(),
                         words2.begin(), words2.end(),
                         std::inserter(intersection, intersection.begin()));
    
    double wordSimilarity = static_cast<double>(intersection.size()) / 
                           std::max(words1.size(), words2.size());
    
    return wordSimilarity * 0.8;
}

// Struct to hold API credentials
struct APICredentials {
    std::string youtubeApiKey;
    std::string spotifyClientId;
    std::string spotifyClientSecret;
    std::string spotifyAccessToken;
    std::chrono::system_clock::time_point spotifyTokenExpiry;
    
    bool hasYouTube() const { return !youtubeApiKey.empty(); }
    bool hasSpotify() const { return !spotifyClientId.empty() && !spotifyClientSecret.empty(); }
};

// Helper struct for HTTP responses
struct HTTPResponse {
    std::string data;
    long responseCode;
    
    HTTPResponse() : responseCode(0) {}
};

// Callback function for CURL write data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, HTTPResponse* response) {
    size_t totalSize = size * nmemb;
    response->data.append((char*)contents, totalSize);
    return totalSize;
}

// Struct for matched song data
struct MatchedSong {
    std::string spotifyTrackId;
    std::string spotifyTrackName;
    std::string spotifyAlbumId;
    std::string spotifyAlbumName;
    std::string youtubeVideoId;
    std::string youtubeTitle;
    double similarity;
    bool isMatch;
    
    MatchedSong() : similarity(0.0), isMatch(false) {}
};

class AudioFingerprintingServer {
private:
    std::unique_ptr<AudioFingerprinting::SongRecognizer> recognizer;
    std::string dbPath;
    std::string tempDir;
    std::string envPath;
    APICredentials apiCreds;
    
    // Helper to save uploaded file temporarily
    std::string saveUploadedFile(const std::string& fileData, const std::string& filename) {
        if (!std::filesystem::exists(tempDir)) {
            std::filesystem::create_directories(tempDir);
        }
        
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        std::string tempFilename = tempDir + "/" + std::to_string(timestamp) + "_" + filename;
        
        std::ofstream file(tempFilename, std::ios::binary);
        if (!file) {
            throw std::runtime_error("Failed to create temporary file");
        }
        
        file.write(fileData.c_str(), fileData.size());
        file.close();
        
        return tempFilename;
    }
    
    void cleanupTempFile(const std::string& filepath) {
        try {
            if (std::filesystem::exists(filepath)) {
                std::filesystem::remove(filepath);
            }
        } catch (...) {
            // Ignore cleanup errors
        }
    }
    
    // HTTP request helper
    HTTPResponse makeHTTPRequest(const std::string& url, const std::vector<std::string>& headers = {}, 
                                const std::string& postData = "", const std::string& method = "GET") {
        HTTPResponse response;
        CURL* curl = curl_easy_init();
        
        if (!curl) {
            response.responseCode = -1;
            return response;
        }
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        
        // Set headers
        struct curl_slist* headerList = nullptr;
        for (const auto& header : headers) {
            headerList = curl_slist_append(headerList, header.c_str());
        }
        if (headerList) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
        }
        
        // Set POST data if provided
        if (!postData.empty() && method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
        }
        
        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.responseCode);
        } else {
            response.responseCode = -1;
            std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
        }
        
        if (headerList) {
            curl_slist_free_all(headerList);
        }
        curl_easy_cleanup(curl);
        
        return response;
    }
    
    // URL encode helper
    std::string urlEncode(const std::string& str) {
        CURL* curl = curl_easy_init();
        if (!curl) return str;
        
        char* encoded = curl_easy_escape(curl, str.c_str(), str.length());
        std::string result(encoded);
        curl_free(encoded);
        curl_easy_cleanup(curl);
        
        return result;
    }
    
    // Spotify API methods
    bool refreshSpotifyToken() {
        if (!apiCreds.hasSpotify()) {
            std::cout << "Spotify credentials not available" << std::endl;
            return false;
        }
        
        std::string auth = apiCreds.spotifyClientId + ":" + apiCreds.spotifyClientSecret;
        std::string encodedAuth = base64_encode(auth);
        
        std::vector<std::string> headers = {
            "Authorization: Basic " + encodedAuth,
            "Content-Type: application/x-www-form-urlencoded"
        };
        
        std::string postData = "grant_type=client_credentials";
        
        HTTPResponse response = makeHTTPRequest("https://accounts.spotify.com/api/token", 
                                              headers, postData, "POST");
        
        if (response.responseCode == 200) {
            try {
                json tokenResponse = json::parse(response.data);
                apiCreds.spotifyAccessToken = tokenResponse["access_token"];
                int expiresIn = tokenResponse["expires_in"];
                apiCreds.spotifyTokenExpiry = std::chrono::system_clock::now() + 
                                            std::chrono::seconds(expiresIn - 300); // 5 min buffer
                std::cout << "Spotify token refreshed successfully" << std::endl;
                return true;
            } catch (const std::exception& e) {
                std::cerr << "Failed to parse Spotify token response: " << e.what() << std::endl;
                return false;
            }
        }
        
        std::cerr << "Failed to refresh Spotify token. HTTP " << response.responseCode << std::endl;
        return false;
    }
    
    bool ensureSpotifyToken() {
        auto now = std::chrono::system_clock::now();
        if (apiCreds.spotifyAccessToken.empty() || now >= apiCreds.spotifyTokenExpiry) {
            return refreshSpotifyToken();
        }
        return true;
    }
    
    // Enhanced Spotify search - Step 1: Find songs by name
    MatchedSong findBestSpotifyTrack(const std::string& artist, const std::string& title) {
        MatchedSong result;
        
        if (!ensureSpotifyToken()) {
            std::cout << "Spotify token not available" << std::endl;
            return result;
        }
        
        // Search for tracks by name and artist
        std::string query = urlEncode("track:\"" + title + "\" artist:\"" + artist + "\"");
        std::string url = "https://api.spotify.com/v1/search?q=" + query + 
                         "&type=track&limit=20";
        
        std::vector<std::string> headers = {
            "Authorization: Bearer " + apiCreds.spotifyAccessToken
        };
        
        HTTPResponse response = makeHTTPRequest(url, headers);
        
        if (response.responseCode == 200) {
            try {
                json spotifyResponse = json::parse(response.data);
                if (spotifyResponse.contains("tracks") && 
                    spotifyResponse["tracks"].contains("items") &&
                    !spotifyResponse["tracks"]["items"].empty()) {
                    
                    double bestSimilarity = 0.0;
                    json bestTrack;
                    
                    // Find the best matching track
                    for (const auto& track : spotifyResponse["tracks"]["items"]) {
                        std::string trackName = track["name"];
                        std::string trackArtist = track["artists"][0]["name"];
                        
                        // Calculate similarity for both title and artist
                        double titleSim = calculateSongSimilarity(trackName, title);
                        double artistSim = calculateSongSimilarity(trackArtist, artist);
                        double combinedSim = (titleSim * 0.7) + (artistSim * 0.3);
                        
                        if (combinedSim > bestSimilarity) {
                            bestSimilarity = combinedSim;
                            bestTrack = track;
                        }
                    }
                    
                    // Only accept matches with high confidence
                    if (bestSimilarity >= 0.7 && !bestTrack.is_null()) {
                        result.spotifyTrackId = bestTrack["id"];
                        result.spotifyTrackName = bestTrack["name"];
                        result.spotifyAlbumId = bestTrack["album"]["id"];
                        result.spotifyAlbumName = bestTrack["album"]["name"];
                        result.similarity = bestSimilarity;
                        result.isMatch = true;
                        
                        std::cout << "Found Spotify track: " << result.spotifyTrackName 
                                  << " (Similarity: " << bestSimilarity << ")" << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Spotify track search parsing error: " << e.what() << std::endl;
            }
        } else {
            std::cerr << "Spotify track search failed. HTTP " << response.responseCode << std::endl;
        }
        
        return result;
    }
    
    // Step 2: Get album tracks using the matched song's album
    json getSpotifyAlbumTracksEnhanced(const MatchedSong& matchedSong, const std::string& /* identifiedTitle */) {
        if (!matchedSong.isMatch || matchedSong.spotifyAlbumId.empty()) {
            return nullptr;
        }
        
        std::string url = "https://api.spotify.com/v1/albums/" + matchedSong.spotifyAlbumId + "/tracks";
        
        std::vector<std::string> headers = {
            "Authorization: Bearer " + apiCreds.spotifyAccessToken
        };
        
        HTTPResponse response = makeHTTPRequest(url, headers);
        
        if (response.responseCode == 200) {
            try {
                json tracksResponse = json::parse(response.data);
                json albumTracks;
                albumTracks["tracks"] = json::array();
                albumTracks["albumName"] = matchedSong.spotifyAlbumName;
                albumTracks["albumId"] = matchedSong.spotifyAlbumId;
                
                // Get album info for metadata
                json albumInfo = getSpotifyAlbumInfo(matchedSong.spotifyAlbumId);
                if (!albumInfo.is_null()) {
                    albumTracks["albumUrl"] = albumInfo["external_urls"]["spotify"];
                    albumTracks["releaseDate"] = albumInfo["release_date"];
                    if (!albumInfo["images"].empty()) {
                        albumTracks["albumImage"] = albumInfo["images"][0]["url"];
                    }
                }
                
                // Process tracks and mark only the identified one
                for (const auto& track : tracksResponse["items"]) {
                    json trackInfo;
                    trackInfo["name"] = track["name"];
                    trackInfo["trackNumber"] = track["track_number"];
                    trackInfo["duration"] = track["duration_ms"];
                    trackInfo["url"] = track["external_urls"]["spotify"];
                    trackInfo["trackId"] = track["id"];
                    trackInfo["explicit"] = track["explicit"];
                    
                    // Add preview URL for 30-second samples
                    if (track.contains("preview_url") && !track["preview_url"].is_null()) {
                        trackInfo["previewUrl"] = track["preview_url"];
                    }
                    
                    // Format duration for display
                    int durationMs = track["duration_ms"];
                    int minutes = durationMs / 60000;
                    int seconds = (durationMs % 60000) / 1000;
                    char formatted[16];
                    snprintf(formatted, sizeof(formatted), "%d:%02d", minutes, seconds);
                    trackInfo["durationFormatted"] = formatted;
                    
                    // Artists array
                    json artists = json::array();
                    for (const auto& artist : track["artists"]) {
                        artists.push_back(artist["name"]);
                    }
                    trackInfo["artists"] = artists;
                    
                    // Mark as identified track only if it matches our originally found track
                    std::string trackId = track["id"];
                    trackInfo["isIdentifiedTrack"] = (trackId == matchedSong.spotifyTrackId);
                    
                    albumTracks["tracks"].push_back(trackInfo);
                }
                
                std::cout << "Retrieved album tracks for: " << matchedSong.spotifyAlbumName 
                          << " (Identified track: " << matchedSong.spotifyTrackName << ")" << std::endl;
                
                return albumTracks;
                
            } catch (const std::exception& e) {
                std::cerr << "Spotify album tracks parsing error: " << e.what() << std::endl;
            }
        } else {
            std::cerr << "Spotify album tracks request failed. HTTP " << response.responseCode << std::endl;
        }
        
        return nullptr;
    }
    
    // Helper to get album info
    json getSpotifyAlbumInfo(const std::string& albumId) {
        std::string url = "https://api.spotify.com/v1/albums/" + albumId;
        
        std::vector<std::string> headers = {
            "Authorization: Bearer " + apiCreds.spotifyAccessToken
        };
        
        HTTPResponse response = makeHTTPRequest(url, headers);
        
        if (response.responseCode == 200) {
            try {
                return json::parse(response.data);
            } catch (const std::exception& e) {
                std::cerr << "Spotify album info parsing error: " << e.what() << std::endl;
            }
        }
        
        return nullptr;
    }
    
    // Enhanced YouTube search with cross-verification
    json searchYouTubeVideoEnhanced(const std::string& artist, const std::string& title, const MatchedSong& spotifyMatch) {
        json result;
        
        if (!apiCreds.hasYouTube()) {
            std::cout << "YouTube API key not available, skipping video search" << std::endl;
            return result;
        }
        
        // Try different search strategies
        std::vector<std::string> searchQueries = {
            artist + " " + title + " official",
            artist + " " + title + " music video",
            artist + " " + title
        };
        
        for (const auto& queryBase : searchQueries) {
            std::string query = urlEncode(queryBase);
            std::string url = "https://www.googleapis.com/youtube/v3/search"
                             "?part=snippet"
                             "&type=video"
                             "&videoCategoryId=10" // Music category
                             "&maxResults=15" // Get more results for better matching
                             "&order=relevance"
                             "&q=" + query +
                             "&key=" + apiCreds.youtubeApiKey;
            
            HTTPResponse response = makeHTTPRequest(url);
            
            if (response.responseCode == 200) {
                try {
                    json youtubeResponse = json::parse(response.data);
                    if (youtubeResponse.contains("items") && !youtubeResponse["items"].empty()) {
                        
                        // Find the best video that matches our Spotify track
                        json bestVideo = findBestMatchingYouTubeVideo(youtubeResponse["items"], 
                                                                    artist, title, spotifyMatch);
                        
                        if (!bestVideo.is_null()) {
                            json youtubeInfo;
                            youtubeInfo["videoId"] = bestVideo["id"]["videoId"];
                            youtubeInfo["url"] = "https://www.youtube.com/watch?v=" + 
                                                bestVideo["id"]["videoId"].get<std::string>();
                            youtubeInfo["title"] = bestVideo["snippet"]["title"];
                            youtubeInfo["channelTitle"] = bestVideo["snippet"]["channelTitle"];
                            
                            // Get high quality thumbnail
                            if (bestVideo["snippet"]["thumbnails"].contains("high")) {
                                youtubeInfo["thumbnail"] = bestVideo["snippet"]["thumbnails"]["high"]["url"];
                            } else if (bestVideo["snippet"]["thumbnails"].contains("default")) {
                                youtubeInfo["thumbnail"] = bestVideo["snippet"]["thumbnails"]["default"]["url"];
                            }
                            
                            result["youtube"] = youtubeInfo;
                            std::cout << "Found matching YouTube video: " << youtubeInfo["title"] << std::endl;
                            return result;
                        }
                    }
                } catch (const std::exception& e) {
                    std::cerr << "YouTube API parsing error: " << e.what() << std::endl;
                }
            } else if (response.responseCode == 403) {
                std::cerr << "YouTube API quota exceeded" << std::endl;
                break;
            } else {
                std::cerr << "YouTube API request failed. HTTP " << response.responseCode << std::endl;
            }
        }
        
        return result;
    }
    
    // Find YouTube video that best matches our Spotify track
    json findBestMatchingYouTubeVideo(const json& videos, const std::string& artist, 
                                     const std::string& title, const MatchedSong& spotifyMatch) {
        json bestVideo;
        double bestScore = 0.0;
        
        for (const auto& video : videos) {
            double score = 0.0;
            std::string videoTitle = video["snippet"]["title"];
            std::string channelTitle = video["snippet"]["channelTitle"];
            
            // Calculate title similarity
            double titleSim = calculateSongSimilarity(videoTitle, title);
            
            // If we have a Spotify match, also compare with Spotify track name
            if (spotifyMatch.isMatch) {
                double spotifyTitleSim = calculateSongSimilarity(videoTitle, spotifyMatch.spotifyTrackName);
                titleSim = std::max(titleSim, spotifyTitleSim);
            }
            
            // Calculate artist/channel similarity
            double artistSim = calculateSongSimilarity(channelTitle, artist);
            
            // Combine scores
            score = (titleSim * 0.8) + (artistSim * 0.2);
            
            // Bonus for official channels
            std::string channelLower = channelTitle;
            std::transform(channelLower.begin(), channelLower.end(), channelLower.begin(), ::tolower);
            if (channelLower.find("official") != std::string::npos) {
                score += 0.1;
            }
            
            // Only consider videos with reasonable similarity
            if (score > bestScore && titleSim >= 0.6) {
                bestScore = score;
                bestVideo = video;
            }
        }
        
        return bestVideo;
    }
    
    // Enhanced song info to JSON conversion with new workflow
    json songInfoToJsonEnhanced(const AudioFingerprinting::SongInfo& songInfo) {
        json response;
        response["success"] = !songInfo.songId.empty();
        response["match"] = !songInfo.songId.empty();
        
        if (!songInfo.songId.empty()) {
            response["artist"] = songInfo.artist;
            response["album"] = songInfo.album;
            response["title"] = songInfo.title;
            response["songId"] = songInfo.songId;
            
            // Step 1: Find the best matching Spotify track
            MatchedSong spotifyMatch = findBestSpotifyTrack(songInfo.artist, songInfo.title);
            
            // Step 2: Get YouTube video that matches the same song
            json youtubeResult = searchYouTubeVideoEnhanced(songInfo.artist, songInfo.title, spotifyMatch);
            if (youtubeResult.contains("youtube")) {
                response["youtube"] = youtubeResult["youtube"];
            }
            
            // Step 3: Get full album using the matched Spotify track's album
            if (spotifyMatch.isMatch) {
                json spotifyAlbum = getSpotifyAlbumTracksEnhanced(spotifyMatch, songInfo.title);
                if (!spotifyAlbum.is_null()) {
                    response["spotify"] = spotifyAlbum;
                }
            }
            
        } else {
            response["message"] = "No match found in database";
        }
        
        return response;
    }

public:
    AudioFingerprintingServer(const std::string& dbPath = "fingerprints.db", 
                             const std::string& tempDir = "./temp",
                             const std::string& envPath = "") 
        : dbPath(dbPath), tempDir(tempDir) {
        recognizer = std::make_unique<AudioFingerprinting::SongRecognizer>(dbPath);
        curl_global_init(CURL_GLOBAL_DEFAULT);
        
        // Store env path for later use
        this->envPath = envPath;
    }
    
    ~AudioFingerprintingServer() {
        fftw_cleanup_threads();
        curl_global_cleanup();
    }
    
    bool initialize() {
        // Initialize FFTW threads
        fftw_init_threads();
        fftw_plan_with_nthreads(std::thread::hardware_concurrency());
        
        // Initialize database
        if (!recognizer->initializeDatabase()) {
            std::cerr << "Failed to initialize database" << std::endl;
            return false;
        }
        
        // Load API credentials from .env file
        std::map<std::string, std::string> envVars;
        
        if (!envPath.empty()) {
            // Use specified env path
            envVars = parseEnvFile(envPath);
            if (!envVars.empty()) {
                std::cout << "Loaded .env from: " << envPath << std::endl;
            }
        } else {
            // Try multiple locations for .env file
            std::vector<std::string> envPaths = {
                ".env",
                "./.env",
                "../.env",
                "../../.env"
            };
            
            for (const auto& path : envPaths) {
                envVars = parseEnvFile(path);
                if (!envVars.empty()) {
                    std::cout << "Loaded .env from: " << path << std::endl;
                    break;
                }
            }
        }
        
        if (envVars.find("YOUTUBE_API_KEY") != envVars.end()) {
            apiCreds.youtubeApiKey = envVars["YOUTUBE_API_KEY"];
        }
        if (envVars.find("SPOTIFY_CLIENT_ID") != envVars.end()) {
            apiCreds.spotifyClientId = envVars["SPOTIFY_CLIENT_ID"];
        }
        if (envVars.find("SPOTIFY_CLIENT_SECRET") != envVars.end()) {
            apiCreds.spotifyClientSecret = envVars["SPOTIFY_CLIENT_SECRET"];
        }
        
        std::cout << "Enhanced Audio Fingerprinting Server initialized" << std::endl;
        std::cout << "Database: " << dbPath << std::endl;
        std::cout << "YouTube API: " << (apiCreds.hasYouTube() ? "Enabled" : "Disabled") << std::endl;
        std::cout << "Spotify API: " << (apiCreds.hasSpotify() ? "Enabled" : "Disabled") << std::endl;
        
        return true;
    }
    
    // Configuration endpoint to set API keys at runtime
    void handleConfig(const httplib::Request& req, httplib::Response& res) {
        try {
            if (req.body.empty()) {
                json error;
                error["success"] = false;
                error["error"] = "No configuration data provided";
                res.set_content(error.dump(2) + "\n", "application/json");
                res.status = 400;
                return;
            }
            
            json config = json::parse(req.body);
            
            if (config.contains("youtubeApiKey")) {
                apiCreds.youtubeApiKey = config["youtubeApiKey"];
            }
            if (config.contains("spotifyClientId")) {
                apiCreds.spotifyClientId = config["spotifyClientId"];
            }
            if (config.contains("spotifyClientSecret")) {
                apiCreds.spotifyClientSecret = config["spotifyClientSecret"];
                // Clear existing token to force refresh
                apiCreds.spotifyAccessToken.clear();
            }
            
            json response;
            response["success"] = true;
            response["message"] = "Configuration updated";
            response["youtubeEnabled"] = apiCreds.hasYouTube();
            response["spotifyEnabled"] = apiCreds.hasSpotify();
            
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(response.dump(2) + "\n", "application/json");
            
        } catch (const std::exception& e) {
            json error;
            error["success"] = false;
            error["error"] = std::string("Configuration failed: ") + e.what();
            res.set_content(error.dump(2) + "\n", "application/json");
            res.status = 500;
        }
    }
    
    void handleRecognition(const httplib::Request& req, httplib::Response& res) {
        try {
            if (!req.has_file("audio") && !req.has_file("file")) {
                json error;
                error["success"] = false;
                error["error"] = "No audio file found in request. Use 'audio' or 'file' as field name.";
                
                res.set_content(error.dump(2) + "\n", "application/json");
                res.status = 400;
                return;
            }
            
            auto file = req.has_file("audio") ? req.get_file_value("audio") : req.get_file_value("file");
            
            std::string filename = "upload.wav";
            if (!file.filename.empty()) {
                filename = file.filename;
            }
            
            if (!AudioFingerprinting::SongRecognizer::isSupportedExtension(filename)) {
                json error;
                error["success"] = false;
                error["error"] = "Unsupported file format. Supported formats: mp3, wav, flac";
                
                res.set_content(error.dump(2) + "\n", "application/json");
                res.status = 400;
                return;
            }
            
            std::string tempFilePath = saveUploadedFile(file.content, filename);
            
            auto startTime = std::chrono::high_resolution_clock::now();
            AudioFingerprinting::SongInfo result = recognizer->recognizeSong(tempFilePath);
            auto endTime = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            cleanupTempFile(tempFilePath);
            
            // Build enhanced response with improved workflow
            json response = songInfoToJsonEnhanced(result);
            response["recognitionTimeMs"] = duration.count();
            
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(response.dump(2) + "\n", "application/json");
            
        } catch (const std::exception& e) {
            json error;
            error["success"] = false;
            error["error"] = std::string("Recognition failed: ") + e.what();
            
            res.set_content(error.dump(2) + "\n", "application/json");
            res.status = 500;
        }
    }
    
    void handleStreamRecognition(const httplib::Request& req, httplib::Response& res) {
        try {
            if (req.body.empty()) {
                json error;
                error["success"] = false;
                error["error"] = "No audio data found in request body";
                
                res.set_content(error.dump(2) + "\n", "application/json");
                res.status = 400;
                return;
            }
            
            std::string filename = "stream.wav";
            auto contentType = req.get_header_value("Content-Type");
            if (!contentType.empty()) {
                if (contentType.find("audio/mpeg") != std::string::npos || 
                    contentType.find("audio/mp3") != std::string::npos) {
                    filename = "stream.mp3";
                } else if (contentType.find("audio/flac") != std::string::npos) {
                    filename = "stream.flac";
                } else if (contentType.find("audio/wav") != std::string::npos) {
                    filename = "stream.wav";
                }
            }
            
            if (!AudioFingerprinting::SongRecognizer::isSupportedExtension(filename)) {
                json error;
                error["success"] = false;
                error["error"] = "Unsupported audio format. Supported formats: mp3, wav, flac";
                
                res.set_content(error.dump(2) + "\n", "application/json");
                res.status = 400;
                return;
            }
            
            std::string tempFilePath = saveUploadedFile(req.body, filename);
            
            auto startTime = std::chrono::high_resolution_clock::now();
            AudioFingerprinting::SongInfo result = recognizer->recognizeSong(tempFilePath);
            auto endTime = std::chrono::high_resolution_clock::now();
            
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
            
            cleanupTempFile(tempFilePath);
            
            json response = songInfoToJsonEnhanced(result);
            response["recognitionTimeMs"] = duration.count();
            
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(response.dump(2) + "\n", "application/json");
            
        } catch (const std::exception& e) {
            json error;
            error["success"] = false;
            error["error"] = std::string("Recognition failed: ") + e.what();
            
            res.set_content(error.dump(2) + "\n", "application/json");
            res.status = 500;
        }
    }
    
    void handleStats(const httplib::Request&, httplib::Response& res) {
        try {
            json stats;
            stats["totalSongs"] = 0; // recognizer->getTotalSongs();
            stats["totalHashes"] = 0; // recognizer->getTotalHashes();
            stats["database"] = dbPath;
            stats["apiStatus"] = {
                {"youtube", apiCreds.hasYouTube()},
                {"spotify", apiCreds.hasSpotify()}
            };
            
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(stats.dump(2) + "\n", "application/json");
            
        } catch (const std::exception& e) {
            json error;
            error["success"] = false;
            error["error"] = std::string("Failed to get stats: ") + e.what();
            
            res.set_content(error.dump(2) + "\n", "application/json");
            res.status = 500;
        }
    }
};

int main(int argc, char* argv[]) {
    std::string dbPath = "fingerprints.db";
    int port = 8080;
    std::string envPath = "";
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) {
            dbPath = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--env" && i + 1 < argc) {
            envPath = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --db <path>     Database path (default: fingerprints.db)\n";
            std::cout << "  --port <port>   Server port (default: 8080)\n";
            std::cout << "  --env <path>    .env file path (default: auto-detect)\n";
            std::cout << "  --help          Show this help\n";
            std::cout << "\nEnvironment Variables (from .env file):\n";
            std::cout << "  YOUTUBE_API_KEY      YouTube Data API v3 key\n";
            std::cout << "  SPOTIFY_CLIENT_ID    Spotify Client ID\n";
            std::cout << "  SPOTIFY_CLIENT_SECRET Spotify Client Secret\n";
            return 0;
        }
    }
    
    // Initialize server
    AudioFingerprintingServer server(dbPath, "./temp", envPath);
    if (!server.initialize()) {
        return 1;
    }
    
    // Create HTTP server
    httplib::Server svr;
    svr.set_payload_max_length(50 * 1024 * 1024);
    
    // Enable CORS
    svr.set_pre_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        return httplib::Server::HandlerResponse::Unhandled;
    });
    
    // Handle OPTIONS requests
    svr.Options("/.*", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        return;
    });
    
    // Recognition endpoint (multipart file upload)
    svr.Post("/recognize", [&server](const httplib::Request& req, httplib::Response& res) {
        server.handleRecognition(req, res);
    });
    
    // Streaming recognition endpoint (raw audio data)
    svr.Post("/recognize/stream", [&server](const httplib::Request& req, httplib::Response& res) {
        server.handleStreamRecognition(req, res);
    });
    
    // Stats endpoint
    svr.Get("/stats", [&server](const httplib::Request& req, httplib::Response& res) {
        server.handleStats(req, res);
    });
    
    // Configuration endpoint
    svr.Put("/config", [&server](const httplib::Request& req, httplib::Response& res) {
        server.handleConfig(req, res);
    });
    
    // Health check endpoint
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        json health;
        health["status"] = "ok";
        health["service"] = "audio-fingerprinting-enhanced";
        
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(health.dump(2) + "\n", "application/json");
    });
    
    // Start server
    std::cout << "Starting Enhanced Audio Fingerprinting Server on port " << port << std::endl;
    std::cout << "Endpoints:" << std::endl;
    std::cout << "  POST /recognize        - Upload audio file for recognition (multipart)" << std::endl;
    std::cout << "  POST /recognize/stream - Stream audio data for recognition (raw)" << std::endl;
    std::cout << "  GET  /stats           - Database statistics" << std::endl;
    std::cout << "  PUT  /config          - Configure API keys" << std::endl;
    std::cout << "  GET  /health          - Health check" << std::endl;
    
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "Failed to start server on port " << port << std::endl;
        return 1;
    }
    
    return 0;
}