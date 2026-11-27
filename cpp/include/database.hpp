#pragma once

#include "types.hpp"
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace fr {

/**
 * SQLite3 Face Database Manager (100% Schema-Compatible with Python database.py).
 * Maintains an in-memory L2-normalized embedding cache for microsecond Cosine Similarity matching.
 */
class FaceDatabase {
public:
    FaceDatabase() = default;
    ~FaceDatabase();

    bool open(const std::string& db_path);
    void close();

    /** Reloads all embeddings into RAM cache for ultra-fast matching. */
    bool reloadCache();

    /** Finds the closest matching person using Cosine Similarity (dot product of unit vectors). */
    MatchResult findMatch(const std::vector<float>& embedding, float threshold = 0.60f) const;

    /** Enrolls a new person (or adds an embedding to an existing person). */
    bool enrollFace(const std::string& name,
                    const std::string& student_id,
                    const std::string& role,
                    const std::vector<float>& embedding,
                    float confidence = 1.0f);

    /** Logs a recognition event into recognition_logs table. */
    void logRecognition(const MatchResult& match,
                        float threshold,
                        double detect_ms,
                        double embed_ms,
                        double match_ms);

    int enrolledPersonCount() const;
    int embeddingCount() const;

private:
    bool initSchema();

    sqlite3* db_ = nullptr;
    mutable std::mutex mutex_;
    std::vector<EmbeddingRecord> cache_;
    int unique_person_count_ = 0;
};

} // namespace fr
