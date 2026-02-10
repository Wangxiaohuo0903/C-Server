import requests
import time
import concurrent.futures

BASE_URL = "http://localhost:6063"

def create_session():
    try:
        resp = requests.post(f"{BASE_URL}/api/sessions/new", timeout=5)
        resp.raise_for_status()
        return resp.json()['session_id']
    except Exception as e:
        print(f"Failed to create session: {e}")
        return None

def chat(session_id, prompt, max_tokens=50):
    try:
        start = time.perf_counter()
        resp = requests.post(f"{BASE_URL}/api/sessions/{session_id}/chat", json={
            "message": prompt,
            "max_tokens": max_tokens,
            "temperature": 0.1
        }, timeout=120)
        end = time.perf_counter()
        return end - start, resp.json().get('message', 'Error')
    except Exception as e:
        return 0, str(e)

def run_test():
    print("Creating sessions...")
    sessions = []
    for _ in range(4):
        sid = create_session()
        if sid: sessions.append(sid)
    
    if len(sessions) < 4:
        print("Not enough sessions created.")
        return

    print(f"Created {len(sessions)} sessions: {sessions}")

    prompts = [
        "Count from 1 to 50.",  # Long generation
        "List 20 colors.",      # Long generation
        "Write a poem about sky.", # Long generation
        "Explain quantum physics in 50 words." # Long generation
    ]

    print("\nStarting concurrent batch test (4 requests)...")
    start_total = time.perf_counter()
    
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        futures = [executor.submit(chat, sessions[i], prompts[i], 100) for i in range(4)]
        results = [f.result() for f in futures]
    
    end_total = time.perf_counter()
    total_time = end_total - start_total

    print(f"\nTotal time for 4 concurrent requests: {total_time:.4f}s")
    
    sum_individual = 0
    for i, (duration, msg) in enumerate(results):
        print(f"Request {i+1}: {duration:.4f}s | Response: {msg.strip()[:30]}...")
        sum_individual += duration

    print(f"\nSum of individual times (Serial Baseline): {sum_individual:.4f}s")
    
    # Speedup = Serial Time / Parallel Time
    # If perfect parallel: Speedup ~= 4
    # If pure serial: Speedup ~= 1
    if total_time > 0:
        print(f"Speedup vs Serial: {sum_individual / total_time:.2f}x")

if __name__ == "__main__":
    run_test()