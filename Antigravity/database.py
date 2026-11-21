# database.py
# ==============================================================================
# โครงงาน: Face Recognition on Raspberry Pi 5 (MobileFaceNet + INT8 Quantization)
# หน้าที่: จัดการฐานข้อมูล SQLite สำหรับจัดเก็บ Face Vectors, ข้อมูลสมาชิก, และประวัติการสแกน
# ==============================================================================

import sqlite3
import numpy as np
import json
import os
import sys
import time
import threading
from datetime import datetime
from typing import List, Dict, Optional, Tuple, Any

if sys.stdout and hasattr(sys.stdout, 'reconfigure'):
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except Exception:
        pass

DEFAULT_DB_PATH = os.path.join(os.path.dirname(__file__), "face_recognition.db")

class FaceDatabaseManager:
    """
    SQLite Face Database Manager สำหรับระบบรู้จำใบหน้า
    - จัดเก็บข้อมูลบุคคล (persons)
    - จัดเก็บเวกเตอร์เอกลักษณ์ใบหน้า Float32 Binary BLOB & JSON (face_embeddings)
    - บันทึกประวัติการตรวจจับและผลการรู้จำแบบ Real-time (recognition_logs)
    - In-Memory Vector Cache สำหรับการคำนวณ Cosine Similarity ความเร็วสูงระดับไมโครวินาที
    """

    def __init__(self, db_path: str = DEFAULT_DB_PATH):
        self.db_path = db_path
        self._cache_lock = threading.Lock()
        self._cached_matrix: Optional[np.ndarray] = None
        self._cached_metadata: Optional[List[Dict[str, Any]]] = None
        self._cached_all_records: Optional[List[Dict[str, Any]]] = None
        self.init_db()

    def get_connection(self) -> sqlite3.Connection:
        conn = sqlite3.connect(self.db_path)
        conn.row_factory = sqlite3.Row
        conn.execute("PRAGMA foreign_keys = ON")
        return conn

    def init_db(self):
        """สร้างตารางในฐานข้อมูล SQLite หากยังไม่มี"""
        with self.get_connection() as conn:
            cursor = conn.cursor()

            # 1. ตารางข้อมูลบุคคล (Persons Table)
            cursor.execute("""
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
            """)

            # 2. ตารางเวกเตอร์เอกลักษณ์ใบหน้า (Face Embeddings Table)
            cursor.execute("""
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
            """)

            # 3. ตารางประวัติการรู้จำใบหน้า (Recognition Logs Table)
            cursor.execute("""
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
            """)

            # 4. ตารางประวัติการแก้ไขข้อมูลบุคคล (Person Edit History Table)
            cursor.execute("""
            CREATE TABLE IF NOT EXISTS person_edit_logs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                person_id INTEGER NOT NULL,
                action TEXT NOT NULL,
                details TEXT,
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
                FOREIGN KEY (person_id) REFERENCES persons(id) ON DELETE CASCADE
            );
            """)

            # สร้าง Index เพื่อการค้นหาที่รวดเร็ว
            cursor.execute("CREATE INDEX IF NOT EXISTS idx_embeddings_person_id ON face_embeddings(person_id);")
            cursor.execute("CREATE INDEX IF NOT EXISTS idx_logs_timestamp ON recognition_logs(timestamp);")
            cursor.execute("CREATE INDEX IF NOT EXISTS idx_logs_status ON recognition_logs(status);")
            cursor.execute("CREATE INDEX IF NOT EXISTS idx_edit_logs_person_id ON person_edit_logs(person_id);")
            conn.commit()

    @staticmethod
    def embedding_to_blob(embedding: np.ndarray) -> bytes:
        """แปลง NumPy array เป็น Float32 Binary BLOB เพื่อการจัดเก็บที่รวดเร็วและประหยัดพื้นที่"""
        return embedding.astype(np.float32).tobytes()

    @staticmethod
    def blob_to_embedding(blob: bytes, dim: Optional[int] = None) -> np.ndarray:
        """แปลง Float32 Binary BLOB กลับเป็น NumPy array"""
        emb = np.frombuffer(blob, dtype=np.float32)
        if dim is not None:
            emb = emb.reshape((dim,))
        return emb

    def add_or_update_person(
        self,
        name: str,
        student_id: Optional[str] = None,
        role: str = "Student",
        photo_path: Optional[str] = None,
        notes: Optional[str] = None
    ) -> int:
        """เพิ่มหรืออัปเดตข้อมูลบุคคลในตาราง persons โดยไม่ข้าม ID Auto-increment"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT id, student_id, role, photo_path, notes FROM persons WHERE name = ?", (name,))
            existing = cursor.fetchone()
            if existing:
                person_id = existing["id"]
                new_std_id = student_id if student_id is not None else existing["student_id"]
                new_role = role if role is not None else existing["role"]
                new_photo = photo_path if photo_path is not None else existing["photo_path"]
                new_notes = notes if notes is not None else existing["notes"]
                cursor.execute("""
                    UPDATE persons
                    SET student_id = ?, role = ?, photo_path = ?, notes = ?, updated_at = CURRENT_TIMESTAMP
                    WHERE id = ?
                """, (new_std_id, new_role, new_photo, new_notes, person_id))
                conn.commit()
                return person_id
            else:
                cursor.execute("""
                    INSERT INTO persons (name, student_id, role, photo_path, notes, updated_at)
                    VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
                """, (name, student_id, role, photo_path, notes))
                person_id = cursor.lastrowid
                conn.commit()
                return person_id

    def add_embedding(
        self,
        person_id: int,
        embedding: np.ndarray,
        source_image: Optional[str] = None,
        mtcnn_confidence: float = 1.0,
        landmarks: Optional[List[Tuple[float, float]]] = None
    ) -> int:
        """บันทึกเวกเตอร์ Embedding ของบุคคลลงในตาราง face_embeddings"""
        norm = np.linalg.norm(embedding)
        if norm > 0:
            embedding = embedding / norm

        emb_blob = self.embedding_to_blob(embedding)
        emb_json = json.dumps(embedding.tolist())
        landmark_str = json.dumps(landmarks) if landmarks is not None else None
        dim = int(embedding.size)

        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT INTO face_embeddings 
                (person_id, embedding_blob, embedding_json, dim, source_image, mtcnn_confidence, landmark_json)
                VALUES (?, ?, ?, ?, ?, ?, ?)
            """, (person_id, emb_blob, emb_json, dim, source_image, mtcnn_confidence, landmark_str))
            emb_id = cursor.lastrowid
            conn.commit()
            self.invalidate_cache()
            return emb_id

    def register_person_with_embedding(
        self,
        name: str,
        embedding: np.ndarray,
        student_id: Optional[str] = None,
        role: str = "Student",
        source_image: Optional[str] = None,
        mtcnn_confidence: float = 1.0,
        landmarks: Optional[List[Tuple[float, float]]] = None,
        overwrite_embeddings: bool = False
    ) -> int:
        """ลงทะเบียนบุคคลและบันทึกเวกเตอร์ในขั้นตอนเดียว"""
        person_id = self.add_or_update_person(
            name=name,
            student_id=student_id,
            role=role,
            photo_path=source_image
        )

        with self.get_connection() as conn:
            cursor = conn.cursor()
            if overwrite_embeddings:
                cursor.execute("DELETE FROM face_embeddings WHERE person_id = ?", (person_id,))
                conn.commit()

        self.add_embedding(
            person_id=person_id,
            embedding=embedding,
            source_image=source_image,
            mtcnn_confidence=mtcnn_confidence,
            landmarks=landmarks
        )
        return person_id

    def invalidate_cache(self):
        """ล้าง Vector Cache ในหน่วยความจำเพื่อให้โหลดใหม่เมื่อฐานข้อมูลมีการเปลี่ยนแปลง"""
        with self._cache_lock:
            self._cached_matrix = None
            self._cached_metadata = None
            self._cached_all_records = None

    def _ensure_cache_loaded(self):
        """โหลดเวกเตอร์ทั้งหมดเข้าหน่วยความจำเป็น Matrix สำหรับ Vectorized Dot Product"""
        with self._cache_lock:
            if self._cached_matrix is not None and self._cached_metadata is not None:
                return

            with self.get_connection() as conn:
                cursor = conn.cursor()
                cursor.execute("""
                    SELECT 
                        e.id AS embedding_id,
                        e.person_id,
                        p.name,
                        p.student_id,
                        p.role,
                        e.embedding_blob,
                        e.dim,
                        e.source_image,
                        e.mtcnn_confidence,
                        e.created_at
                    FROM face_embeddings e
                    JOIN persons p ON e.person_id = p.id
                    ORDER BY p.name ASC, e.id ASC
                """)
                rows = cursor.fetchall()

            if not rows:
                self._cached_matrix = np.empty((0, 512), dtype=np.float32)
                self._cached_metadata = []
                self._cached_all_records = []
                return

            embeddings_list = []
            metadata_list = []
            all_records = []

            for r in rows:
                emb_arr = self.blob_to_embedding(r["embedding_blob"], r["dim"])
                embeddings_list.append(emb_arr)
                meta = {
                    "embedding_id": r["embedding_id"],
                    "person_id": r["person_id"],
                    "name": r["name"],
                    "student_id": r["student_id"] or "",
                    "role": r["role"] or "นักศึกษา",
                    "source_image": r["source_image"],
                    "mtcnn_confidence": r["mtcnn_confidence"],
                    "created_at": r["created_at"]
                }
                metadata_list.append(meta)
                all_records.append({**meta, "embedding": emb_arr, "dim": r["dim"]})

            # แปลงเป็น 2D NumPy Matrix (N, 512) พร้อมทำ Normalization ล่วงหน้า
            mat = np.array(embeddings_list, dtype=np.float32)
            norms = np.linalg.norm(mat, axis=1, keepdims=True)
            norms[norms == 0] = 1e-9
            mat = mat / norms

            self._cached_matrix = mat
            self._cached_metadata = metadata_list
            self._cached_all_records = all_records

    def get_all_embeddings(self) -> List[Dict[str, Any]]:
        """ดึงรายการเวกเตอร์ทั้งหมดในฐานข้อมูลพร้อมข้อมูลบุคคล (ผ่าน Cache)"""
        self._ensure_cache_loaded()
        return list(self._cached_all_records or [])

    def find_match(
        self,
        query_embedding: np.ndarray,
        threshold: float = 0.60
    ) -> Tuple[str, float, Optional[int], str]:
        """
        ค้นหาบุคคลที่ตรงกับ Query Vector โดยคำนวณ Cosine Similarity ด้วย Vectorized Matrix Dot Product
        Returns: (matched_name, highest_similarity, matched_person_id, status: 'KNOWN'/'UNKNOWN')
        """
        self._ensure_cache_loaded()
        if self._cached_matrix is None or len(self._cached_matrix) == 0:
            return "Unknown", 0.0, None, "UNKNOWN"

        norm = float(np.linalg.norm(query_embedding))
        if norm > 0:
            q = (query_embedding / norm).astype(np.float32)
        else:
            q = query_embedding.astype(np.float32)

        # Vectorized BLAS Dot Product (< 0.1 ms สำหรับ 2,500+ เวกเตอร์)
        scores = np.dot(self._cached_matrix, q)
        best_idx = int(np.argmax(scores))
        best_score = float(scores[best_idx])
        meta = self._cached_metadata[best_idx]

        status = "KNOWN" if best_score >= threshold else "UNKNOWN"
        final_name = meta["name"] if status == "KNOWN" else "Unknown"

        return final_name, best_score, meta["person_id"] if status == "KNOWN" else None, status

    def find_match_details(
        self,
        query_embedding: np.ndarray,
        threshold: float = 0.60
    ) -> Dict[str, Any]:
        """
        ค้นหาบุคคลที่ตรงกับ Query Vector โดยคำนวณ Cosine Similarity ความเร็วสูง พร้อมดึง student_id และ role
        """
        self._ensure_cache_loaded()
        if self._cached_matrix is None or len(self._cached_matrix) == 0:
            return {
                "name": "Unknown",
                "similarity": 0.0,
                "person_id": None,
                "status": "UNKNOWN",
                "student_id": "",
                "role": "Unregistered / Unknown"
            }

        norm = float(np.linalg.norm(query_embedding))
        if norm > 0:
            q = (query_embedding / norm).astype(np.float32)
        else:
            q = query_embedding.astype(np.float32)

        # Vectorized BLAS Dot Product
        scores = np.dot(self._cached_matrix, q)
        best_idx = int(np.argmax(scores))
        best_score = float(scores[best_idx])
        meta = self._cached_metadata[best_idx]

        status = "KNOWN" if best_score >= threshold else "UNKNOWN"
        final_name = meta["name"] if status == "KNOWN" else "Unknown"

        return {
            "name": final_name,
            "similarity": best_score,
            "person_id": meta["person_id"] if status == "KNOWN" else None,
            "status": status,
            "student_id": meta["student_id"] if status == "KNOWN" else "",
            "role": meta["role"] if status == "KNOWN" else "Unregistered"
        }

    def log_recognition(
        self,
        matched_name: str,
        similarity_score: float,
        threshold: float,
        status: str,
        matched_person_id: Optional[int] = None,
        detection_time_ms: float = 0.0,
        embedding_time_ms: float = 0.0,
        matching_time_ms: float = 0.0,
        total_latency_ms: float = 0.0,
        snapshot_path: Optional[str] = None
    ) -> int:
        """บันทึกเหตุการณ์การรู้จำลงในตาราง recognition_logs"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT INTO recognition_logs (
                    matched_person_id, matched_name, similarity_score, threshold,
                    status, detection_time_ms, embedding_time_ms, matching_time_ms,
                    total_latency_ms, snapshot_path
                )
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """, (
                matched_person_id, matched_name, similarity_score, threshold,
                status, detection_time_ms, embedding_time_ms, matching_time_ms,
                total_latency_ms, snapshot_path
            ))
            log_id = cursor.lastrowid
            conn.commit()
            return log_id

    def get_recent_logs(self, limit: int = 50) -> List[Dict[str, Any]]:
        """ดึงประวัติการสแกนล่าสุด"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                SELECT 
                    l.id, l.timestamp, l.matched_name, l.similarity_score,
                    l.threshold, l.status, l.total_latency_ms, l.detection_time_ms,
                    l.embedding_time_ms, l.matching_time_ms, p.student_id, p.role
                FROM recognition_logs l
                LEFT JOIN persons p ON l.matched_person_id = p.id
                ORDER BY l.id DESC
                LIMIT ?
            """, (limit,))
            return [dict(row) for row in cursor.fetchall()]

    def get_system_stats(self) -> Dict[str, Any]:
        """ดึงสถิติภาพรวมของระบบสำหรับ Dashboard"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT COUNT(*) AS total_persons FROM persons;")
            total_persons = cursor.fetchone()["total_persons"]

            cursor.execute("SELECT COUNT(*) AS total_embeddings FROM face_embeddings;")
            total_embeddings = cursor.fetchone()["total_embeddings"]

            cursor.execute("SELECT COUNT(*) AS total_logs FROM recognition_logs;")
            total_logs = cursor.fetchone()["total_logs"]

            cursor.execute("SELECT COUNT(*) AS known_count FROM recognition_logs WHERE status = 'KNOWN';")
            known_count = cursor.fetchone()["known_count"]

            cursor.execute("SELECT COUNT(*) AS unknown_count FROM recognition_logs WHERE status = 'UNKNOWN';")
            unknown_count = cursor.fetchone()["unknown_count"]

            cursor.execute("SELECT AVG(total_latency_ms) AS avg_latency FROM recognition_logs WHERE total_latency_ms > 0;")
            avg_latency = cursor.fetchone()["avg_latency"] or 0.0

            success_rate = (known_count / total_logs * 100.0) if total_logs > 0 else 100.0

            return {
                "total_persons": total_persons,
                "total_embeddings": total_embeddings,
                "total_logs": total_logs,
                "known_count": known_count,
                "unknown_count": unknown_count,
                "success_rate": round(success_rate, 1),
                "avg_latency_ms": round(avg_latency, 2)
            }

    def delete_person(self, person_id: int) -> bool:
        """ลบข้อมูลบุคคลพร้อมเวกเตอร์ทั้งหมดที่เกี่ยวข้อง"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("DELETE FROM face_embeddings WHERE person_id = ?", (person_id,))
            cursor.execute("DELETE FROM person_edit_logs WHERE person_id = ?", (person_id,))
            cursor.execute("UPDATE recognition_logs SET matched_person_id = NULL WHERE matched_person_id = ?", (person_id,))
            cursor.execute("DELETE FROM persons WHERE id = ?", (person_id,))
            deleted = cursor.rowcount > 0
            conn.commit()
            if deleted:
                self.invalidate_cache()
            return deleted

    def log_person_edit(self, person_id: int, action: str, details: Optional[str] = None) -> int:
        """บันทึกประวัติการแก้ไขข้อมูลบุคคล"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                INSERT INTO person_edit_logs (person_id, action, details, timestamp)
                VALUES (?, ?, ?, datetime('now', 'localtime'))
            """, (person_id, action, details))
            log_id = cursor.lastrowid
            conn.commit()
            return log_id

    def get_person_edit_history(self, person_id: int, limit: int = 5) -> List[Dict[str, Any]]:
        """ดึงประวัติการแก้ไข 5 รายการล่าสุดของบุคคล"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                SELECT id, person_id, action, details, timestamp
                FROM person_edit_logs
                WHERE person_id = ?
                ORDER BY id DESC
                LIMIT ?
            """, (person_id, limit))
            rows = cursor.fetchall()
            if not rows:
                cursor.execute("SELECT created_at, updated_at, name, role, student_id FROM persons WHERE id = ?", (person_id,))
                p = cursor.fetchone()
                if p:
                    res = []
                    if p["updated_at"]:
                        res.append({
                            "id": 1,
                            "person_id": person_id,
                            "action": "แก้ไขข้อมูลล่าสุด",
                            "details": f"อัปเดตสถานะ ({p['role'] or 'นักศึกษา'})",
                            "timestamp": p["updated_at"]
                        })
                    if p["created_at"]:
                        res.append({
                            "id": 2,
                            "person_id": person_id,
                            "action": "ลงทะเบียนในระบบ",
                            "details": f"บันทึกข้อมูลและชุดเวกเตอร์ใบหน้า (512-D)",
                            "timestamp": p["created_at"]
                        })
                    return res
            return [dict(r) for r in rows]

    def update_person_info(
        self,
        person_id: int,
        name: str,
        student_id: Optional[str] = None,
        role: Optional[str] = None
    ) -> bool:
        """แก้ไขข้อมูลบุคคล (ชื่อ, รหัส, ตำแหน่ง) ในตาราง persons พร้อมบันทึก Log"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                UPDATE persons
                SET name = ?,
                    student_id = ?,
                    role = ?,
                    updated_at = datetime('now', 'localtime')
                WHERE id = ?
            """, (name.strip(), student_id.strip() if student_id else None, role.strip() if role else None, person_id))
            updated = cursor.rowcount > 0
            conn.commit()
            if updated:
                self.invalidate_cache()
                detail_parts = [f"ชื่อ: {name.strip()}"]
                if student_id:
                    detail_parts.append(f"รหัส: {student_id.strip()}")
                if role:
                    detail_parts.append(f"ตำแหน่ง: {role.strip()}")
                self.log_person_edit(
                    person_id=person_id,
                    action="แก้ไขข้อมูลบุคคล",
                    details=" • ".join(detail_parts)
                )
            return updated

    def clear_all_embeddings(self):
        """ล้างเวกเตอร์ใบหน้าทั้งหมดในตาราง face_embeddings"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("DELETE FROM face_embeddings;")
            conn.commit()
            self.invalidate_cache()

    def cleanup_empty_persons(self) -> int:
        """ลบรายชื่อบุคคลในตาราง persons ที่ไม่มีเวกเตอร์รูปภาพใดๆ หลงเหลืออยู่"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                DELETE FROM persons 
                WHERE id NOT IN (SELECT DISTINCT person_id FROM face_embeddings);
            """)
            deleted_count = cursor.rowcount
            conn.commit()
            self.invalidate_cache()
            return deleted_count

    def reorder_and_compact_ids(self):
        """จัดเรียง ID ของบุคคลและเวกเตอร์ใหม่ให้ต่อเนื่องกัน (1, 2, 3...) โดยรักษาความสัมพันธ์ทั้งหมด"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT id FROM persons ORDER BY id ASC;")
            persons = cursor.fetchall()
            if not persons:
                return

            cursor.execute("PRAGMA foreign_keys = OFF;")
            
            # Map old_id -> new_id (ใช้ temporary offset เพื่อป้องกัน collision)
            temp_offset = 100000
            for idx, p in enumerate(persons, start=1):
                old_id = p["id"]
                temp_id = temp_offset + idx
                cursor.execute("UPDATE persons SET id = ? WHERE id = ?", (temp_id, old_id))
                cursor.execute("UPDATE face_embeddings SET person_id = ? WHERE person_id = ?", (temp_id, old_id))
                cursor.execute("UPDATE recognition_logs SET matched_person_id = ? WHERE matched_person_id = ?", (temp_id, old_id))
                cursor.execute("UPDATE person_edit_logs SET person_id = ? WHERE person_id = ?", (temp_id, old_id))

            for idx, p in enumerate(persons, start=1):
                temp_id = temp_offset + idx
                cursor.execute("UPDATE persons SET id = ? WHERE id = ?", (idx, temp_id))
                cursor.execute("UPDATE face_embeddings SET person_id = ? WHERE person_id = ?", (idx, temp_id))
                cursor.execute("UPDATE recognition_logs SET matched_person_id = ? WHERE matched_person_id = ?", (idx, temp_id))
                cursor.execute("UPDATE person_edit_logs SET person_id = ? WHERE person_id = ?", (idx, temp_id))

            # Reset sqlite_sequence to current max ids
            cursor.execute("SELECT MAX(id) AS m FROM persons;")
            max_p = cursor.fetchone()["m"] or 0
            cursor.execute("DELETE FROM sqlite_sequence WHERE name = 'persons';")
            cursor.execute("INSERT INTO sqlite_sequence (name, seq) VALUES ('persons', ?)", (max_p,))

            cursor.execute("PRAGMA foreign_keys = ON;")
            conn.commit()
            self.invalidate_cache()

    def reset_database(self):
        """ล้างข้อมูลทั้งหมดใน persons และ face_embeddings เพื่อเริ่มต้นใหม่"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("DELETE FROM face_embeddings;")
            cursor.execute("DELETE FROM persons;")
            cursor.execute("DELETE FROM sqlite_sequence WHERE name IN ('persons', 'face_embeddings');")
            conn.commit()
            self.invalidate_cache()

    def clear_database(self) -> Dict[str, Any]:
        """
        ล้างข้อมูลทั้งหมดในฐานข้อมูลอย่างสมบูรณ์ (persons, embeddings, logs) และคืนพื้นที่ด้วย VACUUM
        """
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("SELECT COUNT(*) AS c FROM persons;")
            p_count = cursor.fetchone()["c"]
            cursor.execute("SELECT COUNT(*) AS c FROM face_embeddings;")
            e_count = cursor.fetchone()["c"]
            cursor.execute("SELECT COUNT(*) AS c FROM recognition_logs;")
            l_count = cursor.fetchone()["c"]

            is_empty = (p_count == 0 and e_count == 0 and l_count == 0)

            if not is_empty:
                cursor.execute("DELETE FROM face_embeddings;")
                cursor.execute("DELETE FROM person_edit_logs;")
                cursor.execute("DELETE FROM recognition_logs;")
                cursor.execute("DELETE FROM persons;")
                cursor.execute("DELETE FROM sqlite_sequence WHERE name IN ('persons', 'face_embeddings', 'recognition_logs', 'person_edit_logs');")
                conn.commit()

        if not is_empty:
            # VACUUM to reclaim disk space
            with self.get_connection() as conn:
                conn.execute("VACUUM;")

        self.invalidate_cache()
        return {
            "is_empty": is_empty,
            "deleted_persons": p_count,
            "deleted_embeddings": e_count,
            "deleted_logs": l_count,
            "message": "ฐานข้อมูลว่างเปล่าอยู่แล้ว" if is_empty else f"ล้างข้อมูลสำเร็จ (ลบ {p_count} บุคคล, {e_count} เวกเตอร์)"
        }

    def get_persons_summary(self) -> List[Dict[str, Any]]:
        """ดึงรายชื่อบุคคลพร้อมจำนวนเวกเตอร์รูปภาพที่บันทึกไว้"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                SELECT 
                    p.id AS person_id,
                    p.name,
                    p.student_id,
                    p.role,
                    COUNT(e.id) AS embedding_count,
                    p.updated_at
                FROM persons p
                LEFT JOIN face_embeddings e ON p.id = e.person_id
                GROUP BY p.id
                ORDER BY p.name ASC
            """)
            return [dict(r) for r in cursor.fetchall()]

    def get_hourly_activity(self) -> List[Dict[str, Any]]:
        """ดึงข้อมูลจำนวนการสแกนแบ่งตามชั่วโมงสำหรับกราฟสถิติ"""
        with self.get_connection() as conn:
            cursor = conn.cursor()
            cursor.execute("""
                SELECT strftime('%H', timestamp) AS hour, COUNT(*) AS count
                FROM recognition_logs
                GROUP BY strftime('%H', timestamp)
                ORDER BY hour ASC
            """)
            return [dict(r) for r in cursor.fetchall()]


if __name__ == "__main__":
    print("=" * 65)
    print("  ทดสอบโมดูล SQLite Face Database Manager")
    print("=" * 65)

    db = FaceDatabaseManager()
    print(f"[OK] ฐานข้อมูลเชื่อมต่อสำเร็จ: {db.db_path}")

    sample_members = [
        {"name": "Narawich Anuwong", "student_id": "68543206118-0", "role": "Student / Developer"},
        {"name": "Sanya Lungjae", "student_id": "68543206057-0", "role": "Student / Developer"},
        {"name": "Warapakon Phimwong", "student_id": "68543206123-0", "role": "Student / Developer"}
    ]

    for mem in sample_members:
        dummy_vector = np.random.randn(512).astype(np.float32)
        dummy_vector /= np.linalg.norm(dummy_vector)

        pid = db.register_person_with_embedding(
            name=mem["name"],
            embedding=dummy_vector,
            student_id=mem["student_id"],
            role=mem["role"]
        )
        print(f"[✓] ลงทะเบียนตัวอย่าง: {mem['name']} (ID: {pid})")

    stats = db.get_system_stats()
    print(f"[OK] สถิติระบบ: {stats}")
