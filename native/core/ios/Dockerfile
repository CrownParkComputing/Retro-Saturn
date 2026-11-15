FROM mobaiapp/iosbox:latest

RUN apt-get update && apt-get install -y --no-install-recommends \
    flex bison file ca-certificates && \
    rm -rf /var/lib/apt/lists/*