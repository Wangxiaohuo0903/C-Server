import requests
import time
import json

# Configuration
SERVERS = {
    "Server-12 (No Cache)": "http://localhost:6060",
    "Server-13 (KV Cache)": "http://localhost:6061"
}

# Generate a long prompt (approx 500-600 tokens)
# Repeating a paragraph to ensure length without needing external files
LONG_TEXT = "The quick brown fox jumps over the lazy dog. " * 50 
INITIAL_PROMPT = f"Here is a long text to read: {LONG_TEXT}. Now, just say 'Acknowledged'."

FOLLOW_UP_PROMPT = "What animal jumped?"

def create_session(base_url):
    try:
        response = requests.post(f"{base_url}/api/sessions/new", timeout=5)
        response.raise_for_status()
        return response.json()['session_id']
    except Exception as e:
        print(f"Error creating session on {base_url}: {e}")
        return None

def chat(base_url, session_id, message, max_tokens=10):
    try:
        start_time = time.perf_counter()
        response = requests.post(
            f"{base_url}/api/sessions/{session_id}/chat",
            json={"message": message, "max_tokens": max_tokens, "temperature": 0.1},
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
    print(f"LONG CONTEXT Performance Comparison")
    print(f"Server 12 (No Cache) vs Server 13 (KV Cache)")
    print(f"Initial Prompt Length: ~{len(INITIAL_PROMPT.split())} words")
    print(f"{ '='*60}\n")

    for server_name, base_url in SERVERS.items():
        print(f"Testing {server_name} at {base_url}...")
        session_id = create_session(base_url)
        if not session_id:
            continue
            
        print(f"  -> Session created: {session_id}")
        
        # Turn 1: Long Context Injection
        print(f"  -> Turn 1: Sending long text... ", end="", flush=True)
        t1, _ = chat(base_url, session_id, INITIAL_PROMPT, max_tokens=5)
        if t1:
            print(f"{t1:.4f}s (Pre-fill)")
            results[server_name].append(t1)
        else:
            print("Failed")
            results[server_name].append(None)
            
        # Turn 2: Short Follow-up (Latency Test)
        print(f"  -> Turn 2: Short follow-up (max_tokens=1)... ", end="", flush=True)
        # Ask for a very short response to minimize generation time and highlight prompt processing time
        t2, _ = chat(base_url, session_id, "Is the text about a fox? Answer Yes or No.", max_tokens=1)
        if t2:
            print(f"{t2:.4f}s (Latency/TTFT)")
            results[server_name].append(t2)
        else:
            print("Failed")
            results[server_name].append(None)
        print()

    # Display Comparison
    print(f"\n{ '='*60}")
    print(f"{ 'Turn':<20} | {'Server 12 (s)':<15} | {'Server 13 (s)':<15} | {'Speedup':<10}")
    print(f"{'-'*60}")
    
    turns = ["1 (Long Context)", "2 (Short Follow-up)"]
    for i in range(len(turns)):
        t12 = results["Server-12 (No Cache)"][i]
        t13 = results["Server-13 (KV Cache)"][i]
        
        s12_str = f"{t12:.4f}" if t12 else "N/A"
        s13_str = f"{t13:.4f}" if t13 else "N/A"
        
        speedup = "N/A"
        if t12 and t13 and t13 > 0:
            ratio = t12 / t13
            speedup = f"{ratio:.2f}x"
            
        print(f"{turns[i]:<20} | {s12_str:<15} | {s13_str:<15} | {speedup:<10}")
    print(f"{ '='*60}")

if __name__ == "__main__":
    run_test()
