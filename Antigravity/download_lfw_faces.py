import os
import sys
import urllib.request
import tarfile
import time

TARGET_DIR = r"C:\Users\Balthazar\Desktop\Projec Face Recognition\Antigravity\my_face_project\known_faces"
DATASET_URL = "https://huggingface.co/datasets/logasja/lfw/resolve/main/lfw_aug.tar"
TARGET_IMAGE_COUNT = 2500  # 2000+ images

def main():
    os.makedirs(TARGET_DIR, exist_ok=True)
    print(f"[*] Target Directory: {TARGET_DIR}")
    print(f"[*] Target Images: {TARGET_IMAGE_COUNT}")
    print(f"[*] Connecting to {DATASET_URL} ...")

    req = urllib.request.Request(DATASET_URL, headers={"User-Agent": "Mozilla/5.0"})
    start_time = time.time()
    
    extracted_count = 0
    person_set = set()

    with urllib.request.urlopen(req, timeout=30) as resp:
        print("[+] Connection established! Streaming and extracting images...")
        with tarfile.open(fileobj=resp, mode="r|") as tar:
            for member in tar:
                if not member.isfile():
                    continue
                
                # Expected path: lfw_aug/Person_Name/Person_Name_xxxx.jpg
                parts = member.name.replace("\\", "/").split("/")
                if len(parts) < 3:
                    continue
                
                raw_person = parts[1]
                img_filename = parts[2]
                
                # Format person folder name
                person_name = raw_person.replace("_", " ").strip()
                person_dir = os.path.join(TARGET_DIR, person_name)
                os.makedirs(person_dir, exist_ok=True)
                
                target_path = os.path.join(person_dir, img_filename)
                
                # Extract file
                f_in = tar.extractfile(member)
                if f_in:
                    with open(target_path, "wb") as f_out:
                        f_out.write(f_in.read())
                    
                    extracted_count += 1
                    person_set.add(person_name)
                    
                    if extracted_count % 200 == 0 or extracted_count == TARGET_IMAGE_COUNT:
                        elapsed = time.time() - start_time
                        print(f"[{extracted_count}/{TARGET_IMAGE_COUNT}] Extracted {extracted_count} images across {len(person_set)} people ({elapsed:.1f}s)...")
                    
                    if extracted_count >= TARGET_IMAGE_COUNT:
                        print(f"[✓] Reached target image count: {extracted_count}")
                        break

    elapsed = time.time() - start_time
    print("\n" + "="*50)
    print(f"Completed in {elapsed:.1f}s!")
    print(f"Total new images extracted: {extracted_count}")
    print(f"Total new people folders: {len(person_set)}")
    print("="*50)

if __name__ == "__main__":
    main()
