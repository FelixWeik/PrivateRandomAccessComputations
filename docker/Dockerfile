FROM ubuntu:22.04
RUN apt update && apt install -y wget git build-essential gdb libgmp3-dev net-tools iproute2 iperf iputils-ping libbsd-dev libboost-all-dev numactl time
WORKDIR /root
COPY . prac
WORKDIR /root/prac
RUN make clean
RUN make -j `nproc`
CMD ["/bin/bash"]