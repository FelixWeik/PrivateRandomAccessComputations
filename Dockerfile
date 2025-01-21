FROM ubuntu:latest
RUN apt update && apt install -y wget git build-essential net-tools iproute2 iperf iputils-ping libbsd-dev libboost-all-dev numactl time
WORKDIR /app
COPY ./prac /app/
CMD ./prac -t 8 -p 0 &\
    ./prac -t 8 -p 1 localhost &\
    ./prac -t 8 -p 2 localhost localhost r20:90;\
  wait; \
  ./prac -t 8 0 duoram 20 10& \
  ./prac -t 8 1 localhost duoram 20 10& \
  ./prac -t 8 2 localhost localhost duoram 20 10; \
  wait;