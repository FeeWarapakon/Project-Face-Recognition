#include "database.hpp"
#include <sqlite3.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <unordered_set>

namespace fr {

FaceDatabase::~FaceDatabase() {
    close();
}

bool FaceDatabase::open(const std::string& db_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }

    if (sqlite3_open(db_path.c_str(), &db_) != SQLITE_OK) {
        std::cerr << "[ERROR] Cannot open SQLite DB (" << db_path << "): "
                  << (db_ ? sqlite3_errmsg(db_) : "unknown error") << "\n";
        return false;
    }

    sqlite3_exec(db_, "PRAGMA foreign_keys = ON;", nullptr, nullptr, nullptr);
    if (!initSchema()) {
        return false;
    }

    std::cout << "[OK] SQLite FaceDatabase opened: " << db_path << "\n";
    return true;
}

void FaceDatabase::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool FaceDatabase::initSchema() {
    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS persons (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE,
            student_id TEXT,
            role TEXT DEFAULT 'Student',
            photo_path TEXT,
            notes TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            updated_at DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS face_embeddings (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            person_id INTEGER NOT NULL,
            embedding_blob BLOB NOT NULL,
            embedding_json TEXT,
            dim INTEGER NOT NULL,
            source_image TEXT,
            mtcnn_confidence REAL DEFAULT 1.0,
            landmark_json TEXT,
            created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (person_id) REFERENCES persons(id) ON DELETE CASCADE
        );
        CREATE TABLE IF NOT EXISTS recognition_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            matched_person_id INTEGER,
            matched_name TEXT NOT NULL,
            similarity_score REAL NOT NULL,
            threshold REAL NOT NULL,
            status TEXT NOT NULL,
            detection_time_ms REAL DEFAULT 0.0,
            embedding_time_ms REAL DEFAULT 0.0,
            matching_time_ms REAL DEFAULT 0.0,
            total_latency_ms REAL DEFAULT 0.0,
            snapshot_path TEXT,
            FOREIGN KEY (matched_person_id) REFERENCES persons(id) ON DELETE SET NULL
        );
    )SQL";

    char* err_msg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
        std::cerr << "[ERROR] SQLite schema init failed: " << (err_msg ? err_msg : "") << "\n";
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

bool FaceDatabase::reloadCache() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    cache_.clear();
    std::unordered_set<int> unique_persons;

    const char* query = R"SQL(
        SELECT p.id, p.name, COALESCE(p.student_id, ''), COALESCE(p.role, ''), fe.embedding_blob, fe.dim
        FROM face_embeddings fe
        JOIN persons p ON p.id = fe.person_id;
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, query, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "[ERROR] SQLite reloadCache prepare failed: " << sqlite3_errmsg(db_) << "\n";
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        EmbeddingRecord rec;
        rec.person_id = sqlite3_column_int(stmt, 0);
        const auto* name_txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* sid_txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* role_txt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        rec.name = name_txt ? name_txt : "Unknown";
        rec.student_id = sid_txt ? sid_txt : "";
        rec.role = role_txt ? role_txt : "";

        const void* blob = sqlite3_column_blob(stmt, 4);
        const int blob_bytes = sqlite3_column_bytes(stmt, 4);
        const int dim = sqlite3_column_int(stmt, 5);

        if (blob && blob_bytes >= dim * static_cast<int>(sizeof(float)) && dim > 0) {
            rec.embedding.resize(static_cast<size_t>(dim));
            std::memcpy(rec.embedding.data(), blob, static_cast<size_t>(dim) * sizeof(float));

            // Ensure unit norm for fast dot-product Cosine Similarity
            double norm_sq = 0.0;
            for (float v : rec.embedding) norm_sq += static_cast<double>(v) * v;
            const float norm = static_cast<float>(std::sqrt(norm_sq));
            if (norm > 1e-6f) {
                for (float& v : rec.embedding) v /= norm;
            }

            unique_persons.insert(rec.person_id);
            cache_.push_back(std::move(rec));
        }
    }

    sqlite3_finalize(stmt);
    unique_person_count_ = static_cast<int>(unique_persons.size());
    std::cout << "[OK] Loaded " << cache_.size() << " face embeddings ("
              << unique_person_count_ << " persons) into RAM cache.\n";
    return true;
}

MatchResult FaceDatabase::findMatch(const std::vector<float>& embedding, float threshold) const {
    std::lock_guard<std::mutex> lock(mutex_);
    MatchResult best;
    if (embedding.empty() || cache_.empty()) {
        return best;
    }

    float max_sim = -1.0f;
    const EmbeddingRecord* best_rec = nullptr;

    for (const auto& rec : cache_) {
        if (rec.embedding.size() != embedding.size()) {
            continue;
        }
        float dot = 0.0f;
        const size_t n = embedding.size();
        for (size_t i = 0; i < n; ++i) {
            dot += embedding[i] * rec.embedding[i];
        }
        if (dot > max_sim) {
            max_sim = dot;
            best_rec = &rec;
        }
    }

    if (best_rec && max_sim >= threshold) {
        best.person_id = best_rec->person_id;
        best.name = best_rec->name;
        best.student_id = best_rec->student_id;
        best.role = best_rec->role;
        best.similarity = max_sim;
        best.status = "KNOWN";
    } else {
        best.similarity = std::max(0.0f, max_sim);
        best.status = "UNKNOWN";
    }
    return best;
}

bool FaceDatabase::enrollFace(const std::string& name,
                              const std::string& student_id,
                              const std::string& role,
                              const std::vector<float>& embedding,
                              float confidence) {
    if (name.empty() || embedding.empty()) return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return false;

        // Upsert person
        const char* upsert_sql = R"SQL(
            INSERT INTO persons (name, student_id, role)
            VALUES (?1, ?2, ?3)
            ON CONFLICT(name) DO UPDATE SET updated_at = CURRENT_TIMESTAMP;
        )SQL";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, upsert_sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, student_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, role.empty() ? "Student" : role.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        // Get person_id
        int person_id = -1;
        const char* sel_sql = "SELECT id FROM persons WHERE name = ?1;";
        if (sqlite3_prepare_v2(db_, sel_sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                person_id = sqlite3_column_int(stmt, 0);
            }
            sqlite3_finalize(stmt);
        }
        if (person_id < 0) return false;

        // Insert embedding BLOB
        const char* ins_sql = R"SQL(
            INSERT INTO face_embeddings (person_id, embedding_blob, dim, source_image, mtcnn_confidence)
            VALUES (?1, ?2, ?3, 'cpp_native_enroll', ?4);
        )SQL";
        if (sqlite3_prepare_v2(db_, ins_sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int(stmt, 1, person_id);
        sqlite3_bind_blob(stmt, 2, embedding.data(),
                          static_cast<int>(embedding.size() * sizeof(float)), SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, static_cast<int>(embedding.size()));
        sqlite3_bind_double(stmt, 4, static_cast<double>(confidence));
        const bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        if (!ok) return false;
    }

    return reloadCache();
}

void FaceDatabase::logRecognition(const MatchResult& match,
                                  float threshold,
                                  double detect_ms,
                                  double embed_ms,
                                  double match_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return;

    const char* sql = R"SQL(
        INSERT INTO recognition_logs
            (matched_person_id, matched_name, similarity_score, threshold, status,
             detection_time_ms, embedding_time_ms, matching_time_ms, total_latency_ms)
        VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9);
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

    if (match.person_id >= 0) {
        sqlite3_bind_int(stmt, 1, match.person_id);
    } else {
        sqlite3_bind_null(stmt, 1);
    }
    sqlite3_bind_text(stmt, 2, match.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, static_cast<double>(match.similarity));
    sqlite3_bind_double(stmt, 4, static_cast<double>(threshold));
    sqlite3_bind_text(stmt, 5, match.status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 6, detect_ms);
    sqlite3_bind_double(stmt, 7, embed_ms);
    sqlite3_bind_double(stmt, 8, match_ms);
    sqlite3_bind_double(stmt, 9, detect_ms + embed_ms + match_ms);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<PersonSummary> FaceDatabase::listPersons() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PersonSummary> list;
    if (!db_) return list;

    const char* sql = R"SQL(
        SELECT p.id, p.name, COALESCE(p.student_id, ''), COALESCE(p.role, 'Student'),
               COUNT(fe.id) AS emb_cnt, COALESCE(p.created_at, '')
        FROM persons p
        LEFT JOIN face_embeddings fe ON fe.person_id = p.id
        GROUP BY p.id
        ORDER BY p.id DESC;
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return list;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PersonSummary ps;
        ps.id = sqlite3_column_int(stmt, 0);
        const auto* n = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* s = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const auto* r = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        ps.embedding_count = sqlite3_column_int(stmt, 4);
        const auto* c = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        ps.name = n ? n : "";
        ps.student_id = s ? s : "";
        ps.role = r ? r : "Student";
        ps.created_at = c ? c : "";
        list.push_back(std::move(ps));
    }
    sqlite3_finalize(stmt);
    return list;
}

bool FaceDatabase::deletePerson(int person_id) {
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!db_) return false;
        const char* sql = "DELETE FROM persons WHERE id = ?1;";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, person_id);
            ok = (sqlite3_step(stmt) == SQLITE_DONE);
            sqlite3_finalize(stmt);
        }
    }
    if (ok) {
        reloadCache();
    }
    return ok;
}

std::vector<RecognitionLogEntry> FaceDatabase::getRecentLogs(int limit) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RecognitionLogEntry> logs;
    if (!db_) return logs;

    const char* sql = R"SQL(
        SELECT id, COALESCE(timestamp, ''), COALESCE(matched_name, 'Unknown'),
               similarity_score, COALESCE(status, 'UNKNOWN'), total_latency_ms
        FROM recognition_logs
        ORDER BY id DESC
        LIMIT ?1;
    )SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return logs;
    sqlite3_bind_int(stmt, 1, limit);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        RecognitionLogEntry entry;
        entry.id = sqlite3_column_int(stmt, 0);
        const auto* ts = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const auto* nm = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        entry.similarity = static_cast<float>(sqlite3_column_double(stmt, 3));
        const auto* st = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        entry.total_latency_ms = sqlite3_column_double(stmt, 5);
        entry.timestamp = ts ? ts : "";
        entry.matched_name = nm ? nm : "Unknown";
        entry.status = st ? st : "UNKNOWN";
        logs.push_back(std::move(entry));
    }
    sqlite3_finalize(stmt);
    return logs;
}

bool FaceDatabase::clearLogs() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;
    return sqlite3_exec(db_, "DELETE FROM recognition_logs;", nullptr, nullptr, nullptr) == SQLITE_OK;
}

int FaceDatabase::enrolledPersonCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return unique_person_count_;
}

int FaceDatabase::embeddingCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(cache_.size());
}

} // namespace fr
