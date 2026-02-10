import requests
import time
import json

# Configuration
SERVERS = {
    "Server-12 (No Cache)": "http://localhost:6060",
    "Server-13 (KV Cache)": "http://localhost:6061"
}

PROMPTS = [
    "Explain the theory of relativity in simple terms.",
    "Who developed it?",
    "What year was it published?"
]

def create_session(base_url):
    try:
        response = requests.post(f"{base_url}/api/sessions/new", timeout=5)
        response.raise_for_status()
        return response.json()['session_id']
    except Exception as e:
        print(f"Error creating session on {base_url}: {e}")
        return None

def chat(base_url, session_id, message):
    try:
        start_time = time.perf_counter()
        response = requests.post(
            f"{base_url}/api/sessions/{session_id}/chat",
            json={"message": message, "max_tokens": 150, "temperature": 0.7},
            timeout=120
        )
        end_time = time.perf_counter()
        response.raise_for_status()
        return end_time - start_time, response.json()['message']
    except Exception as e:
        print(f"Error chatting on {base_url}: {e}")
        return None, None

def run_test():
    results = {name: [] for name in SERVERS}
    
    print(f"{ '='*60}")
    print(f"Performance Comparison: Multi-Chat (Server 12) vs KV Cache (Server 13)")
    print(f"{ '='*60}\n")

    for server_name, base_url in SERVERS.items():
        print(f"Testing {server_name} at {base_url}...")
        session_id = create_session(base_url)
        if not session_id:
            print("  -> Failed to create session. Skipping.")
            continue
            
        print(f"  -> Session created: {session_id}")
        
        for i, prompt in enumerate(PROMPTS):
            print(f"  -> Turn {i+1}: '{prompt}'", end="", flush=True)
            duration, response = chat(base_url, session_id, prompt)
            
            if duration:
                print(f" -> {duration:.4f}s")
                results[server_name].append(duration)
            else:
                print(" -> Failed")
                results[server_name].append(None)
        print()

    # Display Comparison
    print(f"\n{ '='*60}")
    print(f"{ 'Turn':<10} | {'Server 12 (s)':<15} | {'Server 13 (s)':<15} | {'Speedup':<10}")
    print(f"{ '-'*60}")
    
    for i in range(len(PROMPTS)):
        t12 = results["Server-12 (No Cache)"][i]
        t13 = results["Server-13 (KV Cache)"][i]
        
        s12_str = f"{t12:.4f}" if t12 else "N/A"
        s13_str = f"{t13:.4f}" if t13 else "N/A"
        
        speedup = "N/A"
        if t12 and t13 and t13 > 0:
            ratio = t12 / t13
            speedup = f"{ratio:.2f}x"
            
        print(f"{i+1:<10} | {s12_str:<15} | {s13_str:<15} | {speedup:<10}")
    print(f"{ '='*60}")

if __name__ == "__main__":
    run_test()
