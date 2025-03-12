# QW Project

## Project Overview
This project sets up a Docker environment for running a Proxygen-based HTTP server and client with custom congestion control.

## Setup and Installation

### 1. Clone the Repository
```bash
git clone https://github.com/b4lt0/qw.git
cd qw
```

### 2. Build Docker Environment
Ensure Docker Desktop is running, then build the Docker image:
```bash
sudo docker build -t qw-image .
```

### 3. Run Docker Container
```bash
sudo docker run --name qw-container --net host --privileged -v ./:/qw -it qw-image
```

### 4. Inside Docker Container: Prepare Dependencies

#### Update System
```bash
apt-get update && apt-get upgrade
```

#### Install Fast Float Library
```bash
git clone https://github.com/fastfloat/fast_float.git
cd fast_float
mkdir build && cd build
cmake ..
sudo make install
```

#### Build Proxygen
```bash
cd /qw/proxygen/proxygen/
./build.sh -j 2
```
On my machine it works using just 2 jobs.

#### Prepare Sample Server Content
```bash
echo "abcd" > /qw/server/index.txt
```

## Usage

### Start Server
```bash
./_build/proxygen/httpserver/hq \
  --mode=server \
  --host=0.0.0.0 \
  --static_root=/qw/server/ \
  -qlogger_path=/qw/server/logs/ \
  -congestion=westwood
```

### Start Client
```bash
./_build/proxygen/httpserver/hq \
  --mode=client \
  --host=0.0.0.0 \
  --outdir=/qw/client \
  --path="/index.txt" \
  -qlogger_path=/qw/client/logs/
```

## Key Parameters Explained
- `--mode`: Specifies whether to run as server or client
- `--host`: Server address
- `--static_root`: Directory serving static files
- `--outdir`: Client download destination
- `-qlogger_path`: Directory for logging
- `-congestion`: Network congestion control algorithm

## Troubleshooting
- Ensure Docker is running before build
- Check network permissions if experiencing connectivity issues
- Verify all dependencies are correctly installed
- Contact me