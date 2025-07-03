#include <httplib.h>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <fftw3.h>
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <regex>

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

// Struct to hold API credentials
struct APICredentials {
    std::string youtubeApiKey;
    std::string spotifyClientId;
    std::string spotifyClientSecret;
    std::string spotifyAccessToken;
    std::chrono::system_clock::time_point spotifyTokenExpiry;
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

class AudioFingerprintingServer {
private:
    std::unique_ptr<AudioFingerprinting::SongRecognizer> recognizer;
    std::string dbPath;
    std::string tempDir;
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
        if (apiCreds.spotifyClientId.empty() || apiCreds.spotifyClientSecret.empty()) {
            std::cerr << "Spotify credentials not configured" << std::endl;
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
    
    json searchYouTubeVideo(const std::string& artist, const std::string& title) {
        json result;
        result["youtube"] = nullptr;
        
        if (apiCreds.youtubeApiKey.empty()) {
            std::cout << "YouTube API key not configured, skipping video search" << std::endl;
            return result;
        }
        
        std::string query = urlEncode(artist + " " + title + " official");
        std::string url = "https://www.googleapis.com/youtube/v3/search"
                         "?part=snippet"
                         "&type=video"
                         "&videoCategoryId=10" // Music category
                         "&maxResults=1"
                         "&q=" + query +
                         "&key=" + apiCreds.youtubeApiKey;
        
        HTTPResponse response = makeHTTPRequest(url);
        
        if (response.responseCode == 200) {
            try {
                json youtubeResponse = json::parse(response.data);
                if (youtubeResponse.contains("items") && !youtubeResponse["items"].empty()) {
                    auto item = youtubeResponse["items"][0];
                    json youtubeInfo;
                    youtubeInfo["videoId"] = item["id"]["videoId"];
                    youtubeInfo["url"] = "https://www.youtube.com/watch?v=" + 
                                        item["id"]["videoId"].get<std::string>();
                    youtubeInfo["title"] = item["snippet"]["title"];
                    youtubeInfo["channelTitle"] = item["snippet"]["channelTitle"];
                    
                    // Get high quality thumbnail
                    if (item["snippet"]["thumbnails"].contains("high")) {
                        youtubeInfo["thumbnail"] = item["snippet"]["thumbnails"]["high"]["url"];
                    } else if (item["snippet"]["thumbnails"].contains("default")) {
                        youtubeInfo["thumbnail"] = item["snippet"]["thumbnails"]["default"]["url"];
                    }
                    
                    result["youtube"] = youtubeInfo;
                    std::cout << "Found YouTube video: " << youtubeInfo["title"] << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "YouTube API parsing error: " << e.what() << std::endl;
            }
        } else {
            std::cerr << "YouTube API request failed. HTTP " << response.responseCode << std::endl;
        }
        
        return result;
    }
    
    json searchSpotifyAlbum(const std::string& artist, const std::string& album) {
        json result;
        result["spotify"] = nullptr;
        
        if (!ensureSpotifyToken()) {
            return result;
        }
        
        std::string query = urlEncode("artist:" + artist + " album:" + album);
        std::string url = "https://api.spotify.com/v1/search?q=" + query + 
                         "&type=album&limit=1";
        
        std::vector<std::string> headers = {
            "Authorization: Bearer " + apiCreds.spotifyAccessToken
        };
        
        HTTPResponse response = makeHTTPRequest(url, headers);
        
        if (response.responseCode == 200) {
            try {
                json spotifyResponse = json::parse(response.data);
                if (spotifyResponse.contains("albums") && 
                    spotifyResponse["albums"].contains("items") &&
                    !spotifyResponse["albums"]["items"].empty()) {
                    
                    auto albumData = spotifyResponse["albums"]["items"][0];
                    std::string albumId = albumData["id"];
                    
                    // Get album tracks
                    json albumInfo = getSpotifyAlbumTracks(albumId);
                    if (!albumInfo.is_null()) {
                        albumInfo["albumUrl"] = albumData["external_urls"]["spotify"];
                        albumInfo["albumId"] = albumId;
                        albumInfo["albumName"] = albumData["name"];
                        albumInfo["releaseDate"] = albumData["release_date"];
                        
                        // Get highest quality album image
                        if (!albumData["images"].empty()) {
                            albumInfo["albumImage"] = albumData["images"][0]["url"];
                        }
                        
                        result["spotify"] = albumInfo;
                        std::cout << "Found Spotify album: " << albumData["name"] << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Spotify API parsing error: " << e.what() << std::endl;
            }
        } else {
            std::cerr << "Spotify album search failed. HTTP " << response.responseCode << std::endl;
        }
        
        return result;
    }
    
    json getSpotifyAlbumTracks(const std::string& albumId) {
        if (!ensureSpotifyToken()) {
            return nullptr;
        }
        
        std::string url = "https://api.spotify.com/v1/albums/" + albumId + "/tracks";
        
        std::vector<std::string> headers = {
            "Authorization: Bearer " + apiCreds.spotifyAccessToken
        };
        
        HTTPResponse response = makeHTTPRequest(url, headers);
        
        if (response.responseCode == 200) {
            try {
                json tracksResponse = json::parse(response.data);
                json albumTracks;
                albumTracks["tracks"] = json::array();
                
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
                    } else {
                        trackInfo["previewUrl"] = nullptr;
                    }
                    
                    // Format duration for display
                    int durationMs = track["duration_ms"];
                    int minutes = durationMs / 60000;
                    int seconds = (durationMs % 60000) / 1000;
                    char formatted[10];
                    sprintf(formatted, "%d:%02d", minutes, seconds);
                    trackInfo["durationFormatted"] = formatted;
                    
                    // Artists array
                    json artists = json::array();
                    for (const auto& artist : track["artists"]) {
                        artists.push_back(artist["name"]);
                    }
                    trackInfo["artists"] = artists;
                    
                    // Initialize as not identified
                    trackInfo["isIdentifiedTrack"] = false;
                    
                    albumTracks["tracks"].push_back(trackInfo);
                }
                
                return albumTracks;
            } catch (const std::exception& e) {
                std::cerr << "Spotify tracks parsing error: " << e.what() << std::endl;
            }
        } else {
            std::cerr << "Spotify tracks request failed. HTTP " << response.responseCode << std::endl;
        }
        
        return nullptr;
    }
    
    // Enhanced song info to JSON conversion
    json songInfoToJson(const AudioFingerprinting::SongInfo& songInfo) {
        json response;
        response["success"] = !songInfo.songId.empty();
        
        if (!songInfo.songId.empty()) {
            response["match"] = true;
            response["artist"] = songInfo.artist;
            response["album"] = songInfo.album;
            response["title"] = songInfo.title;
            response["songId"] = songInfo.songId;
            
            // Add YouTube video information
            json youtubeInfo = searchYouTubeVideo(songInfo.artist, songInfo.title);
            if (!youtubeInfo["youtube"].is_null()) {
                response["youtube"] = youtubeInfo["youtube"];
            }
            
            // Add Spotify album information
            if (!songInfo.album.empty()) {
                json spotifyInfo = searchSpotifyAlbum(songInfo.artist, songInfo.album);
                if (!spotifyInfo["spotify"].is_null()) {
                    // Mark the identified track
                    auto& tracks = spotifyInfo["spotify"]["tracks"];
                    for (auto& track : tracks) {
                        std::string trackName = track["name"];
                        // Convert both to lowercase for better matching
                        std::string lowerTrackName = trackName;
                        std::string lowerTitle = songInfo.title;
                        std::transform(lowerTrackName.begin(), lowerTrackName.end(), lowerTrackName.begin(), ::tolower);
                        std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);
                        
                        if (lowerTrackName.find(lowerTitle) != std::string::npos || 
                            lowerTitle.find(lowerTrackName) != std::string::npos) {
                            track["isIdentifiedTrack"] = true;
                            std::cout << "Identified track: " << trackName << std::endl;
                        }
                    }
                    response["spotify"] = spotifyInfo["spotify"];
                }
            }
            
        } else {
            response["match"] = false;
            response["message"] = "No match found in database";
        }
        
        return response;
    }

public:
    AudioFingerprintingServer(const std::string& dbPath = "fingerprints.db", 
                             const std::string& tempDir = "./temp") 
        : dbPath(dbPath), tempDir(tempDir) {
        recognizer = std::make_unique<AudioFingerprinting::SongRecognizer>(dbPath);
        curl_global_init(CURL_GLOBAL_DEFAULT);
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
        
        // Load API credentials from environment variables
        const char* youtubeKey = std::getenv("YOUTUBE_API_KEY");
        const char* spotifyClientId = std::getenv("SPOTIFY_CLIENT_ID");
        const char* spotifyClientSecret = std::getenv("SPOTIFY_CLIENT_SECRET");
        
        if (youtubeKey) apiCreds.youtubeApiKey = youtubeKey;
        if (spotifyClientId) apiCreds.spotifyClientId = spotifyClientId;
        if (spotifyClientSecret) apiCreds.spotifyClientSecret = spotifyClientSecret;
        
        std::cout << "Audio Fingerprinting Server initialized" << std::endl;
        std::cout << "Database: " << dbPath << std::endl;
        std::cout << "YouTube API: " << (apiCreds.youtubeApiKey.empty() ? "Disabled" : "Enabled") << std::endl;
        std::cout << "Spotify API: " << (apiCreds.spotifyClientId.empty() ? "Disabled" : "Enabled") << std::endl;
        
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
            response["youtubeEnabled"] = !apiCreds.youtubeApiKey.empty();
            response["spotifyEnabled"] = !apiCreds.spotifyClientId.empty() && 
                                        !apiCreds.spotifyClientSecret.empty();
            
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
            
            // Build enhanced response with YouTube and Spotify data
            json response = songInfoToJson(result);
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
            
            json response = songInfoToJson(result);
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
                {"youtube", !apiCreds.youtubeApiKey.empty()},
                {"spotify", !apiCreds.spotifyClientId.empty() && !apiCreds.spotifyClientSecret.empty()}
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
    
    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--db" && i + 1 < argc) {
            dbPath = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --db <path>     Database path (default: fingerprints.db)\n";
            std::cout << "  --port <port>   Server port (default: 8080)\n";
            std::cout << "  --help          Show this help\n";
            std::cout << "\nEnvironment Variables:\n";
            std::cout << "  YOUTUBE_API_KEY      YouTube Data API v3 key\n";
            std::cout << "  SPOTIFY_CLIENT_ID    Spotify Client ID\n";
            std::cout << "  SPOTIFY_CLIENT_SECRET Spotify Client Secret\n";
            return 0;
        }
    }
    
    // Initialize server
    AudioFingerprintingServer server(dbPath);
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