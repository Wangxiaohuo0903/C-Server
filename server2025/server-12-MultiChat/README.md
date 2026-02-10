容器运行命令：
docker run -it -p 7008:8080 -v "$(pwd)/../models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf:/app/models/model.gguf"   server2026
其中这里$(pwd)/../models/DeepSeek-R1-Distill-Qwen-1.5B-Q4_K_L.gguf是具体的模型挂载的路径
运行起来后
执行任意一行就可以运行对应的服务器，
可用服务器:
  cd /app/server-11-LLM && ./build/ai_infra_server_11
  cd /app/server-12-MultiChat && ./build/ai_infra_server
  cd /app/server-13-KVcache && ./build/ai_infra_server
  cd /app/server-14 && ./build/ai_infra_server
如果想自行修改代码后运行则进入服务器对应目录后，重新build即可
cd /app/server-12-MultiChat && \
    cmake -B build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j$(nproc)

然后运行./build/ai_infra_server即可

运行后测试方案
curl -X POST http://localhost:8080/api/sessions/new
获取到seesion id

{"session_id":"sess_21fec44d","status":"created"}%    
然后拼到下面的请求里去获取回答

curl -X POST http://localhost:7008/api/sessions/sess_21fec44d/chat \
      -H 'Content-Type: application/json' \
     -d '{"message":"what is LLM?", "max_tokens":"128"}'

     
