FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --no-install-recommends --yes \
        build-essential \
        ca-certificates \
        clang \
        clang-format \
        valgrind \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace
COPY . .

RUN make release

CMD ["./build/release/bin/hospital_system"]
