FROM ghcr.io/laude-institute/t-bench/ubuntu-24-04:20250624
WORKDIR /app

RUN apt update && apt install -y build-essential iproute2