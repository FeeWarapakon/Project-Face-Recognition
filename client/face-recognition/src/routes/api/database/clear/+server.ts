import { json, type RequestHandler } from '@sveltejs/kit';
import { DatabaseSync } from 'node:sqlite';
import path from 'node:path';
import fs from 'node:fs';

function getDatabasePath(): string {
	const candidates = [
		path.resolve(process.cwd(), '..', '..', 'Antigravity', 'face_recognition.db'),
		path.resolve(process.cwd(), '..', 'Antigravity', 'face_recognition.db'),
		path.resolve(process.cwd(), 'Antigravity', 'face_recognition.db'),
		'/home/raspberry/Desktop/Projec Face Recognition-bas/Antigravity/face_recognition.db'
	];
	for (const p of candidates) {
		if (fs.existsSync(p)) return p;
	}
	return candidates[0];
}

export const POST: RequestHandler = async ({ request, fetch }) => {
	try {
		const data = await request.json().catch(() => ({}));
		const confirm = String(data?.confirm || '').trim();

		if (confirm !== 'DELETE') {
			return json(
				{ success: false, error: "ต้องพิมพ์คำว่า 'DELETE' เพื่อยืนยันการล้างฐานข้อมูล" },
				{ status: 400 }
			);
		}

		// ลองเรียกเคลียร์ผ่าน Python Flask Backend ก่อน (เพื่อให้ RAM Cache รีเซ็ตด้วย)
		try {
			const pyRes = await fetch('http://127.0.0.1:5000/api/database/clear', {
				method: 'POST',
				headers: { 'Content-Type': 'application/json' },
				body: JSON.stringify({ confirm: 'DELETE' })
			});
			if (pyRes.ok) {
				const pyData = await pyRes.json();
				return json(pyData);
			}
		} catch (e) {
			// fallback direct sqlite below
		}

		const dbPath = getDatabasePath();

		if (!fs.existsSync(dbPath)) {
			return json({
				success: true,
				is_empty: true,
				deleted_persons: 0,
				deleted_embeddings: 0,
				deleted_logs: 0,
				message: 'ฐานข้อมูลว่างเปล่าอยู่แล้ว (ไม่พบไฟล์ฐานข้อมูล)'
			});
		}

		const db = new DatabaseSync(dbPath);

		// ตรวจสอบจำนวนข้อมูลเดิม
		const pRow = db.prepare('SELECT COUNT(*) AS c FROM persons').get() as { c: number } | undefined;
		const eRow = db.prepare('SELECT COUNT(*) AS c FROM face_embeddings').get() as { c: number } | undefined;
		const lRow = db.prepare('SELECT COUNT(*) AS c FROM recognition_logs').get() as { c: number } | undefined;

		const pCount = pRow?.c ?? 0;
		const eCount = eRow?.c ?? 0;
		const lCount = lRow?.c ?? 0;

		const isEmpty = pCount === 0 && eCount === 0 && lCount === 0;

		if (!isEmpty) {
			db.exec('DELETE FROM face_embeddings;');
			db.exec('DELETE FROM person_edit_logs;');
			db.exec('DELETE FROM recognition_logs;');
			db.exec('DELETE FROM persons;');
			db.exec(
				"DELETE FROM sqlite_sequence WHERE name IN ('persons', 'face_embeddings', 'recognition_logs', 'person_edit_logs');"
			);
			db.exec('VACUUM;');
		}

		db.close();

		return json({
			success: true,
			is_empty: isEmpty,
			deleted_persons: pCount,
			deleted_embeddings: eCount,
			deleted_logs: lCount,
			message: isEmpty
				? 'ฐานข้อมูลว่างเปล่าอยู่แล้ว'
				: `ล้างฐานข้อมูลสำเร็จเรียบร้อยแล้ว (ลบ ${pCount} รายชื่อ, ${eCount} เวกเตอร์)`
		});
	} catch (err: any) {
		return json(
			{ success: false, error: `เกิดข้อผิดพลาดในการล้างฐานข้อมูล: ${err?.message || err}` },
			{ status: 500 }
		);
	}
};

export const DELETE: RequestHandler = POST;
