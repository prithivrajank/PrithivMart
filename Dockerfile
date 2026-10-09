FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    pkg-config \
    libdrogon-dev \
    libcurl4-openssl-dev \
    libpq-dev \
    libjsoncpp-dev \
    uuid-dev \
    zlib1g-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --config Release -j2

ENV PORT=8080
EXPOSE 8080

CMD ["sh", "-c", "./build/PrithivMart"]
