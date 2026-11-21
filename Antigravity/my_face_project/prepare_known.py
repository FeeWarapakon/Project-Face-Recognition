# prepare_known.py
# ==============================================================================
# หน้าที่: ตัดแต่ง (Center Crop to Square) และย่อขนาดภาพจาก original_img ไปยัง known_faces
# รองรับ: ทั้งไฟล์เดี่ยวและโฟลเดอร์ย่อย (Subfolders per person)
# ==============================================================================

import os
import cv2
import argparse


def center_crop_to_square(img):
    h, w = img.shape[:2]
    if h == w:
        return img
    if h > w:
        start = (h - w) // 2
        return img[start:start + w, :]
    else:
        start = (w - h) // 2
        return img[:, start:start + h]


def process_directory(src_dir, dst_dir, size=224, exts=None, overwrite=False):
    if exts is None:
        exts = ('.jpg', '.jpeg', '.png', '.bmp')

    src_dir = os.path.abspath(src_dir)
    dst_dir = os.path.abspath(dst_dir)
    os.makedirs(dst_dir, exist_ok=True)

    image_tasks = []
    for root, dirs, files in os.walk(src_dir):
        for f in files:
            if f.lower().endswith(exts):
                src_path = os.path.join(root, f)
                rel_path = os.path.relpath(src_path, src_dir)
                dst_path = os.path.join(dst_dir, rel_path)
                image_tasks.append((src_path, dst_path))

    if not image_tasks:
        print('[!] No images found in', src_dir)
        return 1

    print(f'[*] Found {len(image_tasks)} images to process from {src_dir}')
    success_count = 0

    for src_path, dst_path in image_tasks:
        if os.path.exists(dst_path) and not overwrite:
            print('[Skip] Existing:', dst_path)
            success_count += 1
            continue

        os.makedirs(os.path.dirname(dst_path), exist_ok=True)
        img = cv2.imread(src_path)
        if img is None:
            print('[!] Failed to read:', src_path)
            continue

        crop = center_crop_to_square(img)
        resized = cv2.resize(crop, (size, size), interpolation=cv2.INTER_AREA)
        saved = cv2.imwrite(dst_path, resized)
        if saved:
            print('[OK] Saved:', dst_path)
            success_count += 1
        else:
            print('[!] Failed to save:', dst_path)

    print(f'[Done] Processed {success_count}/{len(image_tasks)} images into {dst_dir}')
    return 0


def main():
    parser = argparse.ArgumentParser(description='Convert images in original_img to 224x224 into known_faces')
    parser.add_argument('--src', '-s', default='original_img', help='Source folder with original images')
    parser.add_argument('--dst', '-d', default='known_faces', help='Destination folder to save resized images')
    parser.add_argument('--size', type=int, default=224, help='Output size (square)')
    parser.add_argument('--overwrite', action='store_true', help='Overwrite existing files in destination')

    args = parser.parse_args()

    src = os.path.join(os.path.dirname(__file__), args.src) if not os.path.isabs(args.src) else args.src
    dst = os.path.join(os.path.dirname(__file__), args.dst) if not os.path.isabs(args.dst) else args.dst

    return process_directory(src, dst, size=args.size, overwrite=args.overwrite)


if __name__ == '__main__':
    raise SystemExit(main())
