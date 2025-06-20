#! /bin/bash
docker start star3 && \
container_status==$(docker inspect --format='{{.State.Running}}' star3)
if [ "$container_status" = "true" ]; then
    docker exec -it star3 bash
fi