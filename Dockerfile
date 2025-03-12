FROM ubuntu:22.04

COPY . qw

RUN apt-get update && apt-get install -y \
    python3 \
    sudo \
    git \
    cmake \
    g++ \
    make \
    net-tools \
    iputils-ping \
    zlib1g-dev \
    m4 \
    libssl-dev \
    libzstd1 \
    libz1 \
    libsodium23 \
    libgoogle-glog0v5 \
    libgflags2.2 \
    libdouble-conversion3 \
    libboost-all-dev \
    libboost-context1.74.0 \
    libboost-filesystem1.74.0 \
    libevent-2.1-7 \
    libssl3 \
    liblzma5 \
    liblz4-1 \
    libsnappy1v5 \
    libunwind8 \
    libstdc++6 \
    libgcc-s1

WORKDIR /qw

# RUN /client/proxygen/getdeps.sh 

CMD ["/bin/bash"]
