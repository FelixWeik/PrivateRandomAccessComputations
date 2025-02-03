FROM ubuntu:latest
ENV DEBIAN_FRONTEND=noninteractive
RUN apt update && apt upgrade -y && \
    apt install -y \
    libgmp-dev \
    libboost-system-dev \
    libboost-context-dev \
    libboost-chrono-dev \
    libboost-thread-dev \
    libpthread-stubs0-dev \
    g++ \
    iproute2 \
    make \
    libbsd-dev
WORKDIR /app
COPY . /app
RUN make