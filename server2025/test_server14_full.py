import requests
import time
import json

BASE_URL = "http://localhost:6062"
USERNAME = "testuser_unique_123"
PASSWORD = "testpassword"

def print_step(msg):
    print(f"\n--- STEP: {msg} ---")

def test_server14():
    print(f"Starting Server-14 Full Verification Test on {BASE_URL}")
    
    # 1. Register
    print_step("Registering User")
    try:
        resp = requests.post(f"{BASE_URL}/register", data={
            "username": USERNAME,
            "password": PASSWORD
        })
        print(f"Status: {resp.status_code}, Body: {resp.text}")
    except Exception as e:
        print(f"Registration failed: {e}")

    # 2. Login
    print_step("Logging In")
    resp = requests.post(f"{BASE_URL}/login", data={
        "username": USERNAME,
        "password": PASSWORD
    })
    if resp.status_code != 200:
        print(f"Login failed: {resp.status_code} {resp.text}")
        return
    
    data = resp.json()
    token = data['token']
    print(f"Login successful! Token acquired (starts with: {token[:20]}...)")
    
    headers = {"Authorization": f"Bearer {token}"}

    # 3. Create Session
    print_step("Creating New Session")
    resp = requests.post(f"{BASE_URL}/api/sessions/new", headers=headers)
    if resp.status_code != 200:
        print(f"Session creation failed: {resp.status_code} {resp.text}")
        return
    
    session_id = resp.json()['session_id']
    print(f"Session created: {session_id}")

    # 4. Send Message (Chat)
    print_step("Sending Chat Message")
    chat_payload = {
        "message": "Hello AI, please tell me what is 2+2? Answer only the number.",
        "max_tokens": 100,
        "temperature": 0.1
    }
    start_time = time.perf_counter()
    resp = requests.post(f"{BASE_URL}/api/sessions/{session_id}/chat", headers=headers, json=chat_payload)
    end_time = time.perf_counter()
    
    if resp.status_code != 200:
        print(f"Chat failed: {resp.status_code} {resp.text}")
        return
    
    result = resp.json()
    print(f"AI Response: {result['message']}")
    if result.get('thinking'):
        print(f"AI Thinking: {result['thinking'][:100]}...")
    print(f"Time taken: {end_time - start_time:.2f}s")

    # 5. Verify History
    print_step("Verifying History")
    resp = requests.get(f"{BASE_URL}/api/sessions/{session_id}/history", headers=headers)
    history = resp.json()
    print(f"History count: {len(history)}")
    for msg in history:
        print(f"  [{msg['role']}]: {msg['content'][:50]}...")
    
    if len(history) >= 2:
        print("✓ History verification successful!")
    else:
        print("✗ History verification failed!")

    # 6. Verify Persistence (List Sessions)
    print_step("Verifying Session Persistence (Listing sessions)")
    resp = requests.get(f"{BASE_URL}/api/sessions", headers=headers)
    sessions = resp.json()
    print(f"Found {len(sessions)} sessions for user {USERNAME}")
    found = any(s['session_id'] == session_id for s in sessions)
    if found:
        print("✓ Session persistence check successful!")
    else:
        print("✗ Session persistence check failed!")

    print("\n" + "="*40)
    print("SERVER-14 VERIFICATION COMPLETE!")
    print("="*40)

if __name__ == "__main__":
    test_server14()
